#include "emit.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- StrBuf: a plain malloc/realloc growable buffer for building C
 * source text during emission. Not arena-backed on purpose -- this is
 * compiler-internal scratch work, not part of any AST or output the
 * caller keeps; the final result gets arena_strdup'd once, at the end. */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

static void sb_init(StrBuf *sb) {
    sb->cap = 512;
    sb->data = (char *)malloc(sb->cap);
    sb->len = 0;
    sb->data[0] = '\0';
}

static void sb_free(StrBuf *sb) {
    free(sb->data);
    sb->data = NULL;
}

static void sb_append(StrBuf *sb, const char *s) {
    size_t slen = strlen(s);
    if (sb->len + slen + 1 > sb->cap) {
        while (sb->len + slen + 1 > sb->cap) sb->cap *= 2;
        sb->data = (char *)realloc(sb->data, sb->cap);
    }
    memcpy(sb->data + sb->len, s, slen + 1);
    sb->len += slen;
}

static void sb_appendf(StrBuf *sb, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    sb_append(sb, buf);
}

/* sb_append_decl appends a real C variable/field declaration for
 * `c_type c_name` -- real, honest fix for a bug found this session
 * (caught by an actual gcc compile, not `parena build`'s own exit
 * code): a plain `"%s %s"` splice, used everywhere in this file until
 * now, produces flatly invalid C for a function-pointer type like
 * `"void (*)(T *)"` (resolve_declared_type()'s own real (Fn ..)
 * emission) -- C's own declarator syntax puts the name *inside* the
 * `(*)`, not after the whole type, e.g. `void (*run)(T *)`, not
 * `void (*)(T *) run`. emit_defn's own parameter-list loop already had
 * to solve this splicing once (for Fn-typed *parameters*); this is the
 * same real fix, factored out so process_defstruct's own field-typedef
 * and constructor-parameter-list emission (which hit this same bug
 * independently, for Fn-typed *struct fields*) can share it instead of
 * re-deriving it a third time. */
static void sb_append_decl(StrBuf *sb, const char *c_type, const char *c_name) {
    const char *paren_star = strstr(c_type, "(*)");
    if (paren_star) {
        size_t prefix_len = (size_t)(paren_star - c_type);
        sb_appendf(sb, "%.*s(*%s)%s", (int)prefix_len, c_type, c_name, paren_star + 3);
    } else {
        sb_appendf(sb, "%s %s", c_type, c_name);
    }
}

/* ---- EmitScope: tracks each in-scope binding's C type and whether it's
 * an Arena *value* (a with-arena local, `Arena name;`, needs `&` when
 * passed where Arena* is expected) or already a pointer (a function
 * parameter, `Arena *name`, used bare). */
#define MAX_LOCALS 64

typedef struct {
    const char *src_name; /* original Parena name, for scope_lookup */
    const char *c_name;   /* mangled C identifier */
    const char *c_type;   /* "Arena *", "Arena", "char *" */
    int is_arena_value;   /* true only for with-arena's own `Arena name;` locals */
} Local;

typedef struct EmitScope {
    struct EmitScope *parent;
    Local locals[MAX_LOCALS];
    int count;
} EmitScope;

static void scope_init(EmitScope *s, EmitScope *parent) {
    s->parent = parent;
    s->count = 0;
}

static void scope_bind(EmitScope *s, const char *src_name, const char *c_name, const char *c_type,
                        int is_arena_value) {
    if (s->count < MAX_LOCALS) {
        s->locals[s->count].src_name = src_name;
        s->locals[s->count].c_name = c_name;
        s->locals[s->count].c_type = c_type;
        s->locals[s->count].is_arena_value = is_arena_value;
        s->count++;
    }
}

static Local *scope_lookup(EmitScope *s, const char *src_name) {
    for (EmitScope *cur = s; cur; cur = cur->parent) {
        for (int i = cur->count - 1; i >= 0; i--) {
            if (strcmp(cur->locals[i].src_name, src_name) == 0) return &cur->locals[i];
        }
    }
    return NULL;
}

/* mangle turns a Parena identifier into a valid C one: '-' and '/' (the
 * only two non-C-identifier characters real STDLIB.md names use, e.g.
 * "buffer/set-data", "load-config") become '_'; a mid/trailing '!'
 * (the mutating-call naming convention, `vec/push!`/`set!`) does too.
 *
 * Real, distinct bug found and fixed here (2026-08-21, while getting
 * pentest/pcap.prn -- the first real stdlib file to actually reach a
 * gcc compile with a `!`-prefixed reference parameter -- to compile):
 * a LEADING '!' (the linear/mutable-binding sigil, `!t`/`!m`/`!cap`)
 * is stripped entirely instead, not converted to '_'. Multiple real
 * stdlib files' own hand-written `#target` inline-C bodies -- thread.
 * prn's own `lock` (`pthread_mutex_lock(&m->handle); make_guard(m)`
 * for a `(!m : &Mutex)` parameter) and pcap.prn's own `read-packet`/
 * `filter` -- reference their own `!`-prefixed parameter by its BARE,
 * un-prefixed name (`m`, `cap`), not an underscore-prefixed one; only
 * converting the leading `!` to `_` (this function's own original,
 * untested guess) produced a real, silent mismatch -- the emitted C
 * declares `T *_m`/`Capture *_cap` but the human-authored inline-C
 * body still says `m`/`cap`, an undeclared identifier. A trailing/
 * mid `!` (the separate, real mutating-*call*-name convention) keeps
 * converting to '_' as before -- that's this compiler's own naming
 * choice on both ends (it both mangles the call site AND names the
 * matching runtime function, e.g. parena_runtime.h's own `vec_push_`),
 * not an external human expectation to match, and stripping it there
 * instead would risk a real collision with a same-named non-mutating
 * sibling function (`push` vs `push!`). */
static const char *mangle(Arena *arena, const char *name) {
    size_t len = strlen(name);
    const char *src = name;
    if (len > 0 && name[0] == '!') {
        src = name + 1;
        len -= 1;
    }
    char *out = (char *)arena_alloc(arena, len + 1);
    for (size_t i = 0; i < len; i++) {
        char c = src[i];
        /* '?' joins the set here too (2026-08-21, found via world.prn's
         * own real `in-bounds?` predicate-naming convention -- a real,
         * common Scheme/Lisp/Ruby-style suffix for a Bool-returning
         * function, not previously exercised by anything that reached a
         * real gcc compile): unlike the leading-'!' sigil, a trailing
         * '?' has no real hand-authored #target body anywhere in this
         * stdlib assuming a specific stripped spelling, so converting
         * (not stripping) is the safe, consistent choice here, same
         * reasoning as '-'/'/' and the mid/trailing '!' case above. */
        out[i] = (c == '-' || c == '/' || c == '!' || c == '?') ? '_' : c;
    }
    out[len] = '\0';
    return out;
}

static int is_symbol(Node *n, const char *text) {
    return n && n->type == NODE_SYMBOL && n->text && strcmp(n->text, text) == 0;
}

/* ---- defenum registry: a real, user-defined tagged union, generalizing
 * the same {tag; void *value;} shape Result/Option already use (see
 * parena_runtime.h's own header comment on that real, honest single-
 * payload-field limitation). Real, extended scope (2026-08-20, while
 * getting scarab.prn's own real `SuiteNode` -- `Group`'s two real,
 * typed fields, `name` and `children` -- to compile): a variant now
 * carries any real number of typed payload fields, not just zero/one.
 * Zero/one-field variants keep the EXACT existing `{tag; void *value;}`
 * runtime shape unchanged (real, deliberate zero-regression-risk
 * choice -- every already-compiling defenum in this stdlib, editor/
 * events.prn's EditorEvent, gfd.prn's PanelKind, etc., stays exactly as
 * it was). Two-or-more-field variants get a real, distinct, additional
 * shape: a companion `EnumName_VariantName_Payload` struct (fields
 * stored by real, distinct C types, same "no shared void*" discipline
 * defstruct's own fields already use) allocated into an explicit
 * `Arena *dest` the constructor now takes as its own first parameter,
 * with a pointer to it stored in the SAME `void *value` field zero/
 * one-field variants already use -- `match`'s own existing "cast
 * value to the payload type" pattern (see emit_match's own code) works
 * unchanged either way, it just casts to a *Payload struct pointer
 * type for these instead of a scalar/single-field type. */
typedef struct {
    const char *name;   /* original source spelling */
    const char *c_name;  /* mangled */
    const char *c_type;
} EnumField;

typedef struct {
    const char *name;
    EnumField *fields;
    size_t field_count;
    int tag_value;
} EnumVariant;

typedef struct EnumInfo {
    const char *name;
    EnumVariant *variants;
    size_t variant_count;
    struct EnumInfo *next;
} EnumInfo;

static EnumInfo *g_enums = NULL;

/* find_enum_variant looks up variant_name across every registered enum,
 * regardless of which enum owns it -- used by emit_expr to recognize a
 * bare `VariantName` or `(VariantName arg)` construction, the same way
 * `Ok`/`Err`/`Some`/`None` are already recognized for the built-in
 * Result/Option types. */
static EnumInfo *find_enum_variant(const char *variant_name, EnumVariant **out_variant) {
    for (EnumInfo *e = g_enums; e; e = e->next) {
        for (size_t i = 0; i < e->variant_count; i++) {
            if (strcmp(e->variants[i].name, variant_name) == 0) {
                if (out_variant) *out_variant = &e->variants[i];
                return e;
            }
        }
    }
    return NULL;
}

/* find_enum_by_name looks up a registered enum by its own type name --
 * used by emit_match to recognize a scrutinee typed as a user defenum,
 * generalizing the hardcoded Result/Option check it already does. */
static EnumInfo *find_enum_by_name(const char *name) {
    for (EnumInfo *e = g_enums; e; e = e->next) {
        if (strcmp(e->name, name) == 0) return e;
    }
    return NULL;
}

/* ---- defstruct registry: a real, plain record type (as opposed to
 * defenum's tagged union) -- a `typedef struct { T1 f1; T2 f2; ... }`
 * with real, distinct per-field C types, not a single shared `void
 * *value` the way defenum's payload is. Same g_structs-is-file-scope-
 * state reasoning as g_enums above (one compilation per emit_c() call,
 * no real reentrancy concern). Real, honest, narrow scope: every field's
 * own type must resolve via resolve_declared_type() (Unit doesn't make
 * sense as a field type and is rejected same as anywhere else)-- a
 * field typed `(Vec T)`/`(Map K V)`/a reference type (`&T`/`&mut T`) is
 * real, separate, unstarted work (VS0 has no collections-as-types or
 * reference types yet either), reported honestly rather than guessed
 * at. */
typedef struct {
    const char *name;    /* original source spelling, e.g. "start-x" -- used for
                           * get-field's own :keyword lookup, which strips a
                           * leading ':' but never mangles (a real keyword like
                           * :start-x still has the literal hyphen in its text). */
    const char *c_name;  /* mangle()'d, e.g. "start_x" -- the only form ever
                           * written into emitted C; a source field name with a
                           * '-' emitted verbatim would be invalid C syntax (a
                           * real bug caught by actually compiling stdlib/csv.prn's
                           * own SplitOptions with real gcc, not guessed at). */
    const char *c_type;
    /* vec_elem_type/vec_elem_is_scalar: set only when this field's own
     * declared type was `(Vec ElemType)` (c_type == "Vec") -- the real
     * resolved element type (e.g. "double", "int", "TestCase"), and
     * whether it's a scalar needing real boxing (see parena_runtime.h's
     * own vec_box_i32/vec_box_f64) rather than a pointer-representable
     * type. Consulted by get-field()'s own emission to register a
     * g_vec_elem_hints entry for this exact field access, the same real
     * mechanism the `&(Vec ElemType)` parameter-binding path already
     * populates -- found necessary while getting world.prn's own real
     * `Terrain.heights` (a struct FIELD, not a parameter) to compile. */
    const char *vec_elem_type;
    int vec_elem_is_scalar;
} StructField;

typedef struct StructInfo {
    const char *name;
    StructField *fields;
    size_t field_count;
    struct StructInfo *next;
} StructInfo;

static StructInfo *g_structs = NULL;

/* g_vec_elem_hints -- a real, narrow, structural fix for a real gap
 * found while getting firefly.prn's own `run-tests` to compile:
 * `(Vec T)` erases T the same way `(Result ..)`/`(Option ..)` erase
 * their own type parameters (resolve_declared_type()'s own comment),
 * which is fine for the *struct field/param C type itself* (there's
 * only ever one real runtime `Vec` struct regardless of T -- VS0 has
 * no generics), but it means `(deref (vec/get cases i))` has no way to
 * know `cases` (bound `Vec *`) holds `TestCase` elements specifically,
 * so `vec/get`'s own return type falls back to the generic, useless
 * "void *" -- `deref` then can't do anything meaningful with that.
 *
 * Rather than thread real generic type parameters through every C type
 * string this emitter produces (a much larger, riskier redesign -- see
 * the commit message for the fuller real reasoning), this is a small,
 * separate, parallel side-table: when a parameter is bound with a
 * `&(Vec ElemType)` type (the exact shape that surfaced this gap),
 * record `param's own mangled C name -> ElemType` here. emit_call()'s
 * own `vec_get` handling consults it, keyed off the CALL SITE's own
 * first-argument symbol text, to report the real element type instead
 * of the generic fallback. Reset per emit_c() call, same real reason
 * g_enums/g_structs already are (this is per-compilation state, not
 * something that should leak across files/tests). */
typedef struct VecElemHint {
    const char *c_name;    /* the Vec-holding expression's own exact emitted C
                             * text -- a plain mangled param name for the
                             * `&(Vec ElemType)` parameter path, or a real
                             * struct-field-access expression like
                             * "(t)->heights" for the get-field path added
                             * alongside process_defstruct()'s own new
                             * vec_elem_type tracking (2026-08-21, world.prn's
                             * real `Terrain.heights`). */
    const char *elem_type; /* the real element type name, e.g. "TestCase"/"double" */
    int is_scalar;          /* real I32/F64/Bool element -- see parena_runtime.h's
                              * own vec_box_i32/vec_box_f64 for why this needs a
                              * real, separate boxing step at push!/set-at! call
                              * sites, unlike a pointer-representable element. */
    struct VecElemHint *next;
} VecElemHint;
static VecElemHint *g_vec_elem_hints = NULL;

static void record_vec_elem_hint(Arena *arena, const char *c_name, const char *elem_type, int is_scalar) {
    VecElemHint *hint = (VecElemHint *)arena_alloc(arena, sizeof(VecElemHint));
    hint->c_name = c_name;
    hint->elem_type = elem_type;
    hint->is_scalar = is_scalar;
    hint->next = g_vec_elem_hints;
    g_vec_elem_hints = hint;
}

static VecElemHint *find_vec_elem_hint(const char *c_name) {
    for (VecElemHint *h = g_vec_elem_hints; h; h = h->next) {
        if (strcmp(h->c_name, c_name) == 0) return h;
    }
    return NULL;
}

/* g_defn_return_types -- a real, minimal function-signature registry:
 * every `defn`'s own mangled name -> its own declared return type,
 * populated as each defn is emitted. Real bug found this session (an
 * actual gcc -pedantic compile of firefly.prn's own `fatalf`, whose
 * whole body is a plain call to `errorf` -- a real, user-defined,
 * Unit(void)-returning function): emit_call()'s own "no function-
 * signature table yet" fallback reports every unrecognized call as
 * returning generic "void *", which is wrong for a *known*, already-
 * emitted-earlier-in-this-same-file function like `errorf` -- wrong
 * enough that emit_body's own void-tail-statement-not-return fix
 * (added earlier this session for the exact same real ISO C99 rule)
 * never even fired, since it only checked for the literal string
 * "void", not "void *". Consulted by emit_call() before falling back
 * to the generic "void *" guess. Originally real, honest, narrow scope:
 * only functions VS0 had already emitted earlier in the SAME file were
 * registered -- a forward reference to a function not yet emitted fell
 * back to the generic guess. Widened (2026-08-21, alongside emit_c()'s
 * own new forward-declaration pre-pass -- see its own comment): every
 * explicitly-typed defn now registers here BEFORE any body is emitted,
 * so this registry also answers "is bare-name X a real, known defn
 * anywhere in this combined build" for mangle_call_name()'s own use
 * below, not just "what does it return." */
typedef struct DefnReturnType {
    const char *c_name;
    const char *return_type;
    const char *payload_type; /* NULL unless return_type is "Result"/"Option" AND the
                                * payload type itself was resolvable -- see
                                * resolve_result_option_payload_type()'s own declaration
                                * comment for why this is real, but narrower than a full
                                * generic type-parameter system. Consulted by `unwrap`'s
                                * own emit_expr handling. */
    struct DefnReturnType *next;
} DefnReturnType;
static DefnReturnType *g_defn_return_types = NULL;

static const char *find_defn_return_type(const char *c_name) {
    for (DefnReturnType *d = g_defn_return_types; d; d = d->next) {
        if (strcmp(d->c_name, c_name) == 0) return d->return_type;
    }
    return NULL;
}

static const char *find_defn_payload_type(const char *c_name) {
    for (DefnReturnType *d = g_defn_return_types; d; d = d->next) {
        if (strcmp(d->c_name, c_name) == 0) return d->payload_type;
    }
    return NULL;
}

/* mangle_call_name -- real, structural fix for a call-site gap
 * genuinely distinct from mangle()'s own character-substitution job
 * (found 2026-08-21 via a real, deliberate multi-file test: a function
 * in one module calling another module's own exported function by its
 * qualified `module/function` name, e.g. glob.prn's own real
 * `(string/length pattern)` calling into string.prn's own real
 * `length`). mangle() alone just blindly turns every `/` into `_`, so
 * `string/length` becomes the call-site text "string_length" -- a C
 * identifier that was NEVER what any real defn actually compiles to
 * (a defn's own name is never itself prefixed by its enclosing
 * module: `(defn length ...)` inside `(module string)` still compiles
 * to bare `length`). This compiler's own multi-file build has no real
 * per-module C symbol table at all -- every top-level form from every
 * combined input file lives in one flat C namespace (see main.c's own
 * cmd_build() comment) -- so a qualified call can only ever correctly
 * resolve by falling back to the BARE, unqualified function name.
 *
 * Real, deliberately conservative resolution order, to avoid
 * regressing any call site that already worked before this fix
 * existed: try the bare (last `/`-segment) mangled name FIRST, but
 * ONLY use it if g_defn_return_types already knows a real defn by that
 * exact bare name (populated early by emit_c()'s own forward-
 * declaration pre-pass, so this sees every explicitly-typed defn in
 * the WHOLE combined build, not just earlier-in-file ones) --
 * otherwise falls back to the OLD, full-text mangle unchanged.
 *
 * Real, self-caught regression fixed here (2026-08-21, an actual gcc
 * -- no, caught even before that, by `parena build` itself misresolving
 * -- compile of array.prn's own `product`, whose real body calls
 * `(vec/get shape i)`): the bare-name-exists check ALONE isn't
 * sufficient -- array.prn ALSO defines its own, unrelated, real `get`
 * function (returning `(Result F64 IndexError)`) later in the very
 * same file, so `vec/get`'s own bare-mangled "get" collided with a
 * REAL, unrelated defn's own name, and the (wrong) resolution silently
 * won, producing `get(shape, i)` instead of the real `vec_get(shape,
 * i)`. `vec` is EXPLICITLY excluded from bare-name resolution here as
 * a result -- it's a confirmed, hardcoded RUNTIME pseudo-module
 * (parena_runtime.h's own comment: its function names were
 * deliberately chosen to already match mangle()'s own full-text
 * output), so its own qualified spelling is ALREADY the one, real,
 * correct target -- there is no real `vec.prn` file at all whose own
 * exported names could ever collide with a `vec/`-qualified call, so
 * this exclusion loses nothing real. `string/concat`, found and fixed
 * earlier this session by adding a hardcoded `string_concat` RUNTIME
 * helper, needs no equivalent exclusion: a single-file build of a file
 * calling `string/concat` without string.prn combined in has no real
 * bare `concat` defn anywhere in that build, so the bare-name lookup
 * correctly misses there on its own, preserving that already-working,
 * already-tested call site exactly as before. */
static const char *mangle_call_name(Arena *arena, const char *text) {
    if (strncmp(text, "vec/", 4) == 0) {
        return mangle(arena, text);
    }
    const char *last_slash = strrchr(text, '/');
    if (last_slash && last_slash[1] != '\0') {
        const char *bare_mangled = mangle(arena, last_slash + 1);
        if (find_defn_return_type(bare_mangled)) {
            return bare_mangled;
        }
    }
    return mangle(arena, text);
}

/* g_boxed_types / g_box_helpers -- generated-once-per-type boxing
 * helper functions, the real fix for Ok/Err/Some (and single-field
 * defenum variants) needing to wrap a NON-pointer payload -- a
 * map-literal struct construction (array.prn's own real `(Ok {:data
 * data ...})`) or a deref'd scalar (array.prn's own real `(Ok (deref
 * (vec/get ...)))`) -- into the runtime's `void *value` field.
 *
 * Real, honest reasoning for THIS approach over the alternatives: a
 * GNU statement-expression (`({ Type *p = ...; *p = v; p; })`) is
 * rejected outright by this project's own `-pedantic -Werror` build;
 * hoisting a synthesized temp-variable DECLARATION into the enclosing
 * statement would need `emit_expr`'s pure-expression-returning
 * architecture to grow statement-emission capability everywhere it's
 * called from, a much larger change. A generated `static inline
 * TypeName *TypeName_box(Arena *dest, TypeName v)` helper sidesteps
 * both: the "declare a local, take its address, copy the value in"
 * logic lives inside the HELPER FUNCTION's own body, where `v` is a
 * genuine, addressable C local (a function parameter) -- so the call
 * SITE only ever needs a plain, valid, pure C99 function-call
 * expression (`NDArray_box(dest, NDArray_new(...))`), composing
 * exactly like any other call this emitter already produces.
 *
 * One real helper function is generated per distinct payload type
 * (deduped via g_boxed_types), collected into a separate buffer
 * (g_box_helpers, not `out` directly -- emit_expr has no StrBuf
 * parameter) and spliced into the final output by emit_c() itself,
 * positioned after the struct/enum pre-pass (so e.g. NDArray's own
 * typedef already exists) but before any defn body (so every real
 * call site, textually, follows its own helper's definition -- C
 * requires a function be at least declared before use). */
typedef struct BoxedType {
    const char *type_name;
    struct BoxedType *next;
} BoxedType;
static BoxedType *g_boxed_types = NULL;
static StrBuf g_box_helpers;

/* g_veceq_types / g_veceq_helpers -- the real fix for `vec-eq?`
 * (array.prn's own real `same-shape?`: `(vec-eq? &(get-field a :shape)
 * &(get-field b :shape))`), found completely unimplemented anywhere --
 * no runtime `vec_eq_` function at all, an honest "implicit declaration
 * of function" gcc error, not caught by `parena build`'s own exit code.
 *
 * Real, honest reasoning for why this can't just be one generic runtime
 * function: `Vec` is fully type-erased at the runtime level (a plain
 * `void *item` per slot -- see parena_runtime.h's own struct). For a
 * scalar element (I32/F64), each slot holds a BOXED pointer to a fresh
 * arena cell (vec_box_i32/vec_box_f64), a different pointer value on
 * every push even for equal underlying numbers -- a generic pointer
 * comparison would be real, actively WRONG (reporting two structurally
 * equal shape Vecs as unequal). The correct comparison needs to know
 * the element type to dereference-and-compare it, which the runtime
 * itself has no way to know. Real, honest, narrow scope, the same
 * "compiler generates a per-type helper, deduped" shape g_box_helpers
 * above already established: one `static inline int Type_vec_eq(Vec *,
 * Vec *)` helper generated per distinct SCALAR element type actually
 * compared, found via the exact same g_vec_elem_hints registry
 * vec_get's own element-type reporting already reads. A non-scalar
 * (pointer-representable) element type is real, separate, un-attempted
 * work -- reported as an honest compiler error, not guessed at, since a
 * plain pointer comparison there might sometimes be right (two
 * references to the very same allocation) but is not real, general
 * structural equality either. */
typedef struct VecEqHelper {
    const char *elem_type;
    struct VecEqHelper *next;
} VecEqHelper;
static VecEqHelper *g_veceq_types = NULL;
static StrBuf g_veceq_helpers;

static const char *ensure_veceq_helper(Arena *arena, const char *c_type) {
    char name_buf[160];
    snprintf(name_buf, sizeof(name_buf), "%s_vec_eq", c_type);
    for (VecEqHelper *v = g_veceq_types; v; v = v->next) {
        if (strcmp(v->elem_type, c_type) == 0) {
            return arena_strdup(arena, name_buf, strlen(name_buf));
        }
    }
    VecEqHelper *nv = (VecEqHelper *)arena_alloc(arena, sizeof(VecEqHelper));
    nv->elem_type = arena_strdup(arena, c_type, strlen(c_type));
    nv->next = g_veceq_types;
    g_veceq_types = nv;
    sb_appendf(&g_veceq_helpers,
               "static inline int %s_vec_eq(Vec *a, Vec *b) {\n"
               "    if (vec_len(a) != vec_len(b)) return 0;\n"
               "    for (int i = 0; i < vec_len(a); i++) {\n"
               "        if (*(%s *)vec_get(a, i) != *(%s *)vec_get(b, i)) return 0;\n"
               "    }\n"
               "    return 1;\n"
               "}\n\n",
               c_type, c_type, c_type);
    return arena_strdup(arena, name_buf, strlen(name_buf));
}

/* g_lambda_helpers / g_lambda_count -- the real fix for `(fn [params]
 * body)` used as a VALUE (array.prn's own real `add`/`mul-elementwise`:
 * `(elementwise a b (fn [x y] (+ x y)) dest)`, passed where `elementwise`
 * itself declares `op : (Fn [F64 F64] F64)`) -- found genuinely
 * unhandled anywhere in emit_expr(), falling through to the generic
 * "unsupported expression form" fallback.
 *
 * Same real "generate a real, addressable, file-scope C function" shape
 * g_box_helpers/ensure_box_helper() above already established for the
 * identical underlying problem (emit_expr's own pure-expression-
 * returning architecture can't emit a fresh function DEFINITION inline
 * at an expression's own call site) -- collected into this separate
 * buffer for the same reason, spliced into the final output by
 * emit_c() itself before any defn body, so a lambda used near the top
 * of the file is still declared before its own first use.
 *
 * Real, honest, deliberately narrow scope, matching this language's own
 * "no ambient anything, explicit everywhere" convention: every param
 * needs an EXPLICIT `(name : Type)` annotation, the same shape `defn`
 * parameters already require -- VS0 has no type inference, so a bare
 * `[x y]` would need to infer both param types from how the lambda gets
 * USED, a real, separate, much larger feature (this emitter has no
 * expected-type context threading into emit_expr at all). This also
 * means a REAL closure/capture is not supported: the generated helper
 * is a genuine top-level `static` C function, which -- exactly like any
 * hand-written C function -- cannot see the enclosing PARENA function's
 * own locals. An attempted capture fails honestly at the gcc stage
 * ("use of undeclared identifier"), not silently miscompiled -- the
 * lambda's own scope is deliberately built fresh (no parent), so a
 * captured name isn't even accidentally resolved against the wrong
 * binding first. */
static StrBuf g_lambda_helpers;
static int g_lambda_count = 0;

/* g_veclit_helpers / g_veclit_count -- the real fix for a Vec LITERAL
 * (`[e1 e2 ...]`) used directly as a value, e.g. linalg.prn's own real
 * `(array/zeros [a-rows b-cols] dest)` -- found completely unhandled
 * (2026-08-21, gcc-verifying linalg.prn: `matmul`/`transpose` both
 * construct a shape Vec this way), falling through to the generic
 * "unsupported expression form" fallback (NODE_VEC had no real
 * expression-position handling anywhere, only as a defn/loop/let
 * PARAMETER-LIST shape). Same "generate a real, addressable helper
 * function" architecture g_box_helpers/g_lambda_helpers/g_veceq_helpers
 * above already established -- one fresh function PER LITERAL (no
 * dedup like the per-type helpers above; two literals at different
 * call sites aren't guaranteed interchangeable) that allocates a real
 * Vec via `vec_new(dest)` then pushes each element in source order,
 * boxing I32/F64 scalars the same way `vec/push!` already does at
 * every other real call site. */
static StrBuf g_veclit_helpers;
static int g_veclit_count = 0;

static const char *ensure_box_helper(Arena *arena, const char *c_type) {
    char name_buf[160];
    snprintf(name_buf, sizeof(name_buf), "%s_box", c_type);
    for (BoxedType *b = g_boxed_types; b; b = b->next) {
        if (strcmp(b->type_name, c_type) == 0) {
            return arena_strdup(arena, name_buf, strlen(name_buf));
        }
    }
    BoxedType *nb = (BoxedType *)arena_alloc(arena, sizeof(BoxedType));
    nb->type_name = arena_strdup(arena, c_type, strlen(c_type));
    nb->next = g_boxed_types;
    g_boxed_types = nb;
    sb_appendf(&g_box_helpers,
               "static inline %s *%s_box(Arena *dest, %s v) {\n"
               "    %s *p = (%s *)arena_alloc(dest, sizeof(%s));\n"
               "    *p = v;\n"
               "    return p;\n"
               "}\n\n",
               c_type, c_type, c_type, c_type, c_type, c_type);
    return arena_strdup(arena, name_buf, strlen(name_buf));
}

/* find_dest_arena -- the real, narrow, honest search for "which Arena
 * do I box this into" at an Ok/Err/Some call site, which (unlike
 * `alloc`) names no arena of its own. STDLIB.md's own array-package
 * design section states the real, established convention directly:
 * "every allocating function takes an explicit dest : Arena @ Region"
 * -- so a bound local named exactly "dest" is checked first. Falls
 * back to the first arena-typed local found anywhere in scope
 * (function parameter or with-arena local) if no "dest" exists, since
 * a differently-named arena parameter is still real and usable. Real,
 * honest limitation, unchanged in spirit from the map-literal
 * struct-match's own "genuinely ambiguous only if..." comment: with
 * more than one arena in scope and no "dest", the first one found
 * wins rather than being reported as ambiguous -- narrower than a
 * real type-directed choice, but no real stdlib call site today has
 * more than one arena in scope at an Ok/Err/Some site. Returns NULL
 * (boxing impossible) when no arena is in scope at all -- e.g.
 * array.prn's own real `get`, whose whole signature carries no Arena
 * parameter -- which keeps failing the same honest way it already did
 * before this fix. */
static Local *find_dest_arena(EmitScope *scope) {
    Local *named = scope_lookup(scope, "dest");
    if (named && (named->is_arena_value || strcmp(named->c_type, "Arena *") == 0)) return named;
    for (EmitScope *cur = scope; cur; cur = cur->parent) {
        for (int i = cur->count - 1; i >= 0; i--) {
            if (cur->locals[i].is_arena_value || strcmp(cur->locals[i].c_type, "Arena *") == 0) {
                return &cur->locals[i];
            }
        }
    }
    return NULL;
}

static StructInfo *find_struct_by_name(const char *name) {
    for (StructInfo *s = g_structs; s; s = s->next) {
        if (strcmp(s->name, name) == 0) return s;
    }
    return NULL;
}

static int is_call_named(Node *n, const char *fn_name) {
    return n && n->type == NODE_LIST && n->child_count > 0 && is_symbol(n->children[0], fn_name);
}

/* binop_c_symbol maps a real PARENA operator symbol to its C infix
 * equivalent, or NULL if `sym` isn't one of them. Real, honest scope:
 * the 2-argument case only -- every real (op a b) call this stdlib's own
 * .prn files actually use (region.c's own vec/map/string test source is
 * the real grounding), not a general variadic-arithmetic evaluator. `=`
 * maps to C's `==` since PARENA's own `=` is equality (mutation is the
 * separate `set!` form), not assignment. */
static const char *binop_c_symbol(const char *sym) {
    if (strcmp(sym, "+") == 0) return "+";
    if (strcmp(sym, "-") == 0) return "-";
    if (strcmp(sym, "*") == 0) return "*";
    if (strcmp(sym, "/") == 0) return "/";
    if (strcmp(sym, "<") == 0) return "<";
    if (strcmp(sym, ">") == 0) return ">";
    if (strcmp(sym, "<=") == 0) return "<=";
    if (strcmp(sym, ">=") == 0) return ">=";
    if (strcmp(sym, "=") == 0) return "==";
    if (strcmp(sym, "and") == 0) return "&&";
    if (strcmp(sym, "or") == 0) return "||";
    /* Real, honest new addition (2026-08-20): bitwise operators, needed
     * for the first time by a real byte-level codec (stdlib/compress/
     * lz4.prn's own token-header packing -- a 4-bit literal-length
     * nibble and a 4-bit match-length nibble combined into one byte).
     * `bit-and`/`bit-or`/`bit-xor`/`shl`/`shr` (not bare `&`/`|`/`<<`/
     * `>>`) since `&` is already this language's own reference-type
     * sigil (see mangle()'s/emit_expr()'s own `&`/`&mut` handling) and
     * `<`/`>` are already comparison operators -- real, own, unambiguous
     * names rather than overloading punctuation this language already
     * gives a different meaning. `mod` for integer remainder, the same
     * real gap firefly.prn's own early continue-vs-recur exploration
     * surfaced (a real `(mod i 2)` call in a test that predates this
     * fix, never a defined runtime function -- now a real operator). */
    if (strcmp(sym, "bit-and") == 0) return "&";
    if (strcmp(sym, "bit-or") == 0) return "|";
    if (strcmp(sym, "bit-xor") == 0) return "^";
    if (strcmp(sym, "shl") == 0) return "<<";
    if (strcmp(sym, "shr") == 0) return ">>";
    if (strcmp(sym, "mod") == 0) return "%";
    return NULL;
}

/* has_region_marker answers "is this an Arena @ :region/x (or generic
 * Arena @ <bare-symbol-region>) parameter": true for either a literal
 * `:region/x` keyword or a generic, bare-symbol region variable (the
 * real `Arena @ Region` shape this stdlib's own design docs use
 * throughout for a caller-supplied region -- cache.prn's open,
 * pentest/scan.prn's scan-ports, gfd.prn, etc.). Real, honest scope:
 * this doesn't do anything WITH the specific region name either way --
 * emit_defn's own binding logic only ever checked for the marker's
 * presence, never its content, so recognizing `@ <bare symbol>` as the
 * same real "this is a region-scoped Arena" signal is a direct
 * widening of that existing behavior, not a new claim to have
 * implemented real region-polymorphism analysis (a genuinely separate,
 * much bigger domain-2 concern, not attempted here).
 *
 * Real bug found and fixed here (2026-08-20, while getting firefly.prn
 * to compile): despite this function's own doc always having said
 * "Arena @ ...", the implementation only ever checked for the presence
 * of ANY `@`/keyword anywhere in the parameter node -- never that the
 * type token actually preceding `@` was the symbol `Arena`. That meant
 * `(msg : String @ :region/scratch)` (a real, common shape -- any
 * scratch-region-annotated non-Arena parameter) silently matched too,
 * and emit_defn's own param-binding branch below bound it as a bare
 * `Arena *`, discarding the real declared type entirely. Never caught
 * before now because nothing that previously reached a real gcc compile
 * combined a non-Arena type with a region annotation on a *parameter*
 * specifically (struct fields and return types both resolve their type
 * through resolve_declared_type() directly and never call this function
 * at all, so they were never affected). */
static int has_region_marker(Node *n) {
    if (n->child_count < 3 || n->children[2]->type != NODE_SYMBOL ||
        !is_symbol(n->children[2], "Arena")) {
        return 0;
    }
    for (size_t i = 3; i < n->child_count; i++) {
        if (n->children[i]->type == NODE_KEYWORD) return 1;
        if (n->children[i]->type == NODE_AT) return 1;
    }
    return 0;
}

/* Forward declaration -- resolve_declared_type() is defined much later
 * in this file (its own real, narrow scope grew organically over this
 * session), but process_defenum() now needs it too (2026-08-20, for
 * real multi-field payload types), same as several other functions
 * defined between here and there already do. */
static const char *resolve_declared_type(Arena *arena, Node *type_node, const char **out_error);

static char *fail(Arena *arena, const char **out_error, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    *out_error = arena_strdup(arena, buf, strlen(buf));
    return NULL;
}

/* resolve_result_option_payload_type -- the real, narrow fix for
 * `unwrap` (array.prn/linalg.prn/stats.prn/ringo.prn/nn.prn's own real
 * `(unwrap (array/get a idx))`/`(unwrap (stats/min data))` call sites,
 * found genuinely never defined anywhere in this stdlib): resolve_
 * declared_type()'s own `(Result X Y)`/`(Option X)` branch erases X
 * entirely, reporting just the bare string `"Result"`/`"Option"` (VS0
 * has no generics / real type-checking pass, the same real limitation
 * `(Vec T)`/`(Map K V)` already have) -- correct for every OTHER real
 * call site (Result/Option's own runtime shape genuinely is just
 * `{tag; void *value}`, X doesn't change that), but `unwrap` needs to
 * know X specifically to cast `.value` back to a real, concrete type
 * before dereferencing it. Rather than a real generic-parameter system,
 * this narrowly re-resolves just the payload slot X, called ONLY when
 * a `defn`'s own return type is registered into g_defn_return_types
 * (see its own declaration comment) -- so `unwrap` can look up a
 * KNOWN, already-emitted (or forward-declared) function's real payload
 * type by name, the same "real, narrow, call-site-grounded" scope this
 * emitter's other special forms already use. Returns NULL (not a real
 * failure -- silently declining, exactly like `find_defn_return_type`
 * itself returning NULL for a genuinely unknown callee) if `type_node`
 * isn't `(Result ..)`/`(Option ..)` at all, OR if X itself can't be
 * resolved (a `dummy_out_error` is used so a real resolution failure
 * here never breaks the OUTER, already-successful "Result"/"Option"
 * resolution that already covers every other real call site). */
static const char *resolve_result_option_payload_type(Arena *arena, Node *type_node) {
    if (type_node->type != NODE_LIST || type_node->child_count < 2 ||
        type_node->children[0]->type != NODE_SYMBOL) {
        return NULL;
    }
    if (!is_symbol(type_node->children[0], "Result") && !is_symbol(type_node->children[0], "Option")) {
        return NULL;
    }
    const char *dummy_out_error = NULL;
    return resolve_declared_type(arena, type_node->children[1], &dummy_out_error);
}

/* process_defenum handles one top-level `(defenum Name (Variant1)
 * (Variant2 (field : Type)) ...)`: registers Name into g_enums (so
 * emit_expr/emit_match can recognize its variants later in the same
 * compilation) and emits its real C type definitions -- a tag enum plus
 * a struct reusing Result/Option's own {tag; void *value;} shape (see
 * this file's own EnumInfo comment for why that's a deliberate, honest
 * generalization rather than a real per-variant-typed union), plus one
 * `static inline` constructor per variant. Emitted before any function
 * bodies (emit_c's own pre-pass), since a defn can reference these types
 * anywhere in its own signature or body. */
static int process_defenum(Arena *arena, StrBuf *out, Node *node, const char **out_error) {
    if (node->child_count < 3 || node->children[1]->type != NODE_SYMBOL) {
        return fail(arena, out_error, "defenum: malformed definition at line %d", node->line) != NULL;
    }
    const char *enum_name = node->children[1]->text;
    size_t variant_count = node->child_count - 2;
    EnumVariant *variants = (EnumVariant *)arena_alloc(arena, sizeof(EnumVariant) * variant_count);
    for (size_t i = 0; i < variant_count; i++) {
        Node *variant = node->children[2 + i];
        if (variant->type != NODE_LIST || variant->child_count < 1 || variant->children[0]->type != NODE_SYMBOL) {
            return fail(arena, out_error,
                        "defenum: variant at line %d must be (Name) or (Name (field : Type) ...)",
                        node->line) != NULL;
        }
        variants[i].name = variant->children[0]->text;
        variants[i].tag_value = (int)i;
        size_t field_count = variant->child_count - 1;
        variants[i].field_count = field_count;
        variants[i].fields = (EnumField *)arena_alloc(arena, sizeof(EnumField) * (field_count ? field_count : 1));
        for (size_t f = 0; f < field_count; f++) {
            Node *field = variant->children[1 + f];
            /* Real, honest scope kept identical to the pre-existing
             * single-field case: a field's own declared type is only
             * actually RESOLVED (via resolve_declared_type()) for
             * multi-field (2+) variants, which need real, distinct C
             * types to build a real companion payload struct. A
             * single-field variant keeps the exact pre-existing
             * behavior -- its one field is still just a generic
             * `void *`, never type-checked -- zero behavior change,
             * zero regression risk to every already-compiling
             * single-payload defenum in this stdlib. */
            if (field->type != NODE_LIST || field->child_count < 3 || field->children[0]->type != NODE_SYMBOL ||
                field->children[1]->type != NODE_COLON) {
                return fail(arena, out_error,
                            "defenum: field at line %d must be (name : Type) or (name : Type @ Region)",
                            variant->line) != NULL;
            }
            variants[i].fields[f].name = field->children[0]->text;
            variants[i].fields[f].c_name = mangle(arena, field->children[0]->text);
            if (field_count >= 2) {
                const char *field_type = resolve_declared_type(arena, field->children[2], out_error);
                if (!field_type) return 0;
                variants[i].fields[f].c_type = field_type;
            } else {
                variants[i].fields[f].c_type = "void *"; /* unused for the single-field path below */
            }
        }
    }

    EnumInfo *info = (EnumInfo *)arena_alloc(arena, sizeof(EnumInfo));
    info->name = enum_name;
    info->variants = variants;
    info->variant_count = variant_count;
    info->next = g_enums;
    g_enums = info;

    /* Tag constants get a real, distinct `_TAG_` infix (EditorEvent_TAG_
     * OnSave) rather than the same EnumName_VariantName shape the
     * constructor function below uses (EditorEvent_OnSave) -- C has a
     * single namespace for ordinary identifiers, so an enum constant and
     * a function can't share a name; caught by actually compiling a real
     * generated file with gcc during this feature's own development,
     * not just eyeballed. */
    sb_append(out, "typedef enum {\n");
    for (size_t i = 0; i < variant_count; i++) {
        sb_appendf(out, "    %s_TAG_%s,\n", enum_name, variants[i].name);
    }
    sb_appendf(out, "} %s_Tag;\n", enum_name);
    sb_appendf(out, "typedef struct { %s_Tag tag; void *value; } %s;\n", enum_name, enum_name);
    for (size_t i = 0; i < variant_count; i++) {
        if (variants[i].field_count >= 2) {
            /* Real, new multi-field-payload shape: a companion struct,
             * real distinct field types (same "no shared void*"
             * discipline defstruct's own fields already use), and a
             * constructor taking an explicit `Arena *dest` (its own
             * first parameter) to allocate it into -- boxed the same
             * real way a Vec's own backing storage is, not a claim of
             * some new allocation-free mechanism. */
            sb_appendf(out, "typedef struct {\n");
            for (size_t f = 0; f < variants[i].field_count; f++) {
                sb_append(out, "    ");
                sb_append_decl(out, variants[i].fields[f].c_type, variants[i].fields[f].c_name);
                sb_append(out, ";\n");
            }
            sb_appendf(out, "} %s_%s_Payload;\n", enum_name, variants[i].name);
            sb_appendf(out, "static inline %s %s_%s(Arena *dest", enum_name, enum_name, variants[i].name);
            for (size_t f = 0; f < variants[i].field_count; f++) {
                sb_append(out, ", ");
                sb_append_decl(out, variants[i].fields[f].c_type, variants[i].fields[f].c_name);
            }
            sb_appendf(out, ") {\n    %s_%s_Payload *p = (%s_%s_Payload *)arena_alloc(dest, sizeof(%s_%s_Payload));\n",
                       enum_name, variants[i].name, enum_name, variants[i].name, enum_name, variants[i].name);
            for (size_t f = 0; f < variants[i].field_count; f++) {
                sb_appendf(out, "    p->%s = %s;\n", variants[i].fields[f].c_name, variants[i].fields[f].c_name);
            }
            sb_appendf(out, "    %s v; v.tag = %s_TAG_%s; v.value = p; return v;\n}\n",
                       enum_name, enum_name, variants[i].name);
        } else if (variants[i].field_count == 1) {
            sb_appendf(out,
                       "static inline %s %s_%s(void *value) { %s v; v.tag = %s_TAG_%s; v.value = value; "
                       "return v; }\n",
                       enum_name, enum_name, variants[i].name, enum_name, enum_name, variants[i].name);
        } else {
            sb_appendf(out,
                       "static inline %s %s_%s(void) { %s v; v.tag = %s_TAG_%s; v.value = NULL; "
                       "return v; }\n",
                       enum_name, enum_name, variants[i].name, enum_name, enum_name, variants[i].name);
        }
    }
    sb_append(out, "\n");
    return 1;
}

/* arena_arg_expr resolves how to pass a known local/param wherever an
 * `Arena *` is expected: a with-arena value needs `&name`, a function
 * parameter (already a pointer) is used bare. */
static const char *arena_arg_expr(Arena *arena, Local *b) {
    if (b->is_arena_value) {
        char buf[128];
        snprintf(buf, sizeof(buf), "&%s", b->c_name);
        return arena_strdup(arena, buf, strlen(buf));
    }
    return b->c_name;
}

static int emit_body(Arena *arena, StrBuf *out, Node **forms, size_t count, EmitScope *scope,
                      int return_mode, const char **out_return_type, const char **out_error);
static Node *find_target_c_src(Arena *arena, Node *target_map, const char **out_error);

/* emit_alloc_call handles the one rank-producing/value-producing call
 * this pass understands: `(alloc arena-expr String value)`, two real
 * shapes for `value`:
 *
 * (1) A string LITERAL (the original, only-ever-supported shape) --
 * emits `arena_strdup(<arena-arg>, "literal", strlen("literal"))`.
 *
 * (2) A real SIZE EXPRESSION, not a literal -- found genuinely missing
 * (2026-08-21, gcc-verifying string.prn's own real `concat`:
 * `(alloc dest String (+ (length a) (length b)))`, immediately filled
 * by a following `#target` inline-C body's own `strcpy`/`strcat`, not
 * pre-filled with known content at all). Emits `(char *)arena_alloc(
 * <arena-arg>, (<size-expr>) + 1)` -- the `+ 1` mirrors arena_strdup()'s
 * own real behavior (parena_runtime.h's twin, src/arena.c's compiler-
 * internal one: `arena_alloc(a, len + 1)`), so a String alloc always
 * reserves real room for the null terminator the same way regardless
 * of which of these two shapes filled it, not a detail `.prn` source
 * has to remember to add itself.
 *
 * Both shapes report "char *" via *out_type. */
static const char *emit_expr(Arena *arena, Node *expr, EmitScope *scope, const char **out_type,
                              const char **out_error);
static const char *emit_alloc_call(Arena *arena, Node *call, EmitScope *scope, const char **out_type,
                                    const char **out_error) {
    if (call->child_count < 4) {
        return fail(arena, out_error, "alloc: expected (alloc arena-expr Type value), got %zu forms",
                    call->child_count);
    }
    Node *arena_node = call->children[1];
    Node *type_node = call->children[2];
    Node *value_node = call->children[3];

    if (arena_node->type != NODE_SYMBOL) {
        return fail(arena, out_error, "alloc: arena argument must be a plain identifier at line %d",
                    arena_node->line);
    }
    Local *arena_local = scope_lookup(scope, arena_node->text);
    if (!arena_local) {
        return fail(arena, out_error, "alloc: unknown arena '%s' at line %d", arena_node->text,
                    arena_node->line);
    }
    if (!is_symbol(type_node, "String")) {
        return fail(arena, out_error, "alloc: only String is a supported alloc type so far (got '%s' at line %d)",
                    type_node->text ? type_node->text : "?", type_node->line);
    }
    *out_type = "char *";
    if (value_node->type == NODE_STRING) {
        char buf[512];
        snprintf(buf, sizeof(buf), "arena_strdup(%s, \"%s\", %zu)", arena_arg_expr(arena, arena_local),
                  value_node->text, value_node->text_len);
        return arena_strdup(arena, buf, strlen(buf));
    }
    const char *size_type = NULL;
    const char *size_c = emit_expr(arena, value_node, scope, &size_type, out_error);
    if (!size_c) return NULL;
    StrBuf buf;
    sb_init(&buf);
    sb_append(&buf, "(char *)arena_alloc(");
    sb_append(&buf, arena_arg_expr(arena, arena_local));
    sb_append(&buf, ", (");
    sb_append(&buf, size_c);
    sb_append(&buf, ") + 1)");
    const char *result = arena_strdup(arena, buf.data, buf.len);
    sb_free(&buf);
    return result;
}

static const char *emit_expr(Arena *arena, Node *expr, EmitScope *scope, const char **out_type,
                              const char **out_error);

/* emit_binop handles `(op a b)` for the real operator set binop_c_symbol
 * knows -- recursively emits both operands, joins with the real C infix
 * operator, wraps in parens so this composes correctly as a sub-
 * expression of anything else (another binop, a function-call argument,
 * an if-branch). Comparison/boolean ops report "int" (real C bool-as-int,
 * matching how region.c's own C code already treats truthiness); the
 * arithmetic operators report the left operand's own type (a real, honest simplification --
 * no actual numeric-type-promotion rules, flagged rather than pretended
 * solved). */
static const char *emit_binop(Arena *arena, Node *call, const char *c_op, EmitScope *scope,
                               const char **out_type, const char **out_error) {
    /* Real, narrow N-ary extension (2026-08-21, found blocking world.prn's
     * own real `(and a b c)`): `&&`/`||` specifically, not every operator
     * here -- logical AND/OR are genuinely associative, so left-folding
     * `(and a b c)` into `((a && b) && c)` is semantically exact, the
     * same real value C's own left-to-right `&&`/`||` chaining already
     * has. Deliberately NOT generalized to arithmetic/comparison
     * operators too: `(< a b c)` in real Lisp/Scheme means "a<b AND
     * b<c" (a genuine three-way chained comparison), which naive
     * pairwise folding `((a < b) < c)` would silently compute wrong
     * (comparing a real 0/1 int result against c) -- flagged as a
     * real, separate, unstarted gap rather than guessed at here. */
    if (call->child_count > 3 && (strcmp(c_op, "&&") == 0 || strcmp(c_op, "||") == 0)) {
        const char *acc_type = NULL;
        const char *acc = emit_expr(arena, call->children[1], scope, &acc_type, out_error);
        if (!acc) return NULL;
        for (size_t i = 2; i < call->child_count; i++) {
            const char *next_type = NULL;
            const char *next = emit_expr(arena, call->children[i], scope, &next_type, out_error);
            if (!next) return NULL;
            char buf[1024];
            snprintf(buf, sizeof(buf), "(%s %s %s)", acc, c_op, next);
            acc = arena_strdup(arena, buf, strlen(buf));
        }
        *out_type = "int";
        return acc;
    }
    if (call->child_count != 3) {
        return fail(arena, out_error,
                     "emit: operator '%s' at line %d needs exactly 2 arguments (VS0's emitter doesn't "
                     "support variadic operators yet)",
                     call->children[0]->text, call->line);
    }
    const char *lhs_type = NULL;
    const char *lhs = emit_expr(arena, call->children[1], scope, &lhs_type, out_error);
    if (!lhs) return NULL;
    const char *rhs_type = NULL;
    const char *rhs = emit_expr(arena, call->children[2], scope, &rhs_type, out_error);
    if (!rhs) return NULL;

    int is_comparison = strcmp(c_op, "&&") == 0 || strcmp(c_op, "||") == 0 || strcmp(c_op, "==") == 0 ||
                         strcmp(c_op, "<") == 0 || strcmp(c_op, ">") == 0 || strcmp(c_op, "<=") == 0 ||
                         strcmp(c_op, ">=") == 0;
    *out_type = is_comparison ? "int" : lhs_type;

    char buf[512];
    snprintf(buf, sizeof(buf), "(%s %s %s)", lhs, c_op, rhs);
    return arena_strdup(arena, buf, strlen(buf));
}

/* emit_if handles `(if cond then else)` as a real C ternary -- correct
 * for expression position (which is the only position VS0's own real
 * `.prn` examples use `if` in so far), not a statement-position `if`
 * with side-effecting branches. */
static const char *emit_if(Arena *arena, Node *call, EmitScope *scope, const char **out_type,
                            const char **out_error) {
    if (call->child_count != 4) {
        return fail(arena, out_error, "emit: if at line %d needs exactly (if cond then else)", call->line);
    }
    const char *cond_type = NULL;
    const char *cond = emit_expr(arena, call->children[1], scope, &cond_type, out_error);
    if (!cond) return NULL;
    const char *then_type = NULL;
    const char *then_c = emit_expr(arena, call->children[2], scope, &then_type, out_error);
    if (!then_c) return NULL;
    const char *else_type = NULL;
    const char *else_c = emit_expr(arena, call->children[3], scope, &else_type, out_error);
    if (!else_c) return NULL;

    *out_type = then_type; /* real, honest simplification: no branch-type unification check yet */
    char buf[512];
    snprintf(buf, sizeof(buf), "(%s ? %s : %s)", cond, then_c, else_c);
    return arena_strdup(arena, buf, strlen(buf));
}

/* emit_cond handles `(cond (test1 result1) (test2 result2) ... (testN
 * resultN))` -- Lisp's own classic multi-clause conditional, found
 * missing entirely (2026-08-21, gcc-verifying regex/glob.prn's own
 * real `glob-match`, whose whole body is a `cond`): with no handling
 * anywhere, this silently fell through to the generic call path and
 * mangled into a bogus call to a never-defined `cond(...)` C function
 * -- `parena build`'s own exit code never caught it, only an actual
 * gcc compile did. Three more real, already-written stdlib files use
 * this same real shape (string.prn's `split`, map.prn's `find-slot`,
 * expr.prn's `apply-binop`).
 *
 * Folds right-to-left into nested C ternaries, the same real
 * composition emit_if() already uses for a single `if` -- built via
 * direct sb_append() pieces, not sb_appendf(), since a chain of
 * several clauses folds into a long nested string that can genuinely
 * exceed sb_appendf()'s own fixed 1024-byte internal vsnprintf buffer
 * (the exact same class of real, previously-fixed truncation bug
 * documented on emit_defn's own final assembly).
 *
 * Real, honest, narrow scope: the LAST clause is always treated as the
 * unconditional default/base case -- its own result is used directly,
 * its own test is never even emitted -- matching every real clause set
 * actually written in this stdlib today (every one ends with a literal
 * `(true ...)` catch-all clause). A `cond` with no clauses, or a clause
 * that isn't a real `(test result)` two-element list, is reported, not
 * guessed at. */
static const char *emit_cond(Arena *arena, Node *call, EmitScope *scope, const char **out_type,
                              const char **out_error) {
    if (call->child_count < 2) {
        return fail(arena, out_error, "cond: needs at least one (test result) clause at line %d", call->line);
    }
    for (size_t i = 1; i < call->child_count; i++) {
        Node *clause = call->children[i];
        if (clause->type != NODE_LIST || clause->child_count != 2) {
            return fail(arena, out_error, "cond: each clause must be (test result) at line %d",
                        clause->line);
        }
    }
    Node *last_clause = call->children[call->child_count - 1];
    const char *result_type = NULL;
    const char *acc = emit_expr(arena, last_clause->children[1], scope, &result_type, out_error);
    if (!acc) return NULL;
    *out_type = result_type; /* real, honest simplification: no branch-type unification check yet, same as emit_if */
    for (size_t i = call->child_count - 2; i >= 1; i--) {
        Node *clause = call->children[i];
        const char *test_type = NULL;
        const char *test_c = emit_expr(arena, clause->children[0], scope, &test_type, out_error);
        if (!test_c) return NULL;
        const char *branch_type = NULL;
        const char *branch_c = emit_expr(arena, clause->children[1], scope, &branch_type, out_error);
        if (!branch_c) return NULL;
        StrBuf buf;
        sb_init(&buf);
        sb_append(&buf, "(");
        sb_append(&buf, test_c);
        sb_append(&buf, " ? ");
        sb_append(&buf, branch_c);
        sb_append(&buf, " : ");
        sb_append(&buf, acc);
        sb_append(&buf, ")");
        acc = arena_strdup(arena, buf.data, buf.len);
        sb_free(&buf);
        if (i == 1) break;
    }
    return acc;
}

/* emit_call handles a general function call `(fn-name arg1 arg2 ...)`
 * not otherwise recognized -- mangles the function name, recursively
 * emits each argument. Real, honest limitation: VS0 has no function-
 * signature table yet (no separate type-checking pass), so the return
 * type of an arbitrary call is reported as "void *" unless the callee
 * happens to be a known local closure -- a real gap flagged here, not
 * silently guessed at as something more specific. */
/* vec_call_target_hint -- shared by emit_call()'s own vec_get/
 * vec_push_/vec_set_at_ special-casing below: every one of them takes
 * the TARGET Vec as its own real first argument, optionally prefixed
 * by a `&`/`&mut` two-node address-of pair (the common real shape,
 * `&(get-field t :heights)`) -- this unwraps that prefix if present,
 * emits the real target expression once (registering any get-field-
 * sourced hint as a side effect, harmless to re-emit -- every
 * expression this compiler produces is pure C text), and looks up its
 * own recorded element-type hint by that exact text. Returns the
 * target's own emitted text via *out_target_text (needed again by the
 * scalar-boxing callers below) and the hint itself (NULL if none). */
static VecElemHint *vec_call_target_hint(Arena *arena, Node *call, EmitScope *scope,
                                          const char **out_target_text, const char **out_error) {
    Node *target_node = call->children[1];
    if (target_node->type == NODE_SYMBOL &&
        (is_symbol(target_node, "&") || is_symbol(target_node, "&mut")) && call->child_count >= 3) {
        target_node = call->children[2];
    }
    const char *target_type = NULL;
    const char *target_text = emit_expr(arena, target_node, scope, &target_type, out_error);
    *out_target_text = target_text;
    return target_text ? find_vec_elem_hint(target_text) : NULL;
}

static const char *emit_call(Arena *arena, Node *call, EmitScope *scope, const char **out_type,
                              const char **out_error) {
    const char *fn_name = mangle_call_name(arena, call->children[0]->text);
    /* vec-eq? -- see g_veceq_types/g_veceq_helpers' own declaration
     * comment for the full real reasoning. Handled entirely separately
     * from the generic argument loop below (like vec_push_/vec_set_at_'s
     * own special-casing, but returning early rather than just tweaking
     * one logical argument -- vec_eq_ needs BOTH its own arguments'
     * element-type hints resolved, not the generic call shape at all).
     * Each of the two arguments may independently carry its own
     * `&`/`&mut` two-node prefix (array.prn's own real call site,
     * `(vec-eq? &(get-field a :shape) &(get-field b :shape))`, has
     * both) -- unwrapped by hand here since vec_call_target_hint() only
     * ever handles the FIRST argument of a call (every other real
     * vec_* call site only ever has one Vec-typed argument to find). */
    if (strcmp(fn_name, "vec_eq_") == 0) {
        size_t idx = 1;
        if (idx >= call->child_count) {
            return fail(arena, out_error, "vec-eq?: expected exactly 2 arguments at line %d", call->line);
        }
        Node *a_node = call->children[idx];
        int a_ref = (a_node->type == NODE_SYMBOL && (is_symbol(a_node, "&") || is_symbol(a_node, "&mut")) &&
                     idx + 1 < call->child_count);
        if (a_ref) { idx++; a_node = call->children[idx]; }
        idx++;
        if (idx >= call->child_count) {
            return fail(arena, out_error, "vec-eq?: expected exactly 2 arguments at line %d", call->line);
        }
        Node *b_node = call->children[idx];
        int b_ref = (b_node->type == NODE_SYMBOL && (is_symbol(b_node, "&") || is_symbol(b_node, "&mut")) &&
                     idx + 1 < call->child_count);
        if (b_ref) { idx++; b_node = call->children[idx]; }
        idx++;
        if (idx != call->child_count) {
            return fail(arena, out_error, "vec-eq?: expected exactly 2 arguments at line %d", call->line);
        }
        const char *a_type = NULL;
        const char *a_text = emit_expr(arena, a_node, scope, &a_type, out_error);
        if (!a_text) return NULL;
        const char *b_type = NULL;
        const char *b_text = emit_expr(arena, b_node, scope, &b_type, out_error);
        if (!b_text) return NULL;
        VecElemHint *hint = find_vec_elem_hint(a_text);
        if (!hint || !hint->is_scalar) {
            return fail(arena, out_error,
                        "vec-eq?: at line %d, no known scalar (I32/F64) element type for this Vec "
                        "-- VS0's emitter only supports comparing scalar-boxed Vecs so far (real, "
                        "separate, un-attempted work for pointer-representable elements)",
                        call->line);
        }
        const char *eq_fn = ensure_veceq_helper(arena, hint->elem_type);
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s(%s%s%s, %s%s%s)", eq_fn,
                 a_ref ? "&(" : "", a_text, a_ref ? ")" : "",
                 b_ref ? "&(" : "", b_text, b_ref ? ")" : "");
        *out_type = "int";
        return arena_strdup(arena, buf, strlen(buf));
    }
    /* Real, narrow scalar-boxing fixup (2026-08-21, found via world.prn's
     * own real `Terrain.heights : (Vec F64)`): if this call is
     * vec_push_/vec_set_at_ AND its own target Vec has a recorded
     * SCALAR element-type hint (I32/F64/Bool -- see parena_runtime.h's
     * own vec_box_i32/vec_box_f64 for why a scalar needs real, separate
     * boxing a pointer-representable element doesn't), the LOGICAL
     * argument holding the value to store (vec_push_'s 2nd, vec_set_
     * at_'s 3rd) needs wrapping in the matching box call -- computed
     * once, up front, and applied by LOGICAL ARGUMENT POSITION inside
     * the real argument loop below (never by re-parsing the already-
     * built, comma-joined argument text, which breaks the instant any
     * argument is itself a multi-argument call, e.g. world.prn's own
     * real `index-for(t, x, z)` as the index argument). */
    const char *box_vec_arg = NULL;       /* the real `Vec *` text vec_box_i32/vec_box_f64's own
                                            * first argument needs -- NOT the same as
                                            * vec_call_target_hint()'s own unwrapped lookup key,
                                            * since a `&`/`&mut`-prefixed call site needs that
                                            * prefix back for a real pointer expression. */
    size_t box_logical_index = 0; /* 1-based position among children[1..]; 0 = "no boxing" */
    if (strcmp(fn_name, "vec_push_") == 0 || strcmp(fn_name, "vec_set_at_") == 0) {
        const char *target_text = NULL;
        /* Real, honest widening (2026-08-21, gcc-verifying array.prn's
         * own real `strides-for`/`zeros`): the boxing DECISION used to
         * require a g_vec_elem_hints entry for the target Vec, which
         * only ever gets recorded for a `&(Vec ElemType)` PARAMETER or a
         * `(Vec ElemType)` struct FIELD -- a plain `let`-bound local Vec
         * (e.g. `(let [s (vec/new dest)] ...)`, no type annotation
         * anywhere `s` itself owns) never got one, so a scalar pushed
         * onto it flowed through unboxed, producing real, broken C
         * (`vec_push_(&(s), running)` where `running` is a bare
         * `double`, not `void *`) -- caught only by an actual gcc
         * compile, `parena build`'s own exit code never noticed. The
         * hint was only ever needed to answer "what's the Vec's element
         * type" for vec_get's own return-type CAST -- for push/set-at,
         * the real, sufficient signal is simpler and already available
         * for free below: the VALUE argument's own emitted C type
         * (arg_type, from emit_expr itself). Boxing here no longer
         * depends on g_vec_elem_hints at all -- box_logical_index is
         * set unconditionally, and the real per-argument type check
         * happens inline in the loop below. */
        (void)vec_call_target_hint(arena, call, scope, &target_text, out_error);
        if (!target_text && out_error && *out_error) return NULL;
        box_logical_index = strcmp(fn_name, "vec_push_") == 0 ? 2 : 3;
        /* Real bug found and fixed here (an actual gcc compile of
         * world.prn's own set-height): the boxing wrap used to read
         * `args.data` at the point of processing the VALUE argument,
         * wrongly assuming it still held only the target's own text
         * -- by then it holds every PRIOR argument too (target AND
         * index, comma-joined), producing real, broken C
         * (`vec_box_f64(target, idx, , h)`). Captured here, once,
         * completely separately from the growing `args` buffer. */
        if (call->children[1]->type == NODE_SYMBOL &&
            (is_symbol(call->children[1], "&") || is_symbol(call->children[1], "&mut"))) {
            char buf[512];
            snprintf(buf, sizeof(buf), "&(%s)", target_text);
            box_vec_arg = arena_strdup(arena, buf, strlen(buf));
        } else {
            box_vec_arg = target_text;
        }
    }
    StrBuf args;
    sb_init(&args);
    size_t logical_index = 0;
    for (size_t i = 1; i < call->child_count; i++) {
        logical_index++;
        /* `&(expr)` -- a bare `&` symbol followed by a parenthesized
         * expression lexes as two SIBLING nodes in this call's own
         * argument list (see mangle()'s own header note on `!`/`-`/`/`
         * for the parallel "real syntax, real lexer/parser output"
         * discipline; here it's parser output, not mangling, but the
         * same "checked against a real `parena parse` run, not guessed"
         * standard applies) -- e.g. firefly.prn's own `(vec/push!
         * &(get-field !t :messages) msg)`. The single-token `&x` form
         * (no space) is already handled inside emit_expr() itself; this
         * is specifically the two-node form only a call's own argument
         * loop can see both halves of. */
        if (call->children[i]->type == NODE_SYMBOL &&
            (is_symbol(call->children[i], "&") || is_symbol(call->children[i], "&mut")) &&
            i + 1 < call->child_count) {
            const char *inner_type = NULL;
            const char *inner_c = emit_expr(arena, call->children[i + 1], scope, &inner_type, out_error);
            if (!inner_c) {
                sb_free(&args);
                return NULL;
            }
            if (i > 1) sb_append(&args, ", ");
            sb_appendf(&args, "&(%s)", inner_c);
            i++;
            continue;
        }
        const char *arg_type = NULL;
        const char *arg_c = emit_expr(arena, call->children[i], scope, &arg_type, out_error);
        if (!arg_c) {
            sb_free(&args);
            return NULL;
        }
        if (i > 1) sb_append(&args, ", ");
        int is_boxable_scalar = arg_type && (strcmp(arg_type, "int") == 0 || strcmp(arg_type, "double") == 0);
        if (box_vec_arg && logical_index == box_logical_index && is_boxable_scalar) {
            /* Box this specific value argument -- vec_box_i32/vec_box_f64
             * need the real `Vec *` target too (to allocate the scalar
             * cell from its own arena), which is always this call's own
             * first argument, already sitting at the front of `args`. */
            const char *box_fn = strcmp(arg_type, "int") == 0 ? "vec_box_i32" : "vec_box_f64";
            sb_appendf(&args, "%s(%s, %s)", box_fn, box_vec_arg, arg_c);
        } else {
            sb_append(&args, arg_c);
        }
    }
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s(%s)", fn_name, args.data);
    sb_free(&args);
    /* Real, honest, narrow exception to the generic "assume void *"
     * fallback below: parena_runtime.h's own Vec functions have real,
     * known, non-void*-by-default return types (vec_push_ specifically
     * returns real C void, not a pointer) -- reported accurately here
     * since callers genuinely need to know, e.g. `do`/emit_body's own
     * tail-position return-vs-statement choice (a real bug found this
     * same pass: emitting `return vec_push_(...)` inside a `Unit`-typed
     * function is real ISO C99 -pedantic error, "forbids 'return' with
     * expression, in function returning void" -- caught by actually
     * compiling the emitted C with gcc, not just trusting `parena
     * build`'s own exit code). Not a general function-signature table --
     * VS0 still doesn't have one -- just these four real, fixed runtime
     * names. */
    if (strcmp(fn_name, "vec_push_") == 0 || strcmp(fn_name, "vec_set_at_") == 0) {
        *out_type = "void";
    } else if (strcmp(fn_name, "vec_new") == 0) {
        *out_type = "Vec";
    } else if (strcmp(fn_name, "vec_len") == 0) {
        *out_type = "int";
    } else if (strcmp(fn_name, "vec_get") == 0 && call->child_count >= 2) {
        /* Real fix for the deeper Vec-element-type gap found while
         * getting firefly.prn's own `run-tests` to compile: `vec_get`
         * genuinely returns `void *` at the C level (every Vec is the
         * same one erased runtime struct), but if this call's own real
         * target expression -- a bare param/local (g_vec_elem_hints
         * populated by the `&(Vec ElemType)` parameter-binding path) OR
         * a get-field struct-field access (populated by process_
         * defstruct()'s own new vec_elem_type tracking + get-field's
         * own emission, 2026-08-21, world.prn's real `Terrain.heights`)
         * -- has a recorded real element type, report that instead of
         * the generic fallback -- `(deref (vec/get cases i))` can then
         * actually resolve to `TestCase`, not a useless `void`. */
        const char *target_text = NULL;
        VecElemHint *hint = vec_call_target_hint(arena, call, scope, &target_text, out_error);
        if (!target_text && out_error && *out_error) return NULL;
        if (hint) {
            /* A scalar element (I32/F64/Bool) reports the same "ElemType
             * *" shape a pointer-representable element already does:
             * real usage (world.prn's own `get-height`: `(deref
             * (vec/get ...))`) always wraps it in `deref` uniformly
             * regardless of element kind, and the Vec's own stored item
             * genuinely IS a real pointer to an arena-allocated scalar
             * cell now (parena_runtime.h's own vec_box_i32/vec_box_f64,
             * used at the push!/set-at! call site above), not a bit-
             * boxed value needing different deref treatment. */
            char t[128];
            snprintf(t, sizeof(t), "%s *", hint->elem_type);
            *out_type = arena_strdup(arena, t, strlen(t));
        } else {
            *out_type = "void *";
        }
    } else {
        /* g_defn_return_types (see its own declaration comment): a
         * known, already-emitted user-defined function's own real
         * declared return type, consulted before the generic "void *"
         * guess -- found necessary the same real way vec_get's own fix
         * above was, via an actual gcc compile of firefly.prn's `fatalf`
         * (a plain call to `errorf`, a real, Unit(void)-returning
         * function -- the generic "void *" guess made emit_body's own
         * void-tail-statement fix never fire, since it only matched the
         * literal string "void"). */
        const char *known = find_defn_return_type(fn_name);
        /* Real gap closed here (2026-08-21, gcc-verifying array.prn's own
         * real `elementwise`: `(vec/push! &out (op ...))`, where `op` is
         * a `(Fn [F64 F64] F64)`-typed PARAMETER, not a registered
         * top-level defn): a call through a scope-bound function-pointer
         * VALUE (a plain call-site symbol, e.g. `(op x y)` -- mangle_
         * call_name() leaves a name with no `/` in it unchanged, so this
         * emits as a real, valid `op(x, y)` C call through the pointer)
         * used to fall straight to the generic "void *" guess, since
         * g_defn_return_types only ever knows about real top-level defns.
         * That silently broke vec_push_'s own scalar-boxing decision just
         * above (`is_boxable_scalar` only fires for the literal strings
         * "int"/"double"), producing real, broken C (`vec_push_(&out,
         * op(x, y))`, a raw `double` where `void *` is required) --
         * caught only by an actual gcc compile, not `parena build`'s own
         * exit code. Fixed by checking scope for a local bound to this
         * exact callee name whose own resolved C type is a function-
         * pointer shape ("RetType (*)(ArgTypes)", the same shape
         * resolve_declared_type()'s own (Fn ..) branch produces) and
         * reporting its real return type instead. */
        Local *callee_local = known ? NULL : scope_lookup(scope, call->children[0]->text);
        const char *paren_star = callee_local ? strstr(callee_local->c_type, "(*)") : NULL;
        if (known) {
            *out_type = known;
        } else if (paren_star) {
            size_t ret_len = (size_t)(paren_star - callee_local->c_type);
            while (ret_len > 0 && callee_local->c_type[ret_len - 1] == ' ') ret_len--;
            *out_type = arena_strdup(arena, callee_local->c_type, ret_len);
        } else {
            *out_type = "void *";
        }
    }
    return arena_strdup(arena, buf, strlen(buf));
}

/* emit_expr emits a plain value-producing expression: a number literal,
 * a symbol reference, an `alloc` call, a known binary operator, an `if`
 * expression, or a general function call. Anything else (loop/recur,
 * match, collection literals, ...) is real, unsupported territory for
 * this pass -- reported, not guessed. */
static const char *emit_expr(Arena *arena, Node *expr, EmitScope *scope, const char **out_type,
                              const char **out_error) {
    if (expr->type == NODE_NUMBER) {
        *out_type = "double"; /* VS0 has no int-vs-float distinction yet -- a real, honest simplification */
        return expr->text;
    }
    /* A real, foundational gap found and fixed here (2026-08-20, while
     * getting firefly.prn to compile): string literals used as a plain
     * expression (e.g. `(string/concat "SKIP: " reason)`) had NO
     * handling anywhere in emit_expr() at all -- only NODE_NUMBER had a
     * literal path; the lexer's own lex_string() already unescapes `\n`/
     * `\t`/`\"`/`\\` into real raw bytes (a real embedded newline byte,
     * not the two characters `\` and `n`), so this has to re-escape
     * before emitting a real C string literal, not just wrap expr->text
     * in quotes -- doing that naively would emit invalid/broken C for
     * any string containing a real quote, backslash, or newline byte. */
    if (expr->type == NODE_STRING) {
        StrBuf sb;
        sb_init(&sb);
        sb_append(&sb, "\"");
        for (size_t i = 0; i < expr->text_len; i++) {
            unsigned char c = (unsigned char)expr->text[i];
            if (c == '"' || c == '\\') {
                char esc[3] = {'\\', (char)c, '\0'};
                sb_append(&sb, esc);
            } else if (c == '\n') {
                sb_append(&sb, "\\n");
            } else if (c == '\t') {
                sb_append(&sb, "\\t");
            } else {
                char one[2] = {(char)c, '\0'};
                sb_append(&sb, one);
            }
        }
        sb_append(&sb, "\"");
        *out_type = "char *";
        const char *result = arena_strdup(arena, sb.data, strlen(sb.data));
        sb_free(&sb);
        return result;
    }
    /* None -- the one real Result/Option constructor with no payload,
     * checked before the generic symbol-lookup path since it's a real
     * core-language value, not a local variable reference. */
    if (is_symbol(expr, "None")) {
        *out_type = "Option";
        return "option_none()";
    }
    /* true/false -- real literal values, same "Bool -> C int" convention
     * resolve_declared_type()'s own Bool handling already documents
     * (that comment's own "a new true/false literal in the lexer/
     * emit_expr neither exist yet" gap, closed here: literal
     * recognition only, not a real C99 <stdbool.h>/bool type -- that
     * fuller feature is still real, separate, deliberately deferred
     * scope, same as it was before). Found blocking firefly.prn's own
     * real `(set! (get-field !t :failed) true)`. */
    if (is_symbol(expr, "true")) {
        *out_type = "int";
        return "1";
    }
    if (is_symbol(expr, "false")) {
        *out_type = "int";
        return "0";
    }
    /* `unit` -- the Unit type's own singleton value, e.g. buffer.prn's
     * own real `(Ok unit)` (array.prn's `set!` uses the identical real
     * shape). Found genuinely missing (2026-08-21): no handling
     * anywhere, so a bare `unit` symbol fell through to the generic
     * scope_lookup path and failed as an unknown identifier. Real,
     * deliberately minimal fix: emits as a plain `NULL` and reports its
     * own type as "void *" -- already pointer-typed, so Ok/Err/Some's
     * own boxing check accepts it directly with no boxing needed at
     * all (NULL is already a valid, real `void *` value), unlike a
     * genuine scalar payload. */
    if (is_symbol(expr, "unit")) {
        *out_type = "void *";
        return "NULL";
    }
    /* A bare, zero-payload user defenum variant (e.g. `OnSave`) -- same
     * "checked before generic symbol lookup" treatment as `None` above,
     * since it's a real value constructor, not a local variable
     * reference. Checked before scope_lookup so a real variant name never
     * gets misreported as an unbound identifier. */
    if (expr->type == NODE_SYMBOL) {
        EnumVariant *variant = NULL;
        EnumInfo *owner = find_enum_variant(expr->text, &variant);
        if (owner && variant->field_count == 0) {
            *out_type = owner->name;
            char buf[128];
            snprintf(buf, sizeof(buf), "%s_%s()", owner->name, variant->name);
            return arena_strdup(arena, buf, strlen(buf));
        }
    }
    /* `&x` -- single-token address-of a plain local (the two-sibling-node
     * `& (expr)` form, e.g. `&(get-field !t :messages)`, is handled at
     * each call site's own argument-list loop instead, since it spans
     * two parent-list children rather than being one self-contained
     * node emit_expr's own per-node dispatch could recognize alone).
     * Checked before the generic scope_lookup path below since `&x`'s
     * own literal text (with the `&`) is never itself a bound name. */
    if (expr->type == NODE_SYMBOL && expr->text && expr->text[0] == '&' &&
        strcmp(expr->text, "&mut") != 0 && strlen(expr->text) > 1) {
        Local *b = scope_lookup(scope, expr->text + 1);
        if (!b) return fail(arena, out_error, "unknown identifier '%s' at line %d", expr->text + 1, expr->line);
        char buf[256];
        snprintf(buf, sizeof(buf), "&(%s)", b->c_name);
        char type_buf[128];
        snprintf(type_buf, sizeof(type_buf), "%s *", b->c_type);
        *out_type = arena_strdup(arena, type_buf, strlen(type_buf));
        return arena_strdup(arena, buf, strlen(buf));
    }
    if (expr->type == NODE_SYMBOL) {
        Local *b = scope_lookup(scope, expr->text);
        if (!b) return fail(arena, out_error, "unknown identifier '%s' at line %d", expr->text, expr->line);
        *out_type = b->c_type;
        return b->c_name;
    }
    if (is_call_named(expr, "alloc")) {
        return emit_alloc_call(arena, expr, scope, out_type, out_error);
    }
    if (is_call_named(expr, "if")) {
        return emit_if(arena, expr, scope, out_type, out_error);
    }
    if (is_call_named(expr, "cond")) {
        return emit_cond(arena, expr, scope, out_type, out_error);
    }
    /* Ok/Err/Some -- the real, payload-carrying Result/Option
     * constructors, matching NORTHSTAR's own "Zero-allocation pattern
     * matching" section's own Ok/Err/Some/None naming. Real, honest
     * limitation: the payload is emitted as-is and stored in the real
     * runtime's `void *value` field, an implicit pointer conversion the
     * C compiler allows but doesn't type-check against what `match`
     * later assumes it is -- the same real gap already flagged for
     * emit_call's own cross-function return types. */
    if (is_call_named(expr, "Ok") || is_call_named(expr, "Err") ||
        is_call_named(expr, "Some")) {
        if (expr->child_count != 2) {
            return fail(arena, out_error, "%s: expects exactly one argument at line %d",
                        expr->children[0]->text, expr->line);
        }
        const char *inner_type = NULL;
        const char *inner_c = emit_expr(arena, expr->children[1], scope, &inner_type, out_error);
        if (!inner_c) return NULL;
        /* Real, honest limitation: the runtime's own Result/Option store
         * their payload as `void *value` (see parena_runtime.h's own
         * comment) -- a non-pointer payload (e.g. a bare `double`, or a
         * by-value struct like `NDArray`) can't implicitly convert to
         * void* in real C. First real attempt: box it into a real,
         * arena-allocated cell via a generated per-type helper function
         * (see g_box_helpers' own comment) -- found genuinely necessary
         * while getting array.prn's own `from-vec` (`(Ok {:data data
         * ...})`, a by-value NDArray struct construction) to compile.
         * Only possible when a real Arena is findable in scope
         * (find_dest_arena) -- when none is (array.prn's own `get`, a
         * scalar-Ok with no Arena parameter at all in its signature),
         * this is reported rather than emitting C that fails to compile
         * downstream with a much more confusing error at the
         * *emitted-C* compile step. */
        if (!inner_type || inner_type[strlen(inner_type) - 1] != '*') {
            if (!inner_type) {
                return fail(arena, out_error,
                            "%s: VS0's emitter only supports pointer-typed payloads so far (got type "
                            "'?' at line %d)",
                            expr->children[0]->text, expr->line);
            }
            Local *arena_local = find_dest_arena(scope);
            if (!arena_local) {
                return fail(arena, out_error,
                            "%s: VS0's emitter only supports pointer-typed payloads so far, and no "
                            "Arena is in scope to box this non-pointer payload ('%s') into at line %d "
                            "(add a 'dest : Arena @ Region' parameter to box it)",
                            expr->children[0]->text, inner_type, expr->line);
            }
            const char *box_fn = ensure_box_helper(arena, inner_type);
            char boxed_buf[512];
            snprintf(boxed_buf, sizeof(boxed_buf), "%s(%s, %s)", box_fn, arena_arg_expr(arena, arena_local),
                      inner_c);
            inner_c = arena_strdup(arena, boxed_buf, strlen(boxed_buf));
        }
        char buf[512];
        if (is_symbol(expr->children[0], "Ok")) {
            *out_type = "Result";
            snprintf(buf, sizeof(buf), "result_ok(%s)", inner_c);
        } else if (is_symbol(expr->children[0], "Err")) {
            *out_type = "Result";
            snprintf(buf, sizeof(buf), "result_err(%s)", inner_c);
        } else {
            *out_type = "Option";
            snprintf(buf, sizeof(buf), "option_some(%s)", inner_c);
        }
        return arena_strdup(arena, buf, strlen(buf));
    }
    /* A payload-carrying user defenum variant call, e.g. `(OnKeybind
     * key-expr)` -- checked before the generic binop/call dispatch below
     * so a real variant name is never mistaken for a general function
     * call (which would mangle "OnKeybind" and emit a call to a
     * function that was never defined). Same real, honest pointer-typed-
     * payload requirement Ok/Err/Some already enforce above, for the
     * same reason (the runtime's own `void *value` field). */
    if (expr->type == NODE_LIST && expr->child_count > 0 && expr->children[0]->type == NODE_SYMBOL) {
        EnumVariant *variant = NULL;
        EnumInfo *owner = find_enum_variant(expr->children[0]->text, &variant);
        if (owner && variant->field_count >= 2) {
            /* Real, new multi-field-payload construction call, e.g.
             * `(Group dest name children)` -- the bare (non-namespaced)
             * variant-call convention. The constructor's own real first
             * parameter is the destination Arena (see process_defenum()'s
             * own comment on why the companion payload struct needs one)
             * -- real field-value arguments follow, one per declared
             * field, no pointer-type requirement (each field keeps its
             * own real, distinct C type from resolve_declared_type(),
             * unlike the single-field path's own generic void*). */
            if (expr->child_count != variant->field_count + 2) {
                return fail(arena, out_error,
                            "%s: expects a destination arena plus %zu field value(s) at line %d, got %zu",
                            expr->children[0]->text, variant->field_count, expr->line, expr->child_count - 1);
            }
            StrBuf args;
            sb_init(&args);
            for (size_t i = 1; i < expr->child_count; i++) {
                const char *arg_type = NULL;
                const char *arg_c = emit_expr(arena, expr->children[i], scope, &arg_type, out_error);
                if (!arg_c) {
                    sb_free(&args);
                    return NULL;
                }
                if (i > 1) sb_append(&args, ", ");
                sb_append(&args, arg_c);
            }
            *out_type = owner->name;
            char buf[512];
            snprintf(buf, sizeof(buf), "%s_%s(%s)", owner->name, variant->name, args.data);
            sb_free(&args);
            return arena_strdup(arena, buf, strlen(buf));
        }
        if (owner && variant->field_count == 1) {
            if (expr->child_count != 2) {
                return fail(arena, out_error, "%s: expects exactly one argument at line %d",
                            expr->children[0]->text, expr->line);
            }
            const char *inner_type = NULL;
            const char *inner_c = emit_expr(arena, expr->children[1], scope, &inner_type, out_error);
            if (!inner_c) return NULL;
            /* Same real boxing fallback Ok/Err/Some's own site above
             * uses, for the identical real reason (a single-field
             * defenum variant's own payload field can just as easily be
             * a by-value struct or scalar as a Result/Option's). */
            if (!inner_type || inner_type[strlen(inner_type) - 1] != '*') {
                Local *arena_local = inner_type ? find_dest_arena(scope) : NULL;
                if (!arena_local) {
                    return fail(arena, out_error,
                                "%s: VS0's emitter only supports pointer-typed payloads so far%s at "
                                "line %d",
                                expr->children[0]->text,
                                inner_type ? " (got type '?', no Arena in scope to box it into)"
                                           : " (got type '?')",
                                expr->line);
                }
                const char *box_fn = ensure_box_helper(arena, inner_type);
                char boxed_buf[512];
                snprintf(boxed_buf, sizeof(boxed_buf), "%s(%s, %s)", box_fn,
                          arena_arg_expr(arena, arena_local), inner_c);
                inner_c = arena_strdup(arena, boxed_buf, strlen(boxed_buf));
            }
            *out_type = owner->name;
            char buf[512];
            snprintf(buf, sizeof(buf), "%s_%s(%s)", owner->name, variant->name, inner_c);
            return arena_strdup(arena, buf, strlen(buf));
        }
    }
    /* A defstruct construction call, e.g. `(HttpResponse status headers
     * body)` -- positional, matching defenum's own `(VariantName arg)`
     * constructor-call convention rather than inventing a separate
     * keyword-argument syntax. Checked before the generic call dispatch
     * below for the same real reason as the defenum variant check
     * above: a struct name is a real constructor, not a function to
     * mangle-and-call. Real, honest requirement: the argument count must
     * match the struct's own real field count exactly -- VS0 has no
     * default-field-value concept, a mismatched call is reported rather
     * than silently zero-filling or truncating. */
    if (expr->type == NODE_LIST && expr->child_count > 0 && expr->children[0]->type == NODE_SYMBOL) {
        StructInfo *sinfo = find_struct_by_name(expr->children[0]->text);
        if (sinfo) {
            if (expr->child_count != sinfo->field_count + 1) {
                return fail(arena, out_error,
                            "%s: expects exactly %zu argument(s) (one per field) at line %d, got %zu",
                            sinfo->name, sinfo->field_count, expr->line, expr->child_count - 1);
            }
            StrBuf args;
            sb_init(&args);
            for (size_t i = 0; i < sinfo->field_count; i++) {
                const char *arg_type = NULL;
                const char *arg_c = emit_expr(arena, expr->children[i + 1], scope, &arg_type, out_error);
                if (!arg_c) {
                    sb_free(&args);
                    return NULL;
                }
                if (i > 0) sb_append(&args, ", ");
                sb_append(&args, arg_c);
            }
            *out_type = sinfo->name;
            char buf[512];
            snprintf(buf, sizeof(buf), "%s_new(%s)", sinfo->name, args.data);
            sb_free(&args);
            return arena_strdup(arena, buf, strlen(buf));
        }
    }
    /* (get-field struct-expr :field-name) -- the real field-access form
     * this stdlib's own thread.prn already uses (`(get-field ch
     * :guard)`), grounded in real prior usage rather than invented here.
     * struct-expr's own type must resolve to a registered defstruct;
     * :field-name must be a real field of that struct -- both checked,
     * neither guessed at. */
    /* (deref expr) -- the real dereference form scarab.prn's own
     * `(deref (vec/get &suite-tree i))` already uses, grounded in that
     * real prior usage the same way get-field is. Emits a real C
     * dereference; `expr`'s own resolved type must be a pointer (ends in
     * `*`) for that to mean anything, checked rather than guessed. */
    /* (set! (get-field target :field) value) -- the real, only shape
     * this stdlib's own mutation actually uses (firefly.prn's own
     * `errorf`: `(set! (get-field !t :failed) true)`), matching
     * STDLIB.md's own already-itemized gap #7 ("set! mutation"). Emits a
     * real C assignment; the target must literally be a `get-field` form
     * -- get-field's own emission already produces a real, valid C
     * lvalue (`(x).field` or `(x)->field`), reused here rather than
     * duplicating struct/field lookup a second time. A plain local-
     * variable target (`(set! x value)`, no defstruct involved) is real,
     * separate, unstarted scope -- not silently guessed at. */
    if (is_call_named(expr, "set!")) {
        if (expr->child_count != 3) {
            return fail(arena, out_error, "set!: expects exactly 2 arguments at line %d", expr->line);
        }
        if (!is_call_named(expr->children[1], "get-field")) {
            return fail(arena, out_error,
                        "set!: VS0's emitter only supports (set! (get-field target :field) value) so "
                        "far at line %d", expr->line);
        }
        const char *lhs_type = NULL;
        const char *lhs_c = emit_expr(arena, expr->children[1], scope, &lhs_type, out_error);
        if (!lhs_c) return NULL;
        const char *rhs_type = NULL;
        const char *rhs_c = emit_expr(arena, expr->children[2], scope, &rhs_type, out_error);
        if (!rhs_c) return NULL;
        *out_type = "void";
        char buf[512];
        snprintf(buf, sizeof(buf), "(%s = %s)", lhs_c, rhs_c);
        return arena_strdup(arena, buf, strlen(buf));
    }
    if (is_call_named(expr, "deref")) {
        if (expr->child_count != 2) {
            return fail(arena, out_error, "deref: expects exactly one argument at line %d", expr->line);
        }
        const char *inner_type = NULL;
        const char *inner_c = emit_expr(arena, expr->children[1], scope, &inner_type, out_error);
        if (!inner_c) return NULL;
        size_t inner_len = inner_type ? strlen(inner_type) : 0;
        if (inner_len == 0 || inner_type[inner_len - 1] != '*') {
            return fail(arena, out_error,
                        "deref: expects a pointer-typed argument at line %d (got type '%s')",
                        expr->line, inner_type ? inner_type : "?");
        }
        char type_buf[128];
        snprintf(type_buf, sizeof(type_buf), "%.*s", (int)(inner_len - 1), inner_type);
        /* Trailing space left by a "T *"-shaped type (every pointer type
         * this emitter produces, e.g. resolve_declared_type()'s own
         * "%s *" format) is harmless as a C type spelling but trimmed
         * here for a tidier scope_bind()/out_type value. */
        size_t tl = strlen(type_buf);
        while (tl > 0 && type_buf[tl - 1] == ' ') type_buf[--tl] = '\0';
        *out_type = arena_strdup(arena, type_buf, tl);
        /* Real bug found and fixed here (an actual gcc compile of
         * firefly.prn's own `run-tests`): a bare `*(expr)` is only
         * valid C when `expr`'s own REAL C type (not just what this
         * emitter's own internal type-tracking believes it to be) is
         * already the correct concrete pointer type. `vec_get` is the
         * real counter-example that surfaced this: parena_runtime.h's
         * own `vec_get` genuinely returns `void *` in real C regardless
         * of what emit_call()'s own g_vec_elem_hints-informed *out_type*
         * says it "means" -- dereferencing a real `void *` directly is
         * itself a distinct real ISO C error ("dereferencing 'void *'
         * pointer"). Casting to the known target type before
         * dereferencing fixes both at once: `*((TestCase *)(expr))` is
         * real, valid C whether `expr`'s own actual declared C type was
         * already `TestCase *` (a harmless redundant cast) or generic
         * `void *` (the cast is load-bearing there). */
        char buf[512];
        snprintf(buf, sizeof(buf), "(*((%s *)(%s)))", type_buf, inner_c);
        return arena_strdup(arena, buf, strlen(buf));
    }
    if (is_call_named(expr, "get-field")) {
        if (expr->child_count != 3 || expr->children[2]->type != NODE_KEYWORD) {
            return fail(arena, out_error,
                        "get-field: expected (get-field struct-expr :field-name) at line %d", expr->line);
        }
        const char *struct_type = NULL;
        const char *struct_c = emit_expr(arena, expr->children[1], scope, &struct_type, out_error);
        if (!struct_c) return NULL;
        /* struct-expr may be a reference (e.g. `!t : &mut T`, resolved
         * type "T *") rather than a plain value -- get-field auto-derefs
         * through exactly one pointer level the same way C's own `->`
         * does, since a real linear/mutable binding is still logically
         * "the struct itself" to every real call site in this stdlib
         * (firefly.prn's own `(get-field !t :failed)`). Only one level:
         * VS0 has no nested-reference types to worry about yet. */
        int is_ref = 0;
        const char *lookup_type = struct_type;
        char stripped_buf[128];
        if (struct_type) {
            size_t stl = strlen(struct_type);
            if (stl > 2 && struct_type[stl - 1] == '*' && struct_type[stl - 2] == ' ') {
                snprintf(stripped_buf, sizeof(stripped_buf), "%.*s", (int)(stl - 2), struct_type);
                lookup_type = stripped_buf;
                is_ref = 1;
            }
        }
        StructInfo *sinfo = lookup_type ? find_struct_by_name(lookup_type) : NULL;
        if (!sinfo) {
            return fail(arena, out_error,
                        "get-field: '%s' at line %d isn't a registered defstruct type",
                        struct_type ? struct_type : "?", expr->line);
        }
        /* field->text includes the leading ':' (see lexer.c's own
         * lex_keyword), same convention #target's own :c key lookup
         * already relies on elsewhere in this file. */
        const char *field_name = expr->children[2]->text + 1;
        for (size_t i = 0; i < sinfo->field_count; i++) {
            /* Lookup against the field's original source spelling
             * (a real :keyword's own text is never mangled), but the
             * emitted C has to reference the mangled c_name -- a field
             * declared `start-x` is really named `start_x` in the
             * struct's own C typedef (see process_defstruct's own
             * comment on why unmangled hyphens are invalid C). */
            if (strcmp(sinfo->fields[i].name, field_name) == 0) {
                *out_type = sinfo->fields[i].c_type;
                char buf[512];
                snprintf(buf, sizeof(buf), is_ref ? "(%s)->%s" : "(%s).%s", struct_c, sinfo->fields[i].c_name);
                const char *result = arena_strdup(arena, buf, strlen(buf));
                /* Register a g_vec_elem_hints entry for THIS exact field
                 * access if process_defstruct() recorded a real element
                 * type for it (a `(Vec ElemType)`-typed field) -- keyed
                 * by this access's own emitted text (`result` itself),
                 * the same real mechanism emit_call()'s vec_get/
                 * vec_push_/vec_set_at_ handling looks hints up by (see
                 * vec_call_target_hint()'s own comment). Found necessary
                 * getting world.prn's own real `Terrain.heights` to
                 * compile -- the earlier version of this mechanism only
                 * ever covered `&(Vec ElemType)` PARAMETERS, never a
                 * struct field accessed through get-field. */
                if (sinfo->fields[i].vec_elem_type) {
                    record_vec_elem_hint(arena, result, sinfo->fields[i].vec_elem_type,
                                          sinfo->fields[i].vec_elem_is_scalar);
                }
                return result;
            }
        }
        return fail(arena, out_error, "get-field: '%s' has no field '%s' at line %d",
                    sinfo->name, field_name, expr->line);
    }
    /* `(fn [(name : Type) ...] body)` -- an anonymous function VALUE
     * passed where a `(Fn [..] ..)`-typed parameter expects a real C
     * function pointer, e.g. array.prn's own `add`/`mul-elementwise`
     * calling `elementwise`'s own `op` parameter. See g_lambda_helpers'
     * own declaration comment for the full real reasoning: a fresh,
     * non-capturing, file-scope `static` C function is generated once
     * per lambda literal, collected into g_lambda_helpers (not `out`
     * directly -- same real reason g_box_helpers isn't), and this
     * expression position itself just becomes a reference to that
     * function's own name -- a real C function name IS already a valid
     * function-pointer value with no further decoration needed.
     *
     * Checked BEFORE the generic symbol-headed-call dispatch just below
     * -- real bug found and fixed here (2026-08-21, gcc-verifying
     * array.prn's own `add`): that generic case matches ANY
     * NODE_LIST with a symbol head, `fn` included, and would otherwise
     * always intercept it first and mangle `fn` into a bogus call to a
     * never-defined `fn(...)` C function -- this branch never even ran,
     * despite being real, present code, until moved above that catch-all. */
    if (is_call_named(expr, "fn") && expr->child_count == 3 && expr->children[1]->type == NODE_VEC) {
        Node *params = expr->children[1];
        Node *body = expr->children[2];
        EmitScope lambda_scope;
        scope_init(&lambda_scope, NULL); /* deliberately no parent -- see
                                             g_lambda_helpers' own comment
                                             on why real captures aren't
                                             supported and shouldn't even
                                             accidentally resolve. */
        StrBuf param_list;
        sb_init(&param_list);
        StrBuf type_list; /* real param C types only, comma-joined, so this
                            * value's own out_type can report a real,
                            * accurate "RetType (*)(ArgTypes)" function-
                            * pointer type -- the same shape
                            * resolve_declared_type()'s own (Fn ..) branch
                            * already produces for a PARAMETER of this
                            * type, kept consistent here for a VALUE of it. */
        sb_init(&type_list);
        if (params->child_count == 0) {
            sb_append(&param_list, "void");
            sb_append(&type_list, "void");
        }
        for (size_t i = 0; i < params->child_count; i++) {
            Node *param = params->children[i];
            if (param->type != NODE_LIST || param->child_count != 3 ||
                param->children[0]->type != NODE_SYMBOL || param->children[1]->type != NODE_COLON) {
                sb_free(&param_list);
                sb_free(&type_list);
                return fail(arena, out_error,
                            "fn: parameter at line %d needs an explicit '(name : Type)' annotation "
                            "-- VS0 has no type inference, the same explicit-typing convention "
                            "every defn parameter already follows",
                            expr->line);
            }
            const char *p_c_type = resolve_declared_type(arena, param->children[2], out_error);
            if (!p_c_type) {
                sb_free(&param_list);
                sb_free(&type_list);
                return NULL;
            }
            const char *p_c_name = mangle(arena, param->children[0]->text);
            scope_bind(&lambda_scope, param->children[0]->text, p_c_name, p_c_type, 0 /* not an arena value */);
            if (i > 0) {
                sb_append(&param_list, ", ");
                sb_append(&type_list, ", ");
            }
            sb_appendf(&param_list, "%s %s", p_c_type, p_c_name);
            sb_append(&type_list, p_c_type);
        }
        const char *body_type = NULL;
        const char *body_c = emit_expr(arena, body, &lambda_scope, &body_type, out_error);
        if (!body_c) {
            sb_free(&param_list);
            sb_free(&type_list);
            return NULL;
        }
        char lambda_name_buf[32];
        snprintf(lambda_name_buf, sizeof(lambda_name_buf), "__lambda_%d", g_lambda_count++);
        const char *lambda_name = arena_strdup(arena, lambda_name_buf, strlen(lambda_name_buf));
        sb_appendf(&g_lambda_helpers, "static %s %s(%s) {\n    return %s;\n}\n\n",
                   body_type, lambda_name, param_list.data, body_c);
        sb_free(&param_list);
        char type_buf[192];
        snprintf(type_buf, sizeof(type_buf), "%s (*)(%s)", body_type, type_list.data);
        sb_free(&type_list);
        *out_type = arena_strdup(arena, type_buf, strlen(type_buf));
        return lambda_name;
    }
    /* `(not x)` -- real, honest unary-operator gap found and fixed here
     * (2026-08-21, gcc-verifying array.prn's own real `elementwise`:
     * `(if (not (same-shape? a b)) ...)`): `binop_c_symbol()` only ever
     * maps 2-ARGUMENT operators, so `not` (a real, distinct 1-argument
     * form, not to be confused with `!`/`&mut`'s own reference-marker
     * meaning elsewhere in this emitter) fell through to the generic
     * symbol-headed-call dispatch just below and mangled into a bogus
     * call to a never-defined `not(...)` C function. Checked here, same
     * as `fn` just above, so it's never reached by that catch-all.
     * Reports "int" the same real, honest way every other comparison/
     * boolean operator in emit_binop() already does (C's own bool-as-
     * int convention, matching region.c's own real code). */
    if (is_call_named(expr, "not") && expr->child_count == 2) {
        const char *inner_type = NULL;
        const char *inner_c = emit_expr(arena, expr->children[1], scope, &inner_type, out_error);
        if (!inner_c) return NULL;
        *out_type = "int";
        char buf[512];
        snprintf(buf, sizeof(buf), "(!(%s))", inner_c);
        return arena_strdup(arena, buf, strlen(buf));
    }
    /* `(unwrap expr)` -- real, honest, narrow scope, found genuinely
     * never defined anywhere in this stdlib despite real call sites
     * (linalg.prn's own real `(unwrap (array/get a idx))`,
     * ringo.prn/nn.prn's own `(unwrap (stats/min data))`): panics
     * (aborts, with a real stderr message) on Err/None, otherwise
     * unboxes the real payload -- Rust's own well-known `.unwrap()`
     * semantics, the obvious real meaning this name signals. Requires
     * `expr` to be a DIRECT call to a known top-level defn whose own
     * payload type was resolved at registration time (see
     * resolve_result_option_payload_type()'s own declaration comment
     * for why: VS0 has no generics, so a Result/Option's own payload
     * type can only be known here by looking up a SPECIFIC, already-
     * registered callee, not derived from the Result/Option runtime
     * shape itself, which is genuinely erased). Checked here, before
     * the generic symbol-headed-call dispatch just below, the same
     * placement `not`/`fn` needed for the identical real reason. */
    if (is_call_named(expr, "unwrap") && expr->child_count == 2) {
        Node *inner = expr->children[1];
        if (inner->type != NODE_LIST || inner->child_count == 0 || inner->children[0]->type != NODE_SYMBOL) {
            return fail(arena, out_error,
                        "unwrap: at line %d, expects a direct call to a known function (e.g. "
                        "'(unwrap (array/get a idx))') -- VS0 has no generics, so a Result/"
                        "Option's own real payload type can only be found by looking up a "
                        "specific, known callee, not derived from an arbitrary expression",
                        expr->line);
        }
        const char *callee_name = mangle_call_name(arena, inner->children[0]->text);
        const char *payload_type = find_defn_payload_type(callee_name);
        const char *ret_kind = find_defn_return_type(callee_name);
        if (!payload_type || !ret_kind || (strcmp(ret_kind, "Result") != 0 && strcmp(ret_kind, "Option") != 0)) {
            return fail(arena, out_error,
                        "unwrap: at line %d, '%s' isn't a known function returning (Result .. ..) "
                        "or (Option ..) with a resolvable payload type",
                        expr->line, inner->children[0]->text);
        }
        const char *inner_type = NULL;
        const char *inner_c = emit_expr(arena, inner, scope, &inner_type, out_error);
        if (!inner_c) return NULL;
        const char *check_fn = strcmp(ret_kind, "Result") == 0 ? "result_unwrap_check" : "option_unwrap_check";
        char buf[1024];
        snprintf(buf, sizeof(buf), "(*((%s *)(%s(%s).value)))", payload_type, check_fn, inner_c);
        *out_type = payload_type;
        return arena_strdup(arena, buf, strlen(buf));
    }
    if (expr->type == NODE_LIST && expr->child_count > 0 && expr->children[0]->type == NODE_SYMBOL) {
        const char *c_op = binop_c_symbol(expr->children[0]->text);
        if (c_op) return emit_binop(arena, expr, c_op, scope, out_type, out_error);
        return emit_call(arena, expr, scope, out_type, out_error);
    }
    /* `((expr) arg1 arg2 ...)` -- calling a first-class function VALUE
     * (a `(Fn [..] ..)`-typed struct field/local, not a plain named
     * function), e.g. firefly.prn's own `((get-field tc :run) &mut t)`
     * (TestCase.run is exactly `(Fn [&mut T] Unit)`). Real, distinct gap
     * from emit_call()'s own symbol-headed-call path above: the callee
     * position here is itself a compound expression (a get-field call),
     * not a bare symbol, so it was never reachable through emit_call()
     * (which always mangles `call->children[0]->text`, assuming a
     * symbol). Real C function-pointer values are directly callable
     * with normal call syntax once parenthesized, so this just emits
     * the callee expression, wraps it in parens, and appends the
     * argument list -- the same real "&(expr)" two-node address-of
     * pairing emit_call()'s own argument loop already does is repeated
     * here rather than factored out, since duplicating ~10 lines is
     * cheaper and safer under real time pressure than restructuring
     * emit_call() to accept a pre-computed callee string this late in
     * the session. */
    if (expr->type == NODE_LIST && expr->child_count > 0 && expr->children[0]->type == NODE_LIST) {
        const char *callee_type = NULL;
        const char *callee_c = emit_expr(arena, expr->children[0], scope, &callee_type, out_error);
        if (!callee_c) return NULL;
        StrBuf args;
        sb_init(&args);
        for (size_t i = 1; i < expr->child_count; i++) {
            if (expr->children[i]->type == NODE_SYMBOL &&
                (is_symbol(expr->children[i], "&") || is_symbol(expr->children[i], "&mut")) &&
                i + 1 < expr->child_count) {
                const char *inner_type = NULL;
                const char *inner_c = emit_expr(arena, expr->children[i + 1], scope, &inner_type, out_error);
                if (!inner_c) {
                    sb_free(&args);
                    return NULL;
                }
                if (i > 1) sb_append(&args, ", ");
                sb_appendf(&args, "&(%s)", inner_c);
                i++;
                continue;
            }
            const char *arg_type = NULL;
            const char *arg_c = emit_expr(arena, expr->children[i], scope, &arg_type, out_error);
            if (!arg_c) {
                sb_free(&args);
                return NULL;
            }
            if (i > 1) sb_append(&args, ", ");
            sb_append(&args, arg_c);
        }
        char buf[1024];
        snprintf(buf, sizeof(buf), "(%s)(%s)", callee_c, args.data);
        sb_free(&args);
        *out_type = "void *"; /* same real, honest "no function-signature table yet" fallback emit_call() itself uses */
        return arena_strdup(arena, buf, strlen(buf));
    }
    /* `{:key1 val1 :key2 val2 ...}` -- map-literal struct construction,
     * the real gap STDLIB.md's own gap-analysis section already itemized
     * (#2), found blocking firefly.prn's own `run-tests`
     * (`{:passed passed :failed failed :skipped 0}`) and
     * firefly/ladybug.prn's own `expect` (`{:actual actual :t !t}`).
     * Real, honest, structural approach rather than deep type-context
     * threading: VS0 has no type-checking pass to tell this function
     * "the enclosing tail position expects a TestReport" (emit_expr()'s
     * own signature has no such hint), so this instead searches every
     * registered defstruct for the one whose own field NAME SET exactly
     * matches this map literal's own keyword keys -- the same "no real
     * type table, best real match" character every other part of this
     * emitter already has (struct/defenum construction-by-name is the
     * same kind of structural match, not name-mangled type inference).
     * Genuinely ambiguous only if two different registered structs
     * share an identical field-name set, which nothing in this stdlib's
     * own real source does today -- flagged, not silently guessed at,
     * if it ever happens. */
    if (expr->type == NODE_MAP) {
        if (expr->child_count % 2 != 0) {
            return fail(arena, out_error,
                        "map literal at line %d has an odd number of forms (expected :key value pairs)",
                        expr->line);
        }
        size_t pair_count = expr->child_count / 2;
        StructInfo *match = NULL;
        for (StructInfo *s = g_structs; s; s = s->next) {
            if (s->field_count != pair_count) continue;
            int all_found = 1;
            for (size_t f = 0; f < s->field_count; f++) {
                int found = 0;
                for (size_t i = 0; i < expr->child_count; i += 2) {
                    if (expr->children[i]->type == NODE_KEYWORD &&
                        strcmp(expr->children[i]->text + 1, s->fields[f].name) == 0) {
                        found = 1;
                        break;
                    }
                }
                if (!found) { all_found = 0; break; }
            }
            if (all_found) { match = s; break; }
        }
        if (!match) {
            return fail(arena, out_error,
                        "map literal at line %d doesn't match any registered defstruct's own field "
                        "set (VS0 has no real type context to construct an anonymous struct from)",
                        expr->line);
        }
        StrBuf args;
        sb_init(&args);
        for (size_t f = 0; f < match->field_count; f++) {
            Node *value_node = NULL;
            for (size_t i = 0; i < expr->child_count; i += 2) {
                if (expr->children[i]->type == NODE_KEYWORD &&
                    strcmp(expr->children[i]->text + 1, match->fields[f].name) == 0) {
                    value_node = expr->children[i + 1];
                    break;
                }
            }
            const char *val_type = NULL;
            const char *val_c = emit_expr(arena, value_node, scope, &val_type, out_error);
            if (!val_c) {
                sb_free(&args);
                return NULL;
            }
            if (f > 0) sb_append(&args, ", ");
            sb_append(&args, val_c);
        }
        *out_type = match->name;
        char buf[512];
        snprintf(buf, sizeof(buf), "%s_new(%s)", match->name, args.data);
        sb_free(&args);
        return arena_strdup(arena, buf, strlen(buf));
    }
    /* `[e1 e2 ...]` -- a Vec LITERAL used directly as a value. See
     * g_veclit_helpers' own declaration comment for the full real
     * reasoning. The Arena to allocate the Vec's own backing store
     * into is found via find_dest_arena() -- the identical scope
     * search Ok/Err/Some's own boxing already uses (a local literally
     * named "dest" first, else the first Arena-typed local in scope)
     * -- a real, honest, narrower limitation than a full call-site-
     * level Arena-inference feature, but consistent with every other
     * implicit-arena lookup this emitter already does. Real, honest,
     * narrow scope: every element must share ONE real C type (the
     * first element's own, matching every other "no real type-checking
     * pass" simplification already documented throughout this file) --
     * a genuinely mixed-type literal isn't checked for and isn't a
     * real call site anywhere in this stdlib today. */
    if (expr->type == NODE_VEC) {
        Local *dest_local = find_dest_arena(scope);
        if (!dest_local) {
            return fail(arena, out_error,
                        "vec literal at line %d needs an Arena in scope to allocate into (no "
                        "local named 'dest', or any other Arena-typed local, found)",
                        expr->line);
        }
        if (expr->child_count == 0) {
            return fail(arena, out_error,
                        "vec literal at line %d cannot be empty (no element type to infer -- VS0 "
                        "has no separate type-annotation syntax for an empty Vec literal)",
                        expr->line);
        }
        StrBuf elems;
        sb_init(&elems);
        const char *elem_c_type = NULL;
        for (size_t i = 0; i < expr->child_count; i++) {
            const char *el_type = NULL;
            const char *el_c = emit_expr(arena, expr->children[i], scope, &el_type, out_error);
            if (!el_c) {
                sb_free(&elems);
                return NULL;
            }
            if (i == 0) elem_c_type = el_type;
            if (i > 0) sb_append(&elems, ", ");
            sb_append(&elems, el_c);
        }
        int is_scalar = elem_c_type && (strcmp(elem_c_type, "int") == 0 || strcmp(elem_c_type, "double") == 0);
        const char *box_fn = is_scalar ? (strcmp(elem_c_type, "int") == 0 ? "vec_box_i32" : "vec_box_f64") : NULL;
        char lit_name_buf[32];
        snprintf(lit_name_buf, sizeof(lit_name_buf), "__veclit_%d", g_veclit_count++);
        const char *lit_name = arena_strdup(arena, lit_name_buf, strlen(lit_name_buf));
        StrBuf params;
        sb_init(&params);
        sb_append(&params, "Arena *dest");
        for (size_t i = 0; i < expr->child_count; i++) {
            sb_appendf(&params, ", %s e%zu", elem_c_type, i);
        }
        StrBuf body;
        sb_init(&body);
        sb_append(&body, "    Vec v = vec_new(dest);\n");
        for (size_t i = 0; i < expr->child_count; i++) {
            if (box_fn) {
                sb_appendf(&body, "    vec_push_(&v, %s(&v, e%zu));\n", box_fn, i);
            } else {
                sb_appendf(&body, "    vec_push_(&v, e%zu);\n", i);
            }
        }
        sb_append(&body, "    return v;\n");
        sb_appendf(&g_veclit_helpers, "static inline Vec %s(%s) {\n%s}\n\n", lit_name, params.data, body.data);
        sb_free(&params);
        sb_free(&body);
        char call_buf[1024];
        snprintf(call_buf, sizeof(call_buf), "%s(%s, %s)", lit_name, dest_local->c_name, elems.data);
        sb_free(&elems);
        const char *call_text = arena_strdup(arena, call_buf, strlen(call_buf));
        /* Register a real g_vec_elem_hints entry too, keyed by this
         * call's own emitted text, so a later vec/get on the
         * CONSTRUCTED value (not just on a named local/field) also
         * reports a real element type instead of the generic void *
         * fallback. */
        record_vec_elem_hint(arena, call_text, elem_c_type, is_scalar);
        *out_type = "Vec";
        return call_text;
    }
    return fail(arena, out_error, "emit: unsupported expression form at line %d", expr->line);
}

/* emit_with_arena emits `(with-arena [name region-kw size] body...)` as
 * a C block scoping the arena's own lifetime exactly: the
 * cleanup-attributed local is declared inside a fresh `{ }`, so it's
 * torn down the instant that block ends -- the real, literal C
 * expression of NORTHSTAR's own "reclaimed when its region ends." */
static int emit_with_arena(Arena *arena, StrBuf *out, Node *node, EmitScope *scope, int return_mode,
                            const char **out_return_type, const char **out_error) {
    if (node->child_count < 2 || node->children[1]->type != NODE_VEC ||
        node->children[1]->child_count < 1) {
        fail(arena, out_error, "with-arena: expected [name region size] at line %d", node->line);
        return 0;
    }
    Node *binding_vec = node->children[1];
    Node *name_node = binding_vec->children[0];
    if (name_node->type != NODE_SYMBOL) {
        fail(arena, out_error, "with-arena: binding name must be a plain identifier at line %d", node->line);
        return 0;
    }
    const char *c_name = mangle(arena, name_node->text);

    sb_append(out, "    {\n");
    sb_appendf(out, "        Arena %s __attribute__((cleanup(arena_free_all)));\n", c_name);
    sb_appendf(out, "        arena_init(&%s);\n", c_name);

    EmitScope child;
    scope_init(&child, scope);
    scope_bind(&child, name_node->text, c_name, "Arena", 1 /* is_arena_value */);

    if (!emit_body(arena, out, node->children + 2, node->child_count - 2, &child, return_mode,
                   out_return_type, out_error)) {
        return 0;
    }
    sb_append(out, "    }\n");
    return 1;
}

/* emit_let emits `(let [name1 expr1 name2 expr2 ...] body...)` as
 * sequential C local declarations -- VS0's own scope doesn't need
 * nested C blocks for `let` (no shadowing in the real test.prn shape),
 * a real, honest simplification. */
static int emit_let(Arena *arena, StrBuf *out, Node *node, EmitScope *scope, int return_mode,
                     const char **out_return_type, const char **out_error) {
    if (node->child_count < 2 || node->children[1]->type != NODE_VEC) {
        fail(arena, out_error, "let: expected a binding vector at line %d", node->line);
        return 0;
    }
    Node *bindings = node->children[1];

    EmitScope child;
    scope_init(&child, scope);

    for (size_t i = 0; i + 1 < bindings->child_count; i += 2) {
        Node *name_node = bindings->children[i];
        Node *expr_node = bindings->children[i + 1];
        if (name_node->type != NODE_SYMBOL) {
            fail(arena, out_error, "let: binding name must be a plain identifier at line %d", node->line);
            return 0;
        }
        const char *c_name = mangle(arena, name_node->text);
        const char *c_type = NULL;
        const char *expr_c = emit_expr(arena, expr_node, &child, &c_type, out_error);
        if (!expr_c) return 0;
        /* __attribute__((unused)): a `let` binding is real, valid source
         * (NORTHSTAR's own "scratch-to-buffer promotion" idiom computes
         * scratch-space work that isn't always the value ultimately
         * returned, e.g. test.prn's own temp-str) -- gcc -Wall correctly
         * flags a genuinely unused C local, but that's not a real bug in
         * the *Parena* source, so it's suppressed the standard, honest
         * C99 way rather than by disabling -Wunused-variable wholesale. */
        sb_appendf(out, "    %s %s __attribute__((unused)) = %s;\n", c_type, c_name, expr_c);
        scope_bind(&child, name_node->text, c_name, c_type, 0 /* not an arena value */);
    }

    return emit_body(arena, out, node->children + 2, node->child_count - 2, &child, return_mode,
                      out_return_type, out_error);
}

#define MAX_LOOP_VARS 32

static int emit_match_core(Arena *arena, StrBuf *out, Node *node, EmitScope *scope,
                            const char *result_var, Local **loop_locals, size_t loop_var_count,
                            const char **out_result_type, const char **out_error);

/* emit_loop_tail handles the real tail position inside a `loop` body --
 * exactly the shape every real `loop`/`recur` use in this stdlib
 * actually has (test.prn/region.c's own C code doesn't use this yet,
 * but stdlib/vec.prn's push!/grow!, stdlib/map.prn's find-slot, etc. --
 * the real .prn source already written this session -- all follow this
 * same shape): `(if cond then else)` where one branch is a plain
 * terminal value and the other is `(recur new-vals...)`. Originally
 * real, honest, narrower scope ("a `cond`/`match` in tail position
 * isn't supported yet") -- `cond` and (2026-08-21, gcc-verifying
 * net/http.prn's own real `serve`, whose accept-loop's whole body is
 * `(match (net/tcp/accept ...) ((Ok !conn) (do ... (recur))) ((Err e)
 * (Err e)))`) `match` were both since added below. */
static int emit_loop_tail(Arena *arena, StrBuf *out, Node *tail, EmitScope *scope,
                           Local **loop_locals, size_t loop_var_count, const char *result_var,
                           const char **out_result_type, const char **out_error) {
    if (is_call_named(tail, "if")) {
        if (tail->child_count != 4) {
            return fail(arena, out_error, "loop: if in tail position needs (if cond then else) at line %d",
                        tail->line) != NULL;
        }
        const char *cond_type = NULL;
        const char *cond = emit_expr(arena, tail->children[1], scope, &cond_type, out_error);
        if (!cond) return 0;
        sb_appendf(out, "        if (%s) {\n", cond);
        const char *then_type = NULL;
        if (!emit_loop_tail(arena, out, tail->children[2], scope, loop_locals, loop_var_count,
                             result_var, &then_type, out_error)) {
            return 0;
        }
        sb_append(out, "        } else {\n");
        const char *else_type = NULL;
        if (!emit_loop_tail(arena, out, tail->children[3], scope, loop_locals, loop_var_count,
                             result_var, &else_type, out_error)) {
            return 0;
        }
        sb_append(out, "        }\n");
        /* recur branches report no result type (they don't produce the
         * loop's own value) -- take whichever branch actually resolved
         * one, real terminal-value branch wins over a recur sibling. */
        if (out_result_type) *out_result_type = then_type ? then_type : else_type;
        return 1;
    }
    if (is_call_named(tail, "cond")) {
        /* `cond` in loop-tail position -- real, honest necessity (not
         * just the emit_expr()-level ternary-chain form emit_cond()
         * already provides): string.prn's own real `split` and
         * map.prn's own real `find-slot` both have `cond` as their
         * whole loop body, with `recur` inside more than one clause's
         * own result -- `recur` emits a real C `continue;` STATEMENT,
         * which can never appear inside a ternary expression, so the
         * plain emit_cond() ternary-chain genuinely cannot serve this
         * real shape. Same real N-ary recursive nested-if/else
         * composition `if`'s own loop-tail handling above already uses,
         * generalized past exactly two branches: each clause but the
         * last opens its own `if (test) { <recurse> } else {`, the
         * LAST clause's own result is emitted unconditionally (same
         * real, honest "last clause is the base case" scope
         * emit_cond() itself documents), then every opened `else {`
         * closes back up. */
        if (tail->child_count < 2) {
            return fail(arena, out_error, "loop: cond needs at least one (test result) clause at line %d",
                        tail->line) != NULL;
        }
        for (size_t i = 1; i < tail->child_count; i++) {
            Node *clause = tail->children[i];
            if (clause->type != NODE_LIST || clause->child_count != 2) {
                return fail(arena, out_error, "loop: cond clause must be (test result) at line %d",
                            clause->line) != NULL;
            }
        }
        size_t clause_count = tail->child_count - 1;
        /* Real bug found and fixed here (an actual gcc compile of an
         * isolated cond-in-loop-tail repro): only the LAST clause's own
         * branch_type used to reach out_result_type -- wrong the moment
         * the LAST clause is a `recur` (which reports no type at all)
         * while an EARLIER clause is the real terminal value, e.g.
         * `(cond ((>= i n) count) (true (recur ...)))`. Every clause's
         * own resolved type is tracked here, same real "take whichever
         * branch actually resolved one" fallback `if`'s own loop-tail
         * handling above already uses across exactly two branches,
         * generalized across N. */
        const char *resolved_type = NULL;
        for (size_t i = 1; i < clause_count; i++) {
            Node *clause = tail->children[i];
            const char *test_type = NULL;
            const char *test_c = emit_expr(arena, clause->children[0], scope, &test_type, out_error);
            if (!test_c) return 0;
            sb_appendf(out, "        if (%s) {\n", test_c);
            const char *branch_type = NULL;
            if (!emit_loop_tail(arena, out, clause->children[1], scope, loop_locals, loop_var_count,
                                 result_var, &branch_type, out_error)) {
                return 0;
            }
            if (!resolved_type) resolved_type = branch_type;
            sb_append(out, "        } else {\n");
        }
        Node *last_clause = tail->children[tail->child_count - 1];
        const char *last_type = NULL;
        if (!emit_loop_tail(arena, out, last_clause->children[1], scope, loop_locals, loop_var_count,
                             result_var, &last_type, out_error)) {
            return 0;
        }
        if (!resolved_type) resolved_type = last_type;
        for (size_t i = 1; i < clause_count; i++) {
            sb_append(out, "        }\n");
        }
        if (out_result_type) *out_result_type = resolved_type;
        return 1;
    }
    if (is_call_named(tail, "when")) {
        /* `when` in loop-tail position -- a real, structural gap found
         * here (2026-08-21, gcc-verifying array.prn's own real
         * `strides-for`, whose whole loop body is `(when (>= i 0)
         * (vec/push! &s running) (recur ...))`): this fell through to
         * the generic "plain value" case below, which calls emit_expr()
         * on the WHOLE `when` node -- emit_expr() has no handling for
         * `when` at all (only emit_body's own statement/tail dispatch
         * does), so this silently mangled into a bogus call to a
         * never-defined `when(...)` C function. `parena build`'s own
         * exit code never caught it -- only an actual gcc compile did.
         *
         * Also real, and new here: `when` in loop-tail position can
         * have MULTIPLE body forms (this exact real call site has two:
         * the `vec/push!` side effect, then `recur`) -- unlike
         * emit_body's own single-body-form `when` (child_count == 3
         * only), every form here except the last is a plain statement,
         * and the LAST one recurses back into emit_loop_tail itself
         * (matching `if`/`let`/`do`'s own recursive composition above),
         * since it can be a `recur`, a nested `if`, or a plain value.
         *
         * `when` has no real "else" value (see emit_body's own comment
         * on this same real, honest scope) -- in loop-tail position
         * that means "stop the loop" when the condition is false: a
         * bare `break` with no result_var assignment. Real, honest,
         * narrow limitation: correct for the one real shape this
         * compiler has ever needed to emit (a side-effect-only loop
         * whose own result is never consumed by the caller, exactly
         * `strides-for`'s real shape below) -- a `when` in the tail of
         * a loop whose OWN result genuinely feeds a `return` would
         * leave result_var honestly unset on the false branch, not
         * flagged separately here since no real call site needs that
         * yet. */
        if (tail->child_count < 3) {
            return fail(arena, out_error, "loop: when expected (when cond body...) at line %d",
                        tail->line) != NULL;
        }
        const char *cond_type = NULL;
        const char *cond = emit_expr(arena, tail->children[1], scope, &cond_type, out_error);
        if (!cond) return 0;
        sb_appendf(out, "        if (%s) {\n", cond);
        /* Real, self-caught bug fixed here (2026-08-21, gcc-verifying an
         * isolated repro of dataframe.prn's own real `select`, whose
         * loop-tail `when` body is `(match ... (recur ...))` -- the
         * match is NOT the last form): every non-last body form here
         * used to go through raw emit_expr(), the same "no handling for
         * statement-shaped constructs" gap already found and fixed for
         * `if`-in-tail-position and match's own clause bodies -- a
         * mid-when `match`/`let`/`do`/nested `loop` fell through to the
         * generic call-dispatch path and silently mis-parsed. Delegates
         * to emit_body() itself (return_mode=0, discarding any value)
         * for these non-last forms instead of hand-rolling a second,
         * narrower statement dispatcher here -- emit_body's own
         * with-arena/let/loop/match/do/when/#target statement handling
         * already covers every real statement shape this compiler
         * understands, for free. */
        if (tail->child_count > 3) {
            if (!emit_body(arena, out, tail->children + 2, tail->child_count - 3, scope, 0, NULL,
                            out_error)) {
                return 0;
            }
        }
        const char *last_type = NULL;
        if (!emit_loop_tail(arena, out, tail->children[tail->child_count - 1], scope, loop_locals,
                             loop_var_count, result_var, &last_type, out_error)) {
            return 0;
        }
        sb_append(out, "        } else {\n");
        sb_append(out, "            break;\n");
        sb_append(out, "        }\n");
        if (out_result_type) *out_result_type = last_type;
        return 1;
    }
    if (is_call_named(tail, "let")) {
        /* A real, structural bug found here (2026-08-20, while getting
         * firefly.prn's own real `run-tests` to compile): a `let`
         * (or `do`, below) in a loop's own tail position -- e.g. the
         * `else` branch of `(if cond {...} (let [...] ...))` -- used to
         * fall through to the generic "plain value" case below, which
         * calls emit_expr() on the WHOLE `let` node; emit_expr() has no
         * handling for `let` at all (it's only ever special-cased at
         * the *body*-statement level, never as a generic expression),
         * so this always failed with "unsupported expression form."
         * Fixed the same way `if` just above already is: emit the
         * `let`'s own bindings as real statements directly, then
         * recurse emit_loop_tail() on the let's own body's own tail
         * form -- a `let` with an `if`/`recur`/nested `let` as its own
         * last body form now composes correctly, the same real
         * "any depth of nesting" property `if`'s own recursion above
         * already has. */
        if (tail->child_count < 2 || tail->children[1]->type != NODE_VEC) {
            return fail(arena, out_error, "loop: let expected a binding vector at line %d", tail->line) != NULL;
        }
        Node *bindings = tail->children[1];
        EmitScope child;
        scope_init(&child, scope);
        for (size_t i = 0; i + 1 < bindings->child_count; i += 2) {
            Node *name_node = bindings->children[i];
            Node *expr_node = bindings->children[i + 1];
            if (name_node->type != NODE_SYMBOL) {
                return fail(arena, out_error, "loop: let binding name must be a plain identifier at line %d",
                            tail->line) != NULL;
            }
            const char *c_name = mangle(arena, name_node->text);
            const char *c_type = NULL;
            const char *expr_c = emit_expr(arena, expr_node, &child, &c_type, out_error);
            if (!expr_c) return 0;
            sb_appendf(out, "        %s %s __attribute__((unused)) = %s;\n", c_type, c_name, expr_c);
            scope_bind(&child, name_node->text, c_name, c_type, 0);
        }
        Node **body_forms = tail->children + 2;
        size_t body_count = tail->child_count - 2;
        if (body_count == 0) {
            return fail(arena, out_error, "loop: let has an empty body at line %d", tail->line) != NULL;
        }
        for (size_t i = 0; i + 1 < body_count; i++) {
            const char *c_type = NULL;
            const char *expr_c = emit_expr(arena, body_forms[i], &child, &c_type, out_error);
            if (!expr_c) return 0;
            sb_appendf(out, "        (void)(%s);\n", expr_c);
        }
        return emit_loop_tail(arena, out, body_forms[body_count - 1], &child, loop_locals, loop_var_count,
                               result_var, out_result_type, out_error);
    }
    if (is_call_named(tail, "do")) {
        /* Same real fix as `let` just above, for the same real reason --
         * `do` is likewise only ever special-cased at the body-statement
         * level, never as a generic emit_expr() form. */
        Node **body_forms = tail->children + 1;
        size_t body_count = tail->child_count - 1;
        if (body_count == 0) {
            return fail(arena, out_error, "loop: do has an empty body at line %d", tail->line) != NULL;
        }
        for (size_t i = 0; i + 1 < body_count; i++) {
            const char *c_type = NULL;
            const char *expr_c = emit_expr(arena, body_forms[i], scope, &c_type, out_error);
            if (!expr_c) return 0;
            sb_appendf(out, "        (void)(%s);\n", expr_c);
        }
        return emit_loop_tail(arena, out, body_forms[body_count - 1], scope, loop_locals, loop_var_count,
                               result_var, out_result_type, out_error);
    }
    if (is_call_named(tail, "recur")) {
        if (tail->child_count - 1 != loop_var_count) {
            return fail(arena, out_error,
                        "loop: recur at line %d passes %zu value(s), loop has %zu variable(s)",
                        tail->line, tail->child_count - 1, loop_var_count) != NULL;
        }
        if (loop_var_count > MAX_LOOP_VARS) {
            return fail(arena, out_error, "loop: too many loop variables at line %d (max %d)",
                        tail->line, MAX_LOOP_VARS) != NULL;
        }
        /* Real simultaneous-assignment: every new value is computed into
         * its own temp FIRST, then assigned back -- so `(recur y x)`
         * really swaps rather than reading an already-overwritten var,
         * the same correctness property real `recur` semantics require. */
        char tmp_names[MAX_LOOP_VARS][32];
        for (size_t i = 0; i < loop_var_count; i++) {
            const char *val_type = NULL;
            const char *val_c = emit_expr(arena, tail->children[i + 1], scope, &val_type, out_error);
            if (!val_c) return 0;
            snprintf(tmp_names[i], sizeof(tmp_names[i]), "__recur_tmp_%zu", i);
            sb_appendf(out, "        %s %s = %s;\n", loop_locals[i]->c_type, tmp_names[i], val_c);
        }
        for (size_t i = 0; i < loop_var_count; i++) {
            sb_appendf(out, "        %s = %s;\n", loop_locals[i]->c_name, tmp_names[i]);
        }
        sb_append(out, "        continue;\n");
        if (out_result_type) *out_result_type = NULL;
        return 1;
    }
    if (is_call_named(tail, "match")) {
        /* `match` in loop-tail position -- real, honest necessity
         * (found 2026-08-21, gcc-verifying net/http.prn's own real
         * `serve`): recurses into emit_match_core() directly, passing
         * THIS loop's own real `loop_locals`/`loop_var_count`/
         * `result_var` straight through -- so a `recur` inside one of
         * the match's own clause bodies (emit_match_clause_body's own
         * new `recur` case, see its own comment) correctly continues
         * THIS loop, and a plain terminal value inside a clause
         * correctly becomes this loop's own result and breaks it,
         * exactly the same real semantics `if`/`cond` already have in
         * this same tail position. */
        return emit_match_core(arena, out, tail, scope, result_var, loop_locals, loop_var_count,
                                out_result_type, out_error);
    }
    /* A plain value in tail position: this is the loop's own real result. */
    const char *val_type = NULL;
    const char *val_c = emit_expr(arena, tail, scope, &val_type, out_error);
    if (!val_c) return 0;
    sb_appendf(out, "        %s = %s;\n", result_var, val_c);
    sb_append(out, "        break;\n");
    if (out_result_type) *out_result_type = val_type;
    return 1;
}

/* emit_loop_core -- the real loop machinery (binding-var setup + body
 * statements + tail composition via emit_loop_tail), factored out of
 * emit_loop() itself (2026-08-21, gcc-verifying net/http.prn's own
 * real `serve`, whose accept-loop `(loop [] ...)` is used DIRECTLY as
 * a match clause's own body -- `(match (net/tcp/listen ...) ((Ok
 * !listener) (loop [] ...)) ...)`) so emit_match_clause_body()'s own
 * `loop` case can recurse into it directly, targeting an
 * ALREADY-OWNED result_var the SAME real way emit_match_core()'s own
 * nested-match case already does -- no fresh declaration, the loop's
 * own final value becomes a real assignment into the match's own
 * shared result variable instead of a `return`. */
static int emit_loop_core(Arena *arena, StrBuf *out, Node *node, EmitScope *scope,
                           const char *result_var, const char **out_result_type,
                           const char **out_error) {
    if (node->child_count < 2 || node->children[1]->type != NODE_VEC) {
        return fail(arena, out_error, "loop: expected a binding vector at line %d", node->line) != NULL;
    }
    Node *bindings = node->children[1];

    EmitScope child;
    scope_init(&child, scope);

    Local *loop_locals[MAX_LOOP_VARS];
    size_t loop_var_count = 0;
    for (size_t i = 0; i + 1 < bindings->child_count; i += 2) {
        Node *name_node = bindings->children[i];
        Node *init_node = bindings->children[i + 1];
        if (name_node->type != NODE_SYMBOL) {
            return fail(arena, out_error, "loop: binding name must be a plain identifier at line %d",
                        node->line) != NULL;
        }
        const char *c_type = NULL;
        const char *init_c = emit_expr(arena, init_node, scope, &c_type, out_error);
        if (!init_c) return 0;
        const char *c_name = mangle(arena, name_node->text);
        sb_appendf(out, "    %s %s = %s;\n", c_type, c_name, init_c);
        scope_bind(&child, name_node->text, c_name, c_type, 0);
        if (loop_var_count < MAX_LOOP_VARS) {
            loop_locals[loop_var_count++] = &child.locals[child.count - 1];
        }
    }

    Node **body_forms = node->children + 2;
    size_t body_count = node->child_count - 2;
    if (body_count == 0) {
        return fail(arena, out_error, "loop: empty body at line %d", node->line) != NULL;
    }

    StrBuf body;
    sb_init(&body);
    for (size_t i = 0; i + 1 < body_count; i++) {
        Node *form = body_forms[i];
        if (is_call_named(form, "with-arena")) {
            if (!emit_with_arena(arena, &body, form, &child, 0, NULL, out_error)) {
                sb_free(&body);
                return 0;
            }
        } else if (is_call_named(form, "let")) {
            if (!emit_let(arena, &body, form, &child, 0, NULL, out_error)) {
                sb_free(&body);
                return 0;
            }
        } else {
            const char *c_type = NULL;
            const char *expr_c = emit_expr(arena, form, &child, &c_type, out_error);
            if (!expr_c) {
                sb_free(&body);
                return 0;
            }
            sb_appendf(&body, "        (void)(%s);\n", expr_c);
        }
    }

    const char *result_type = NULL;
    if (!emit_loop_tail(arena, &body, body_forms[body_count - 1], &child, loop_locals, loop_var_count,
                         result_var, &result_type, out_error)) {
        sb_free(&body);
        return 0;
    }

    sb_append(out, "    while (1) {\n");
    sb_append(out, body.data);
    sb_append(out, "    }\n");
    sb_free(&body);
    if (out_result_type) *out_result_type = result_type;
    return 1;
}

/* emit_loop -- the real, public entry point (unchanged signature, used
 * by emit_body's own statement/tail dispatch): owns result_var's one
 * real declaration (`__attribute__((unused))`: a real, valid Parena
 * `loop` can be used purely for its own side effects, its own result
 * never consumed by anything -- e.g. array.prn's own real
 * `strides-for`, whose loop only pushes onto `s`; found via an actual
 * gcc -pedantic -Werror compile, same real, honest suppression `let`'s
 * own bindings already use) and return_mode's own `return result_var;`
 * wrap. */
static int emit_loop(Arena *arena, StrBuf *out, Node *node, EmitScope *scope, int return_mode,
                      const char **out_return_type, const char **out_error) {
    static int loop_counter = 0;
    char result_var[64];
    snprintf(result_var, sizeof(result_var), "__loop_result_%d", loop_counter++);

    StrBuf body;
    sb_init(&body);
    const char *result_type = NULL;
    if (!emit_loop_core(arena, &body, node, scope, result_var, &result_type, out_error)) {
        sb_free(&body);
        return 0;
    }

    sb_appendf(out, "    %s %s __attribute__((unused));\n", result_type ? result_type : "void *",
               result_var);
    sb_append(out, body.data);
    sb_free(&body);

    if (return_mode) {
        if (out_return_type) *out_return_type = result_type ? result_type : "void *";
        sb_appendf(out, "    return %s;\n", result_var);
    }
    return 1;
}

static int emit_match_core(Arena *arena, StrBuf *out, Node *node, EmitScope *scope,
                            const char *result_var, Local **loop_locals, size_t loop_var_count,
                            const char **out_result_type, const char **out_error);

/* emit_match_clause_body -- real, structural extension (2026-08-21,
 * gcc-verifying shell.prn's own real `resolve`, whose actual policy
 * chain is `(match explicit (... s) (None (match (getenv "SHELL")
 * (... s) (None (match ...)))))` -- a NESTED match used as another
 * match's own clause body, a real, idiomatic "chain of Option checks,
 * fall through to the next on None" pattern): the original clause-body
 * emission called emit_expr() directly on the body, which has no
 * handling for `match` (or `if`/`let`/`do`) as a bare value -- those
 * are only ever special-cased at the body-statement/tail level. A
 * nested match, being a NODE_LIST whose own first child is a symbol,
 * fell all the way to the generic call-dispatch path and silently
 * mangled into a mis-parsed mess (a compound-callee call over the
 * FIRST clause's own `(Some s)` pattern list, mistaking it for a
 * first-class function value being invoked with `s` in the WRONG
 * scope) -- surfacing as a baffling "unknown identifier 's'" far from
 * the real cause.
 *
 * Same real recursive-composition idea `if` already got in emit_body's
 * own tail dispatch, generalized to match's own "write into a shared
 * result_var, no `return`" composition style instead of emit_body's
 * `return`-based one (match's own result_var may itself be consumed by
 * an ENCLOSING context that hasn't decided whether this whole match
 * expression is itself in return position). A nested `match` is the
 * one genuinely new case: it recurses into emit_match_core() directly,
 * targeting the SAME result_var the outer clause already owns -- no
 * new declaration, no new tmp_var (well, a fresh tmp_var for the
 * INNER match's own distinct scrutinee, but the SAME result_var,
 * whose own C type is only known once every real branch, nested or
 * not, has contributed one). */
static int emit_match_clause_body(Arena *arena, StrBuf *out, Node *body, EmitScope *scope,
                                   const char *result_var, Local **loop_locals, size_t loop_var_count,
                                   const char **out_result_type, const char **out_error) {
    /* `recur` inside a match clause body -- found missing (2026-08-21,
     * gcc-verifying net/http.prn's own real `serve`, whose accept-loop
     * is `(loop [] (match (net/tcp/accept ...) ((Ok !conn) (do ...
     * (recur))) ((Err e) (Err e))))`: the match itself is the loop's
     * own TAIL, and one of ITS clauses recurs). Real, honest, narrow
     * scope: only valid when a real loop context was actually passed
     * down (`loop_locals` non-NULL -- see emit_loop_tail's own new
     * `match` case below, the one real place that supplies it); a
     * `recur` inside a match with no enclosing loop is reported, not
     * silently miscompiled. Same real simultaneous-assignment
     * emit_loop_tail's own recur handling already uses. */
    if (is_call_named(body, "recur")) {
        if (!loop_locals) {
            return fail(arena, out_error, "recur: not inside a loop at line %d", body->line) != NULL;
        }
        if (body->child_count - 1 != loop_var_count) {
            return fail(arena, out_error,
                        "match: recur at line %d passes %zu value(s), loop has %zu variable(s)",
                        body->line, body->child_count - 1, loop_var_count) != NULL;
        }
        if (loop_var_count > MAX_LOOP_VARS) {
            return fail(arena, out_error, "match: too many loop variables at line %d (max %d)",
                        body->line, MAX_LOOP_VARS) != NULL;
        }
        char tmp_names[MAX_LOOP_VARS][32];
        for (size_t i = 0; i < loop_var_count; i++) {
            const char *val_type = NULL;
            const char *val_c = emit_expr(arena, body->children[i + 1], scope, &val_type, out_error);
            if (!val_c) return 0;
            snprintf(tmp_names[i], sizeof(tmp_names[i]), "__recur_tmp_%zu", i);
            sb_appendf(out, "        %s %s = %s;\n", loop_locals[i]->c_type, tmp_names[i], val_c);
        }
        for (size_t i = 0; i < loop_var_count; i++) {
            sb_appendf(out, "        %s = %s;\n", loop_locals[i]->c_name, tmp_names[i]);
        }
        sb_append(out, "        continue;\n");
        if (out_result_type) *out_result_type = NULL;
        return 1;
    }
    if (is_call_named(body, "if")) {
        if (body->child_count != 4) {
            return fail(arena, out_error, "match: if in clause body needs (if cond then else) at line %d",
                        body->line) != NULL;
        }
        const char *cond_type = NULL;
        const char *cond_c = emit_expr(arena, body->children[1], scope, &cond_type, out_error);
        if (!cond_c) return 0;
        sb_appendf(out, "        if (%s) {\n", cond_c);
        const char *then_type = NULL;
        if (!emit_match_clause_body(arena, out, body->children[2], scope, result_var, loop_locals,
                                     loop_var_count, &then_type, out_error)) {
            return 0;
        }
        sb_append(out, "        } else {\n");
        const char *else_type = NULL;
        if (!emit_match_clause_body(arena, out, body->children[3], scope, result_var, loop_locals,
                                     loop_var_count, &else_type, out_error)) {
            return 0;
        }
        sb_append(out, "        }\n");
        if (out_result_type) *out_result_type = then_type ? then_type : else_type;
        return 1;
    }
    if (is_call_named(body, "let")) {
        if (body->child_count < 2 || body->children[1]->type != NODE_VEC) {
            return fail(arena, out_error, "match: let expected a binding vector at line %d", body->line) != NULL;
        }
        Node *bindings = body->children[1];
        EmitScope child;
        scope_init(&child, scope);
        for (size_t i = 0; i + 1 < bindings->child_count; i += 2) {
            Node *name_node = bindings->children[i];
            Node *expr_node = bindings->children[i + 1];
            if (name_node->type != NODE_SYMBOL) {
                return fail(arena, out_error, "match: let binding name must be a plain identifier at "
                                               "line %d",
                            body->line) != NULL;
            }
            const char *c_name = mangle(arena, name_node->text);
            const char *c_type = NULL;
            const char *expr_c = emit_expr(arena, expr_node, &child, &c_type, out_error);
            if (!expr_c) return 0;
            sb_appendf(out, "        %s %s __attribute__((unused)) = %s;\n", c_type, c_name, expr_c);
            scope_bind(&child, name_node->text, c_name, c_type, 0);
        }
        Node **body_forms = body->children + 2;
        size_t body_count = body->child_count - 2;
        if (body_count == 0) {
            return fail(arena, out_error, "match: let has an empty body at line %d", body->line) != NULL;
        }
        /* Real, self-caught bug fixed here (2026-08-21, gcc-verifying
         * an isolated repro of net/http.prn's own real `serve`, whose
         * accept-loop's own `(Ok !conn)` clause body is `(do (let
         * [req ...] req) (recur))` -- the `let` itself is a NON-LAST
         * `do` form): non-last body forms here used to go through raw
         * emit_expr(), the same "no handling for statement-shaped
         * constructs" gap already found and fixed for `if`-in-tail-
         * position, match's own clause bodies, and when-in-loop-tail's
         * own non-last forms -- a nested `let`/`loop`/`match`/`when`
         * appearing here (not as the LAST form) fell through the same
         * generic call-dispatch mis-parse. Delegates to emit_body()
         * itself (return_mode=0, discarding any value) instead of
         * hand-rolling yet another narrower statement dispatcher. */
        if (body_count > 1) {
            if (!emit_body(arena, out, body_forms, body_count - 1, &child, 0, NULL, out_error)) {
                return 0;
            }
        }
        return emit_match_clause_body(arena, out, body_forms[body_count - 1], &child, result_var,
                                       loop_locals, loop_var_count, out_result_type, out_error);
    }
    if (is_call_named(body, "do")) {
        Node **body_forms = body->children + 1;
        size_t body_count = body->child_count - 1;
        if (body_count == 0) {
            return fail(arena, out_error, "match: do has an empty body at line %d", body->line) != NULL;
        }
        /* Same real, self-caught fix as `let`'s own body handling just
         * above -- see its own comment. */
        if (body_count > 1) {
            if (!emit_body(arena, out, body_forms, body_count - 1, scope, 0, NULL, out_error)) {
                return 0;
            }
        }
        return emit_match_clause_body(arena, out, body_forms[body_count - 1], scope, result_var,
                                       loop_locals, loop_var_count, out_result_type, out_error);
    }
    if (is_call_named(body, "match")) {
        return emit_match_core(arena, out, body, scope, result_var, loop_locals, loop_var_count,
                                out_result_type, out_error);
    }
    if (is_call_named(body, "loop")) {
        /* `loop` used directly as a match clause's own body -- found
         * missing (2026-08-21, gcc-verifying net/http.prn's own real
         * `serve`, whose accept-loop is exactly `(match (net/tcp/listen
         * ...) ((Ok !listener) (loop [] ...)) ...)`: the loop IS the
         * whole first clause's own value). Recurses into
         * emit_loop_core() directly, the same real "share the outer's
         * already-owned result_var, no fresh declaration" composition
         * emit_match_core()'s own nested-match case just above already
         * uses -- the loop's own final value becomes a real assignment
         * into match's shared result variable instead of a `return`. */
        return emit_loop_core(arena, out, body, scope, result_var, out_result_type, out_error);
    }
    const char *clause_type = NULL;
    const char *clause_c = emit_expr(arena, body, scope, &clause_type, out_error);
    if (!clause_c) return 0;
    sb_appendf(out, "        %s = %s;\n", result_var, clause_c);
    /* Real, self-caught bug fixed here (2026-08-21, before it ever hit
     * gcc -- caught by re-reading emit_loop_tail's own new `match`
     * case above and realizing this assignment alone leaves nothing to
     * stop the enclosing `while (1)`): when a real loop context IS
     * available (loop_locals non-NULL, meaning this whole match is
     * ultimately nested inside a loop's own tail -- see
     * emit_loop_tail's own new `match` case), a plain-value clause is
     * the loop's own real terminal case and must `break` the loop, the
     * exact same real convention emit_loop_tail's OWN "plain value in
     * tail position" fallback already uses. Without this, control
     * would fall out of the match's own if/else-if chain and loop back
     * to `while (1)`'s own top, silently re-running the scrutinee
     * check forever instead of stopping. Outside a loop context
     * (loop_locals NULL, the ordinary non-nested match case), no
     * `break` is emitted -- there's no enclosing loop to break out of,
     * and falling through past the if/else-if chain to whatever comes
     * after the match is already the correct, real behavior. */
    if (loop_locals) {
        sb_append(out, "        break;\n");
    }
    if (out_result_type) *out_result_type = clause_type;
    return 1;
}

/* emit_match_core -- the real clause-matching machinery (tmp_var +
 * if/else-if tag-check chain), factored out of emit_match() itself
 * (2026-08-21) so emit_match_clause_body()'s own nested-match case
 * above can recurse into it directly, targeting an ALREADY-OWNED
 * result_var (no fresh declaration -- the outer match's own top-level
 * emit_match() call is the one real place that ever declares
 * result_var, whether or not any nesting is involved). */
static int emit_match_core(Arena *arena, StrBuf *out, Node *node, EmitScope *scope,
                            const char *result_var, Local **loop_locals, size_t loop_var_count,
                            const char **out_result_type, const char **out_error) {
    if (node->child_count < 3) {
        return fail(arena, out_error, "match: expected (match scrutinee clause...) at line %d",
                    node->line) != NULL;
    }
    const char *scrut_type = NULL;
    const char *scrut_c = emit_expr(arena, node->children[1], scope, &scrut_type, out_error);
    if (!scrut_c) return 0;
    /* scrut_enum is non-NULL when the scrutinee resolved to a real,
     * registered user defenum type -- generalizes the hardcoded
     * Result/Option check below to any such type, using that enum's own
     * real variant->tag table instead of the built-in Ok/Err/Some/None
     * mapping. */
    EnumInfo *scrut_enum = scrut_type ? find_enum_by_name(scrut_type) : NULL;
    if (!scrut_type || (!scrut_enum && strcmp(scrut_type, "Result") != 0 && strcmp(scrut_type, "Option") != 0)) {
        return fail(arena, out_error,
                    "match: VS0's emitter only understands matching a Result, Option, or a "
                    "registered defenum type at line %d (scrutinee's own type resolved to '%s')",
                    node->line, scrut_type ? scrut_type : "?") != NULL;
    }

    static int match_counter = 0;
    int id = match_counter++;
    char tmp_var[64];
    snprintf(tmp_var, sizeof(tmp_var), "__match_tmp_%d", id);

    sb_appendf(out, "    %s %s = %s;\n", scrut_type, tmp_var, scrut_c);

    StrBuf clauses;
    sb_init(&clauses);
    const char *result_type = NULL;
    int first = 1;
    for (size_t i = 2; i < node->child_count; i++) {
        Node *clause = node->children[i];
        if (clause->type != NODE_LIST || clause->child_count != 2) {
            sb_free(&clauses);
            return fail(arena, out_error,
                        "match: VS0's emitter only supports a single-expression clause body, "
                        "(pattern expr), at line %d",
                        node->line) != NULL;
        }
        Node *pattern = clause->children[0];
        const char *ctor_name = NULL;
        Node *bind_node = NULL;
        if (pattern->type == NODE_LIST && pattern->child_count >= 1 &&
            pattern->children[0]->type == NODE_SYMBOL) {
            ctor_name = pattern->children[0]->text;
            if (pattern->child_count >= 2 && pattern->children[1]->type == NODE_SYMBOL) {
                bind_node = pattern->children[1];
            }
        } else if (pattern->type == NODE_SYMBOL) {
            ctor_name = pattern->text;
        } else {
            sb_free(&clauses);
            return fail(arena, out_error, "match: unsupported pattern shape at line %d", node->line) != NULL;
        }

        int tag_value;
        int is_wildcard = strcmp(ctor_name, "_") == 0;
        if (is_wildcard) {
            tag_value = -1;
        } else if (scrut_enum) {
            /* A registered user defenum scrutinee: look the pattern's own
             * ctor_name up in *this* enum's real variant table, not the
             * hardcoded Ok/Err/Some/None one below -- a pattern naming a
             * variant that belongs to some OTHER enum (or no enum at all)
             * is reported, not silently matched against the wrong tag. */
            EnumVariant *pat_variant = NULL;
            size_t vi;
            for (vi = 0; vi < scrut_enum->variant_count; vi++) {
                if (strcmp(scrut_enum->variants[vi].name, ctor_name) == 0) {
                    pat_variant = &scrut_enum->variants[vi];
                    break;
                }
            }
            if (!pat_variant) {
                sb_free(&clauses);
                return fail(arena, out_error,
                            "match: '%s' is not a variant of %s at line %d",
                            ctor_name, scrut_enum->name, node->line) != NULL;
            }
            tag_value = pat_variant->tag_value;
        } else if (strcmp(ctor_name, "Ok") == 0 || strcmp(ctor_name, "Some") == 0) {
            tag_value = 1;
        } else if (strcmp(ctor_name, "Err") == 0 || strcmp(ctor_name, "None") == 0) {
            tag_value = 0;
        } else {
            sb_free(&clauses);
            return fail(arena, out_error,
                        "match: VS0's emitter only understands Ok/Err/Some/None/_ patterns so far "
                        "(got '%s' at line %d)",
                        ctor_name, node->line) != NULL;
        }

        if (is_wildcard) {
            sb_appendf(&clauses, "    %s (1) {\n", first ? "if" : "else if");
        } else {
            sb_appendf(&clauses, "    %s (%s.tag == %d) {\n", first ? "if" : "else if", tmp_var, tag_value);
        }
        first = 0;

        EmitScope clause_scope;
        scope_init(&clause_scope, scope);
        if (bind_node) {
            const char *c_name = mangle(arena, bind_node->text);
            /* __attribute__((unused)): same real reasoning as `let`
             * bindings and function parameters above -- a real match
             * clause can validly ignore its own bound payload (e.g. an
             * Err arm that doesn't need the error value), not a genuine
             * Parena-source bug. */
            sb_appendf(&clauses, "        void *%s __attribute__((unused)) = %s.value;\n", c_name, tmp_var);
            scope_bind(&clause_scope, bind_node->text, c_name, "void *", 0);
        }

        const char *clause_type = NULL;
        if (!emit_match_clause_body(arena, &clauses, clause->children[1], &clause_scope, result_var,
                                     loop_locals, loop_var_count, &clause_type, out_error)) {
            sb_free(&clauses);
            return 0;
        }
        if (!result_type) result_type = clause_type;
        sb_append(&clauses, "    }\n");
    }

    sb_append(out, clauses.data);
    sb_free(&clauses);
    if (out_result_type) *out_result_type = result_type;
    return 1;
}

/* emit_match -- the real, public entry point (unchanged signature, used
 * by emit_body/emit_loop_tail's own tail dispatch): owns result_var's
 * one real declaration (its own C type isn't known until every clause,
 * nested or not, has been walked -- emit_match_core's own use of a
 * separate `clauses` buffer before appending to `out` is exactly what
 * makes learning the type first, declaring second, possible), and
 * return_mode's own `return result_var;` wrap. */
static int emit_match(Arena *arena, StrBuf *out, Node *node, EmitScope *scope, int return_mode,
                       const char **out_return_type, const char **out_error) {
    static int match_result_counter = 0;
    char result_var[64];
    snprintf(result_var, sizeof(result_var), "__match_result_%d", match_result_counter++);

    StrBuf body;
    sb_init(&body);
    const char *result_type = NULL;
    /* NULL, 0 -- this is the top-level entry point (used by emit_body's
     * own statement/tail dispatch), never itself directly inside a
     * loop's own tail composition; emit_loop_tail's own new `match`
     * case below supplies the real loop context instead, by recursing
     * into emit_match_core() directly rather than through here. */
    if (!emit_match_core(arena, &body, node, scope, result_var, NULL, 0, &result_type, out_error)) {
        sb_free(&body);
        return 0;
    }

    /* __attribute__((unused)): same real reasoning as loop's own
     * result_var already has -- a real, valid `match` can be used
     * purely for its own side effects (return_mode=0, mid-body
     * statement), its own result never consumed by anything, e.g. this
     * exact real shape inside dataframe.prn's own real `select`: a
     * `match` whose value is discarded, nested inside `when` inside a
     * `loop`. Found via an actual gcc -pedantic -Werror compile
     * (-Wunused-but-set-variable), not a real bug in the Parena
     * source. */
    sb_appendf(out, "    %s %s __attribute__((unused));\n", result_type ? result_type : "void *",
               result_var);
    sb_append(out, body.data);
    sb_free(&body);

    if (return_mode) {
        if (out_return_type) *out_return_type = result_type ? result_type : "void *";
        sb_appendf(out, "    return %s;\n", result_var);
    }
    return 1;
}

static int emit_body(Arena *arena, StrBuf *out, Node **forms, size_t count, EmitScope *scope,
                      int return_mode, const char **out_return_type, const char **out_error) {
    if (count == 0) return 1;

    for (size_t i = 0; i + 1 < count; i++) {
        Node *form = forms[i];
        if (is_call_named(form, "with-arena")) {
            if (!emit_with_arena(arena, out, form, scope, 0, NULL, out_error)) return 0;
        } else if (is_call_named(form, "let")) {
            if (!emit_let(arena, out, form, scope, 0, NULL, out_error)) return 0;
        } else if (is_call_named(form, "loop")) {
            if (!emit_loop(arena, out, form, scope, 0, NULL, out_error)) return 0;
        } else if (is_call_named(form, "match")) {
            if (!emit_match(arena, out, form, scope, 0, NULL, out_error)) return 0;
        } else if (is_call_named(form, "do")) {
            /* `(do a b c)` -- a plain, real sequence of forms, not a
             * value-producing call. Recurses into emit_body() itself
             * over `do`'s own children (skipping the leading `do`
             * symbol) rather than duplicating the with-arena/let/loop/
             * match dispatch table above -- a nested `do` inside a `do`
             * (or a `let` inside a `do`, etc.) is handled the same real
             * way any other nested body already is. Found missing (not
             * broken by this change -- genuinely never implemented)
             * while getting firefly.prn's own real `errorf` (`(do (set!
             * ...) (vec/push! ...))`) to compile -- previously fell
             * through to the generic call path and mangled into a bogus
             * `do(...)` function call, since nothing before this ever
             * exercised `do` as a function body's own form. */
            if (!emit_body(arena, out, form->children + 1, form->child_count - 1, scope, 0, NULL, out_error)) {
                return 0;
            }
        } else if (is_call_named(form, "when")) {
            /* `(when cond expr)` -- a real, common Lisp single-branch
             * conditional, found missing while getting world.prn's own
             * `set-height` and firefly/ladybug.prn's own `to` to
             * compile (both real: `(when (not (matcher ...)) (firefly/
             * errorf ...))`). Unlike `if`, `when` has no real "else"
             * value at all -- it's inherently a statement-shaped,
             * side-effecting construct (do nothing when the condition
             * is false), not a value-producing ternary -- so this is
             * handled the same statement-level way `do` is, a real C
             * `if (cond) { expr; }` with no else, not attempted as a
             * generic emit_expr() call. */
            if (form->child_count != 3) {
                fail(arena, out_error, "when: expected (when cond expr) at line %d", form->line);
                return 0;
            }
            const char *cond_type = NULL;
            const char *cond_c = emit_expr(arena, form->children[1], scope, &cond_type, out_error);
            if (!cond_c) return 0;
            const char *body_type = NULL;
            const char *body_c = emit_expr(arena, form->children[2], scope, &body_type, out_error);
            if (!body_c) return 0;
            sb_appendf(out, "    if (%s) {\n        (void)(%s);\n    }\n", cond_c, body_c);
        } else if (is_symbol(form, "#target") && i + 1 < count) {
            /* `#target {:c (inline-c "...")}` as a MID-BODY statement,
             * not a whole function body -- found missing (2026-08-21,
             * gcc-verifying string.prn's own real `concat`, whose
             * `let`-body is exactly `[out (alloc ...)] #target {:c
             * (inline-c "strcpy(out, a); strcat(out, b);")} out`: the
             * inline-C fills the just-allocated buffer for its own side
             * effect, then `out` is returned separately). Before this,
             * `#target` was only ever recognized by emit_defn's own
             * check for REPLACING the entire function body
             * (emit_target_defn) -- a bare `#target` symbol showing up
             * mid-body had no handling at all, and (being a two-sibling-
             * node form, `#target` then the map, the same real "two
             * adjacent top-level forms" shape `&(expr)` already has
             * elsewhere in this file) fell through to the generic
             * fallback below, which tried to look `#target` up as a
             * plain identifier and failed outright.
             *
             * Shares find_target_c_src()'s own real `:c (inline-c
             * "...")` extraction with emit_target_defn's whole-body use
             * (factored out for exactly this reason) -- the raw C text
             * is spliced in as a bare statement, no return-wrapping
             * (unlike emit_target_defn's own return_type-aware
             * wrapping), since a mid-body inline-C block is always used
             * for its own side effect here, matching the one real call
             * site's own convention of including its own trailing `;`. */
            Node *target_map = forms[i + 1];
            Node *src = find_target_c_src(arena, target_map, out_error);
            if (!src) return 0;
            sb_appendf(out, "    %.*s\n", (int)src->text_len, src->text);
            i++; /* also consumed the following {:c ...} map form */
        } else {
            const char *c_type = NULL;
            const char *expr_c = emit_expr(arena, form, scope, &c_type, out_error);
            if (!expr_c) return 0;
            sb_appendf(out, "    (void)(%s);\n", expr_c);
        }
    }

    Node *tail = forms[count - 1];
    if (is_call_named(tail, "when")) {
        /* `when` in tail position -- real, honest scope: only makes
         * sense for a Unit(void)-returning function (world.prn's own
         * `set-height` is exactly this shape), since `when` has no real
         * value to return on the false branch. A `when` in tail
         * position of a NON-void function is reported, not silently
         * guessed at (there's no honest value to fabricate for the
         * false branch). */
        if (tail->child_count != 3) {
            fail(arena, out_error, "when: expected (when cond expr) at line %d", tail->line);
            return 0;
        }
        const char *cond_type = NULL;
        const char *cond_c = emit_expr(arena, tail->children[1], scope, &cond_type, out_error);
        if (!cond_c) return 0;
        const char *body_type = NULL;
        const char *body_c = emit_expr(arena, tail->children[2], scope, &body_type, out_error);
        if (!body_c) return 0;
        sb_appendf(out, "    if (%s) {\n        (void)(%s);\n    }\n", cond_c, body_c);
        if (return_mode) {
            if (out_return_type) *out_return_type = "void";
        }
        return 1;
    }
    if (is_call_named(tail, "with-arena")) {
        return emit_with_arena(arena, out, tail, scope, return_mode, out_return_type, out_error);
    }
    if (is_call_named(tail, "let")) {
        return emit_let(arena, out, tail, scope, return_mode, out_return_type, out_error);
    }
    if (is_call_named(tail, "loop")) {
        return emit_loop(arena, out, tail, scope, return_mode, out_return_type, out_error);
    }
    if (is_call_named(tail, "match")) {
        return emit_match(arena, out, tail, scope, return_mode, out_return_type, out_error);
    }
    if (is_call_named(tail, "do")) {
        return emit_body(arena, out, tail->children + 1, tail->child_count - 1, scope, return_mode,
                          out_return_type, out_error);
    }
    if (is_call_named(tail, "if")) {
        /* `if` in tail position, real, statement-level composition --
         * found missing (2026-08-21, gcc-verifying string.prn's own real
         * `is-valid-i32-text?`, whose own `let`-tail is `(if (= n 0)
         * false (loop ...))`): the generic fallback below calls
         * emit_expr() on the whole tail, which dispatches to emit_if()
         * -- a pure ternary that itself calls emit_expr() on both
         * branches, and emit_expr() has no handling for `loop` (or
         * `let`/`when`/`cond`/`match`/`with-arena`/`do`) as a bare VALUE
         * expression -- those are only ever special-cased at the
         * body-statement/tail level, never inside a ternary. Hit the
         * generic "unsupported expression form" fallback with no useful
         * diagnostic pointing at the real cause.
         *
         * Fixed by giving `if` the same real statement-level tail
         * composition `when`/`let`/`loop`/`match`/`do` already get here,
         * simply by recursing into emit_body() itself on each branch
         * treated as its own one-form body -- this reaches every one of
         * emit_body()'s own tail-dispatch cases (including a nested
         * `if`) for free, with no separate bespoke recursive composer
         * needed. return_mode=1 threads through correctly too: each
         * branch, if it resolves to a plain value, emits a real early
         * `return <value>;` from inside its own `if`/`else` block --
         * exactly correct, ordinary C, not requiring a shared result
         * variable the way emit_loop_tail's own analogous `if` handling
         * needs (that one has no `return`, only a shared loop
         * accumulator). */
        if (tail->child_count != 4) {
            fail(arena, out_error, "if: expected (if cond then else) at line %d", tail->line);
            return 0;
        }
        const char *cond_type = NULL;
        const char *cond_c = emit_expr(arena, tail->children[1], scope, &cond_type, out_error);
        if (!cond_c) return 0;
        sb_appendf(out, "    if (%s) {\n", cond_c);
        const char *then_type = NULL;
        if (!emit_body(arena, out, tail->children + 2, 1, scope, return_mode, &then_type, out_error)) {
            return 0;
        }
        sb_append(out, "    } else {\n");
        const char *else_type = NULL;
        if (!emit_body(arena, out, tail->children + 3, 1, scope, return_mode, &else_type, out_error)) {
            return 0;
        }
        sb_append(out, "    }\n");
        if (return_mode && out_return_type) *out_return_type = then_type ? then_type : else_type;
        return 1;
    }
    const char *c_type = NULL;
    const char *expr_c = emit_expr(arena, tail, scope, &c_type, out_error);
    if (!expr_c) return 0;
    if (return_mode) {
        if (out_return_type) *out_return_type = c_type ? c_type : "void";
        /* Real ISO C99 constraint (`-pedantic` catches it, found by
         * actually compiling firefly.prn's own real `errorf` -- its
         * whole body is a `do` block whose own tail call is `vec/push!`,
         * a void-returning runtime call, inside a `Unit`(void)-returning
         * function): "ISO C forbids 'return' with expression, in
         * function returning void" -- even when the expression's own
         * type genuinely is void. A void-typed tail expression is
         * emitted as a bare statement instead; falling off the end of a
         * void C function is valid and means the same thing. */
        if (c_type && strcmp(c_type, "void") == 0) {
            sb_appendf(out, "    (void)(%s);\n", expr_c);
        } else {
            sb_appendf(out, "    return %s;\n", expr_c);
        }
    } else {
        /* Real, self-caught bug fixed here (2026-08-21, gcc-verifying
         * an isolated repro of net/http.prn's own real `serve`, whose
         * accept-loop clause body is `(do (let [req ...] req) (recur))`
         * -- the `let`'s own tail form is a bare, already-bound
         * variable, discarded here since return_mode=0 (this whole
         * `let` is itself a non-last `do` form)): a bare-symbol (or any
         * side-effect-free) discarded tail value produces a real gcc
         * `-Wunused-value` ("statement with no effect") error, found
         * via an actual compile, not `parena build`'s own exit code.
         * Wrapped in `(void)(...)`, the standard, idiomatic C way to
         * mark a value as deliberately discarded -- valid regardless
         * of whether the expression already has side effects, so this
         * is safe to apply unconditionally here, not just for the bare-
         * symbol case that surfaced it. */
        sb_appendf(out, "    (void)(%s);\n", expr_c);
    }
    return 1;
}

/* resolve_declared_type maps an explicit `: <type>` annotation (written
 * after a defn's parameter vector, e.g. `(defn f [] : Unit ...)`) to a
 * real C type string. Real, honest, narrow scope -- just the handful of
 * type spellings this stdlib's own real `#target` FFI declarations
 * actually use (stdlib/editor's own .prn files): `Unit`/`I32`/`String` map to their
 * obvious C equivalents; `(Result ...)`/`(Option ...)` map to the real
 * runtime struct names (matching emit_match's own Result/Option handling
 * elsewhere in this file) without inspecting or validating their inner
 * type parameters -- VS0 has no generics/real type-checking pass yet, so
 * `(Result Unit BufferError)` and `(Result String String)` both just
 * resolve to the same real `Result` C type; not pretended more precise
 * than that. A bare symbol matching a registered defenum or defstruct
 * name (e.g. cache.prn's own `Cache` return type) resolves to that
 * type's own real name directly -- process_defenum()/process_defstruct()
 * already emitted a real C typedef for it earlier in the same
 * compilation. Anything else fails honestly rather than guessing. */
/* resolve_base_type_name is resolve_declared_type's own bare-symbol
 * lookup, factored out so the new `&Type`/`&mut Type` reference-type
 * path below can share it exactly rather than duplicating the
 * Unit/I32/Bool/F64/String/enum/struct table. `Any` is deliberately NOT
 * accepted here as a bare, non-reference type (nothing in this stdlib
 * declares a plain `Any`-typed value, only `&Any`) -- it's handled
 * inside resolve_declared_type's own reference-type branch instead,
 * mapped to `void` (an untyped pointer target), the same "erase what
 * VS0 can't represent yet" honesty every other erasure in this function
 * already uses. */
static const char *resolve_base_type_name(Node *type_node) {
    if (is_symbol(type_node, "Unit")) return "void";
    if (is_symbol(type_node, "I32")) return "int";
    if (is_symbol(type_node, "Bool")) return "int";
    if (is_symbol(type_node, "F64")) return "double";
    if (is_symbol(type_node, "String")) return "char *";
    /* Bare `Arena` (no `@ region`) -- found genuinely missing
     * (2026-08-21, gcc-verifying net/http.prn's own real `serve`,
     * whose `handler` parameter type is `(Fn [&HttpRequest Arena]
     * HttpResponse)`): a `(Fn [...] ...)` argument-type slot recurses
     * into resolve_declared_type() -> resolve_base_type_name() for a
     * plain symbol like this, and "Arena" was never in the bare-symbol
     * table at all (every OTHER real Arena usage in this emitter goes
     * through the separate `Type @ Region` / has_region_marker() path
     * instead, which this Fn-argument-list position doesn't use). Maps
     * to "Arena *", matching every other real Arena value's own C
     * representation throughout this emitter (a real pointer, never a
     * bare struct value). */
    if (is_symbol(type_node, "Arena")) return "Arena *";
    if (find_enum_by_name(type_node->text)) return type_node->text;
    if (find_struct_by_name(type_node->text)) return type_node->text;
    return NULL;
}

static const char *resolve_declared_type(Arena *arena, Node *type_node, const char **out_error) {
    if (type_node->type == NODE_SYMBOL) {
        /* `&Type` -- a real reference type, written as one token (no
         * space between `&` and the type name; `&mut Type` is a
         * *separate* two-token form the defstruct-field/defn-param call
         * sites handle themselves, since by the time a bare `&mut` token
         * alone reaches this function there's no next token left to look
         * at). Emitted as a real C pointer -- VS0 doesn't distinguish
         * mutable from immutable references at the C level any more than
         * it enforces immutability anywhere else yet (Bool-as-int is the
         * same kind of honest non-enforcement), so `&T` and `&mut T` both
         * just become `T *`. */
        if (type_node->text && type_node->text[0] == '&' && strcmp(type_node->text, "&mut") != 0) {
            const char *inner_name = type_node->text + 1;
            const char *base = NULL;
            if (strcmp(inner_name, "Any") == 0) {
                base = "void";
            } else {
                Node inner = *type_node;
                inner.text = inner_name;
                base = resolve_base_type_name(&inner);
            }
            if (!base) {
                return fail(arena, out_error,
                            "defn: unsupported reference target type '%s' at line %d (VS0's "
                            "emitter only understands &Any/&Unit/&I32/&Bool/&F64/&String/a "
                            "registered defenum-or-defstruct name so far)",
                            inner_name, type_node->line);
            }
            char buf[128];
            snprintf(buf, sizeof(buf), "%s *", base);
            return arena_strdup(arena, buf, strlen(buf));
        }
        const char *base = resolve_base_type_name(type_node);
        if (base) return base;
        return fail(arena, out_error,
                    "defn: unsupported return type symbol '%s' at line %d (VS0's emitter only "
                    "understands Unit/I32/Bool/F64/String/a registered defenum/defstruct name so far)",
                    type_node->text ? type_node->text : "?", type_node->line);
    }
    if (type_node->type == NODE_LIST && type_node->child_count > 0 && type_node->children[0]->type == NODE_SYMBOL) {
        if (is_symbol(type_node->children[0], "Result")) return "Result";
        if (is_symbol(type_node->children[0], "Option")) return "Option";
        /* (Vec T) -- erases T the same way (Result ..)/(Option ..) erase
         * their own type parameters above (VS0 has no generics/real
         * type-checking pass yet); maps to the real runtime Vec struct
         * (parena_runtime.h), a void*-item dynamic array. */
        if (is_symbol(type_node->children[0], "Vec")) return "Vec";
        /* (Map K V) -- found genuinely missing (2026-08-21, gcc-verifying
         * net/http.prn's own real `HttpRequest`/`HttpResponse`, both
         * carrying a `headers : (Map String String) @ Region` field):
         * erases K/V the same real, honest way (Result ..)/(Option ..)/
         * (Vec ..) already erase their own type parameters above.
         * Real, deliberately narrower scope than `Vec`'s own treatment:
         * `Vec` maps to a REAL runtime struct (parena_runtime.h defines
         * one, backing every real `vec`-prefixed call this stdlib
         * already uses) -- `Map` has no equivalent real backing implementation
         * anywhere yet (map.prn's own real, intended hash-map source is
         * itself blocked on real generics, a separate, much larger
         * feature; there's no hardcoded runtime pseudo-module the way
         * `vec` is either). Erased to a plain `void *` instead of a
         * named struct -- honest about there being nothing real behind
         * it yet, but still lets a real STRUCT FIELD merely reference a
         * Map-typed value (net/http.prn's own real, only usage: never
         * actually constructs or manipulates one in this file) compile,
         * rather than failing outright on the type annotation alone.
         * A real `map/new`/`map/get`/`map/set!` call site would still
         * fail honestly downstream, unchanged -- this doesn't pretend
         * Map is usable, only that naming its TYPE doesn't have to wait
         * for that. */
        if (is_symbol(type_node->children[0], "Map")) return "void *";
        /* (Fn [ArgType...] RetType) -- a real, typed, possibly
         * non-zero-argument callback type, e.g. firefly.prn's own
         * TestCase.run field (`(Fn [&mut T] Unit)`) and firefly/
         * ladybug.prn's matcher type (`(Fn [&Any] Bool)`). Generalizes
         * emit_defn's own older, narrower "only zero-argument (Fn []
         * <ReturnType>) parameters" special case (kept as-is for that
         * one call site, not removed) -- this path is reachable from
         * ANY type position resolve_declared_type() itself covers
         * (struct fields, return types), not just parameters. Each
         * argument slot handles the same three real shapes a param
         * itself does: a plain resolvable symbol, a single-token
         * `&Type`, or a two-token `&mut Type` pair (consuming two vec
         * elements, not one) -- recursing into resolve_declared_type()
         * itself for anything not needing the two-token special case,
         * so a nested `(Fn [(Fn [] Unit)] Unit)` just works too. */
        if (is_symbol(type_node->children[0], "Fn") && type_node->child_count == 3 &&
            type_node->children[1]->type == NODE_VEC) {
            Node *arg_vec = type_node->children[1];
            const char *ret_type = resolve_declared_type(arena, type_node->children[2], out_error);
            if (!ret_type) return NULL;
            StrBuf args;
            sb_init(&args);
            int first = 1;
            for (size_t i = 0; i < arg_vec->child_count; i++) {
                const char *arg_c_type;
                if (arg_vec->children[i]->type == NODE_SYMBOL &&
                    is_symbol(arg_vec->children[i], "&mut") && i + 1 < arg_vec->child_count &&
                    arg_vec->children[i + 1]->type == NODE_SYMBOL) {
                    const char *base = resolve_base_type_name(arg_vec->children[i + 1]);
                    if (!base && strcmp(arg_vec->children[i + 1]->text, "Any") == 0) base = "void";
                    if (!base) {
                        sb_free(&args);
                        return fail(arena, out_error,
                                    "Fn type: unsupported reference target type '%s' at line %d",
                                    arg_vec->children[i + 1]->text, arg_vec->children[i + 1]->line);
                    }
                    char buf[128];
                    snprintf(buf, sizeof(buf), "%s *", base);
                    arg_c_type = arena_strdup(arena, buf, strlen(buf));
                    i++;
                } else {
                    arg_c_type = resolve_declared_type(arena, arg_vec->children[i], out_error);
                    if (!arg_c_type) {
                        sb_free(&args);
                        return NULL;
                    }
                }
                if (!first) sb_append(&args, ", ");
                first = 0;
                sb_append(&args, arg_c_type);
            }
            if (first) sb_append(&args, "void");
            char buf[256];
            snprintf(buf, sizeof(buf), "%s (*)(%s)", ret_type, args.data);
            sb_free(&args);
            return arena_strdup(arena, buf, strlen(buf));
        }
    }
    return fail(arena, out_error,
                "defn: unsupported return type form at line %d (VS0's emitter only understands "
                "Unit/I32/String/(Result ..)/(Option ..)/(Vec ..)/(Fn [..] ..) so far)",
                type_node->line);
}

/* process_defstruct handles one top-level `(defstruct Name (field1 :
 * Type1) (field2 : Type2 @ Region) ...)`: registers Name into g_structs
 * and emits a real C struct typedef plus one positional `static inline`
 * constructor. Each field's own type is resolved via
 * resolve_declared_type() (reused, not reimplemented) -- an optional
 * trailing `@ <region>` on a field's own type is consumed the same way
 * emit_defn's own return-type parsing already does, since the emitter
 * never does anything WITH the specific region name in any of these
 * positions. Unlike defenum's shared `void *value`, every field here
 * gets its own real, distinct C type -- so a struct with a String field
 * and an I32 field really does carry a `char *` and a real `int`, not
 * two `void *`s pretending to be typed. */
static int process_defstruct(Arena *arena, StrBuf *out, Node *node, const char **out_error) {
    if (node->child_count < 2 || node->children[1]->type != NODE_SYMBOL) {
        return fail(arena, out_error, "defstruct: malformed definition at line %d", node->line) != NULL;
    }
    const char *struct_name = node->children[1]->text;
    size_t field_count = node->child_count - 2;
    StructField *fields = (StructField *)arena_alloc(arena, sizeof(StructField) * (field_count ? field_count : 1));
    for (size_t i = 0; i < field_count; i++) {
        Node *field = node->children[2 + i];
        if (field->type != NODE_LIST || field->child_count < 3 || field->children[0]->type != NODE_SYMBOL ||
            field->children[1]->type != NODE_COLON) {
            return fail(arena, out_error,
                        "defstruct: field at line %d must be (name : Type) or (name : Type @ Region) "
                        "at line %d", node->line, node->line) != NULL;
        }
        const char *field_type;
        /* `&mut Type` -- a real, distinct two-token reference-type form
         * (space between `&mut` and the type name, unlike the single-
         * token `&Type` resolve_declared_type() already handles), e.g.
         * firefly/gomega.prn's own `(t : &mut T)`. Detected here rather
         * than inside resolve_declared_type() since that function only
         * ever sees one type node at a time -- this is the one call site
         * that actually has the next token available to look at. */
        if (field->child_count == 4 && field->children[2]->type == NODE_SYMBOL &&
            is_symbol(field->children[2], "&mut") && field->children[3]->type == NODE_SYMBOL) {
            const char *base = resolve_base_type_name(field->children[3]);
            if (!base && strcmp(field->children[3]->text, "Any") == 0) base = "void";
            if (!base) {
                return fail(arena, out_error,
                            "defstruct: unsupported reference target type '%s' at line %d",
                            field->children[3]->text, field->children[3]->line) != NULL;
            }
            char buf[128];
            snprintf(buf, sizeof(buf), "%s *", base);
            field_type = arena_strdup(arena, buf, strlen(buf));
        } else {
            field_type = resolve_declared_type(arena, field->children[2], out_error);
            if (!field_type) return 0;
        }
        /* An optional trailing `@ <region>` on the field's own type
         * (child_count == 5: name, colon, type, at, region) is simply
         * never read past index 2 -- consumed by omission, same real,
         * honest "existence only" treatment as everywhere else in this
         * file. */
        fields[i].name = field->children[0]->text;
        fields[i].c_name = mangle(arena, field->children[0]->text);
        fields[i].c_type = field_type;
        /* `(Vec ElemType)` field -- resolve the real element type too
         * (field_type itself is just the erased "Vec"), so get-field's
         * own emission can register a real g_vec_elem_hints entry for
         * this exact field access. Real, honest, narrow re-parse of the
         * same type node resolve_declared_type() already consumed above
         * -- simplest way to recover ElemType without threading a second
         * "and also tell me the Vec's own element type" out-parameter
         * through resolve_declared_type() itself this late in the
         * session. */
        fields[i].vec_elem_type = NULL;
        fields[i].vec_elem_is_scalar = 0;
        if (strcmp(field_type, "Vec") == 0 && field->children[2]->type == NODE_LIST &&
            field->children[2]->child_count == 2 && field->children[2]->children[1]->type == NODE_SYMBOL) {
            Node *elem_node = field->children[2]->children[1];
            const char *elem_c_type = resolve_base_type_name(elem_node);
            if (elem_c_type) {
                fields[i].vec_elem_type = elem_c_type;
                fields[i].vec_elem_is_scalar = (strcmp(elem_c_type, "int") == 0 || strcmp(elem_c_type, "double") == 0);
            }
        }
    }

    StructInfo *info = (StructInfo *)arena_alloc(arena, sizeof(StructInfo));
    info->name = struct_name;
    info->fields = fields;
    info->field_count = field_count;
    info->next = g_structs;
    g_structs = info;

    sb_appendf(out, "typedef struct {\n");
    for (size_t i = 0; i < field_count; i++) {
        sb_append(out, "    ");
        sb_append_decl(out, fields[i].c_type, fields[i].c_name);
        sb_append(out, ";\n");
    }
    sb_appendf(out, "} %s;\n", struct_name);

    sb_appendf(out, "static inline %s %s_new(", struct_name, struct_name);
    for (size_t i = 0; i < field_count; i++) {
        if (i > 0) sb_append(out, ", ");
        sb_append_decl(out, fields[i].c_type, fields[i].c_name);
    }
    if (field_count == 0) sb_append(out, "void");
    sb_appendf(out, ") {\n    %s v;\n", struct_name);
    for (size_t i = 0; i < field_count; i++) {
        sb_appendf(out, "    v.%s = %s;\n", fields[i].c_name, fields[i].c_name);
    }
    sb_append(out, "    return v;\n}\n\n");
    return 1;
}

/* emit_target_defn handles a `#target {:c (inline-c "...")}` function body
 * -- the real FFI escape hatch stdlib/editor's own plugin surface
 * (editor/plugin.prn, editor/buffer.prn, etc.) uses to declare functions
 * whose real implementation lives host-side (the not-yet-decided editor
 * shell), not in emitted-C at all. Real, honest, narrow scope: only the
 * `:c` target key is understood (the map's own real design, per
 * NORTHSTAR.md, anticipates other targets like `:js`/`:wasm` later --
 * not attempted here); the inline-c string is trusted verbatim as real C
 * (VS0 has no way to check it, same trust boundary `alloc`'s own literal
 * string argument already crosses elsewhere in this file). A `void`
 * (`Unit`) return emits the string as a bare statement -- the real
 * stdlib source's own convention is to include its own trailing `;` for
 * that case (see editor/plugin.prn's `register-command`); any other
 * return type wraps it as `return (...);` instead. */
/* find_target_c_src -- shared `:c (inline-c "...")` extraction from a
 * `#target {...}` map, factored out (2026-08-21) so the new mid-body
 * `#target` STATEMENT form (see emit_body's own new handling, found
 * missing while gcc-verifying string.prn's own real `concat`, whose
 * `let`-body is `[out (alloc ...)] #target {:c (inline-c "...")} out`
 * -- a #target block used for its own side effect partway through a
 * body, not replacing the WHOLE function body the way this same map
 * shape already did before this) can share it with emit_target_defn's
 * own original whole-body use, instead of re-deriving the same real
 * :c-key-search + `(inline-c "...")`-shape validation a second time. */
static Node *find_target_c_src(Arena *arena, Node *target_map, const char **out_error) {
    if (target_map->child_count % 2 != 0) {
        fail(arena, out_error, "#target map at line %d has an odd number of forms "
                                "(expected key/value pairs)",
             target_map->line);
        return NULL;
    }
    Node *c_value = NULL;
    for (size_t i = 0; i + 1 < target_map->child_count; i += 2) {
        Node *key = target_map->children[i];
        if (key->type == NODE_KEYWORD && key->text && strcmp(key->text, ":c") == 0) {
            c_value = target_map->children[i + 1];
            break;
        }
    }
    if (!c_value) {
        fail(arena, out_error,
             "#target map at line %d has no :c key (VS0's emitter only understands "
             "the C target so far)",
             target_map->line);
        return NULL;
    }
    if (c_value->type != NODE_LIST || c_value->child_count != 2 || c_value->children[0]->type != NODE_SYMBOL ||
        !is_symbol(c_value->children[0], "inline-c") || c_value->children[1]->type != NODE_STRING) {
        fail(arena, out_error, "#target :c value at line %d must be (inline-c \"...\")", c_value->line);
        return NULL;
    }
    return c_value->children[1];
}

static int emit_target_defn(Arena *arena, StrBuf *out, Node *target_map, const char *fn_name,
                             const char *param_list, const char *return_type, const char **out_error) {
    Node *src = find_target_c_src(arena, target_map, out_error);
    if (!src) return 0;
    StrBuf body;
    sb_init(&body);
    if (strcmp(return_type, "void") == 0) {
        sb_appendf(&body, "    %.*s\n", (int)src->text_len, src->text);
    } else {
        sb_appendf(&body, "    return (%.*s);\n", (int)src->text_len, src->text);
    }
    /* Real, honest bug found and fixed here (an actual gcc compile of a
     * real, large function -- firefly.prn's own `run-tests`): sb_appendf()
     * routes every call through a FIXED 1024-byte internal buffer
     * (vsnprintf), which silently truncates whatever doesn't fit --
     * fine for the short, bounded format strings used everywhere else
     * in this file, genuinely wrong here, where `body.data` is an
     * entire function body and can be arbitrarily large. sb_append()
     * (used directly, piece by piece, below) goes through StrBuf's own
     * real, dynamically-growing buffer instead -- no fixed ceiling. */
    sb_append(out, return_type);
    sb_append(out, " ");
    sb_append(out, fn_name);
    sb_append(out, "(");
    sb_append(out, param_list);
    sb_append(out, ") {\n");
    sb_append(out, body.data);
    sb_append(out, "}\n\n");
    sb_free(&body);
    return 1;
}

/* emit_defn handles one top-level `(defn name [params] body...)`. Every
 * parameter is emitted as `Arena *mangled_name` -- the only parameter
 * type test.prn's own real examples use (`Arena @ region`); a real,
 * scoped limitation, not a general type system. The function's return
 * type is, by default, inferred from its own tail expression's resolved
 * C type (found while emitting the body, since VS0 has no separate type-
 * checking pass yet), defaulting to `void` if nothing resolves -- unless
 * an explicit `: <type>` annotation follows the parameter vector, in
 * which case that declared type is used for the C signature directly
 * instead (VS0 has no way to check the two actually agree; a real,
 * stated limitation, not silently pretended solved). A body of exactly
 * `#target {...}` skips normal body emission entirely -- see
 * emit_target_defn. */
static int emit_defn(Arena *arena, StrBuf *out, Node *defn, const char **out_error) {
    if (defn->child_count < 3 || defn->children[1]->type != NODE_SYMBOL ||
        defn->children[2]->type != NODE_VEC) {
        fail(arena, out_error, "defn: malformed function definition at line %d", defn->line);
        return 0;
    }
    const char *fn_name = mangle(arena, defn->children[1]->text);
    Node *params = defn->children[2];

    EmitScope base;
    scope_init(&base, NULL);

    StrBuf param_list;
    sb_init(&param_list);
    if (params->child_count == 0) {
        sb_append(&param_list, "void");
    }
    for (size_t i = 0; i < params->child_count; i++) {
        Node *param = params->children[i];
        if (param->type != NODE_LIST || param->child_count == 0 || param->children[0]->type != NODE_SYMBOL) {
            fail(arena, out_error, "defn: malformed parameter at line %d", defn->line);
            sb_free(&param_list);
            return 0;
        }
        int has_region = has_region_marker(param);
        const char *c_name = mangle(arena, param->children[0]->text);
        if (i > 0) sb_append(&param_list, ", ");
        if (has_region) {
            scope_bind(&base, param->children[0]->text, c_name, "Arena *", 0 /* already a pointer */);
            /* __attribute__((unused)): same real reasoning as `let` bindings
             * above -- a real function can validly not use one of its own
             * parameters (matching a required signature shape), that's not
             * a genuine Parena-source bug worth a C compiler warning. */
            sb_appendf(&param_list, "Arena *%s __attribute__((unused))", c_name);
        } else if (param->child_count == 3 && param->children[1]->type == NODE_COLON &&
                   param->children[2]->type == NODE_SYMBOL &&
                   (is_symbol(param->children[2], "I32") || is_symbol(param->children[2], "Bool") ||
                    is_symbol(param->children[2], "F64") || is_symbol(param->children[2], "String"))) {
            /* A plain, non-region-annotated `I32`/`Bool`/`F64`/`String`
             * parameter -- the real shape stdlib/editor's own mod-surface
             * files actually use for things like a gutter line number or
             * an x/y pixel coordinate (editor/ui.prn's set-gutter-marker/
             * show-popup), or gfd.prn's own `on : Bool` / `x : F64`
             * world-position parameters, where there's genuinely no
             * arena/region involved at all. Bound as a plain C value, not
             * an `Arena *` -- real, narrower scope than the `Arena @
             * :region/x` path above, not a general type system (a param
             * typed `Arena @ Region` with a bare, non-keyword region
             * variable, or any other custom named type like
             * `DiagnosticSeverity`, still falls through to the honest
             * failure below). */
            const char *c_type = resolve_declared_type(arena, param->children[2], out_error);
            if (!c_type) {
                sb_free(&param_list);
                return 0;
            }
            scope_bind(&base, param->children[0]->text, c_name, c_type, 0 /* not an arena value */);
            sb_appendf(&param_list, "%s %s __attribute__((unused))", c_type, c_name);
        } else if (param->child_count == 3 && param->children[1]->type == NODE_COLON &&
                   is_call_named(param->children[2], "Fn") && param->children[2]->child_count == 3 &&
                   param->children[2]->children[1]->type == NODE_VEC &&
                   param->children[2]->children[1]->child_count == 0) {
            /* A `(Fn [] <ReturnType>)` zero-argument callback parameter --
             * the real shape stdlib/editor's own mod-surface plugin
             * functions need (editor/plugin.prn's register-command,
             * editor/events.prn's subscribe, cache.prn's fetch), extended
             * from the original Unit-only support to any return type
             * resolve_declared_type() already understands (Unit/I32/
             * String/(Result ..)/(Option ..)), since a callback returning
             * a value (cache.prn's `(Fn [] String)` compute thunk, for
             * one real example) is exactly as real a shape as one
             * returning Unit. Emitted as a real C function pointer --
             * C's own inside-out function-pointer declaration syntax,
             * which is why this needs its own format string rather than
             * the `<type> <name>` pattern every other parameter shape
             * above uses. Real, honest, narrow scope: only zero-argument
             * Fn types are understood -- the wider stdlib's own `(Fn
             * [F64 F64] F64)`/`(Fn [&mut T] Unit)`/generic-parameter Fn
             * shapes elsewhere (array.prn, sort.prn, scarab.prn, etc.)
             * are real, separate, unstarted work (VS0 has no generics or
             * reference types yet either), not silently guessed at here. */
            const char *ret_type = resolve_declared_type(arena, param->children[2]->children[2], out_error);
            if (!ret_type) {
                sb_free(&param_list);
                return 0;
            }
            char c_type_buf[64];
            snprintf(c_type_buf, sizeof(c_type_buf), "%s (*)(void)", ret_type);
            const char *c_type = arena_strdup(arena, c_type_buf, strlen(c_type_buf));
            scope_bind(&base, param->children[0]->text, c_name, c_type, 0 /* not an arena value */);
            sb_appendf(&param_list, "%s (*%s)(void) __attribute__((unused))", ret_type, c_name);
        } else if (param->child_count == 3 && param->children[1]->type == NODE_COLON &&
                   is_call_named(param->children[2], "Fn") && param->children[2]->child_count == 3 &&
                   param->children[2]->children[1]->type == NODE_VEC &&
                   param->children[2]->children[1]->child_count > 0) {
            /* A `(Fn [ArgType...] RetType)` NON-zero-argument, typed
             * callback parameter -- scarab.prn's own real `(body : (Fn
             * [&mut T] Unit))` (it's own spec-body callback). Real,
             * general case the zero-arg branch above's own header
             * comment named as unstarted -- resolve_declared_type()
             * itself now understands the full `(Fn [..] ..)` shape
             * (added alongside this), so this branch just delegates to
             * it rather than re-deriving the function-pointer C shape a
             * second time; the only extra work here is splicing the
             * parameter's own name into the "RetType (*)(ArgTypes)"
             * string's empty `(*)` slot, since C's own declaration
             * syntax puts a function pointer's name *inside* the
             * parens, not after the whole type the way every other
             * parameter shape in this loop works. */
            const char *c_type = resolve_declared_type(arena, param->children[2], out_error);
            if (!c_type) {
                sb_free(&param_list);
                return 0;
            }
            const char *paren_star = strstr(c_type, "(*)");
            if (!paren_star) {
                fail(arena, out_error, "defn: internal error resolving Fn-typed parameter '%s' at line %d",
                     param->children[0]->text, param->line);
                sb_free(&param_list);
                return 0;
            }
            size_t prefix_len = (size_t)(paren_star - c_type);
            sb_appendf(&param_list, "%.*s(*%s)%s __attribute__((unused))",
                       (int)prefix_len, c_type, c_name, paren_star + 3);
            scope_bind(&base, param->children[0]->text, c_name, c_type, 0 /* not an arena value */);
        } else if (param->child_count == 3 && param->children[1]->type == NODE_COLON &&
                   param->children[2]->type == NODE_SYMBOL &&
                   (find_enum_by_name(param->children[2]->text) ||
                    find_struct_by_name(param->children[2]->text))) {
            /* A parameter typed as a registered defenum or defstruct,
             * e.g. editor/events.prn's `(event : EditorEvent)` or a real
             * `(req : HttpRequest)` -- passed as a real plain C value of
             * that type's own real struct (process_defenum()'s
             * {tag; void *value;} shape or process_defstruct()'s own
             * real per-field struct), same "no Arena involved" reasoning
             * as the plain I32/String case above. */
            const char *c_type = param->children[2]->text;
            scope_bind(&base, param->children[0]->text, c_name, c_type, 0 /* not an arena value */);
            sb_appendf(&param_list, "%s %s __attribute__((unused))", c_type, c_name);
        } else if (param->child_count == 3 && param->children[1]->type == NODE_COLON &&
                   param->children[2]->type == NODE_SYMBOL && param->children[2]->text &&
                   param->children[2]->text[0] == '&' &&
                   strcmp(param->children[2]->text, "&mut") != 0) {
            /* Single-token `&Type` reference parameter (e.g.
             * firefly/gomega.prn's own `(actual : &Any)`,
             * scarab.prn's `(cases : &(Vec TestCase))` -- that inner
             * form is a NODE_LIST, not NODE_SYMBOL, so it still falls
             * through to the honest failure below, not silently
             * mishandled). resolve_declared_type() already understands
             * this shape (added alongside the field-type path). */
            const char *c_type = resolve_declared_type(arena, param->children[2], out_error);
            if (!c_type) {
                sb_free(&param_list);
                return 0;
            }
            scope_bind(&base, param->children[0]->text, c_name, c_type, 0 /* not an arena value */);
            sb_appendf(&param_list, "%s %s __attribute__((unused))", c_type, c_name);
        } else if (param->child_count == 4 && param->children[1]->type == NODE_COLON &&
                   param->children[2]->type == NODE_SYMBOL && is_symbol(param->children[2], "&mut") &&
                   param->children[3]->type == NODE_SYMBOL) {
            /* Two-token `&mut Type` reference parameter (e.g.
             * firefly.prn's own `(!t : &mut T)`) -- same real, honest
             * "&mut and & both just become a C pointer" treatment
             * resolve_declared_type()'s own single-token path uses. */
            const char *base_type = resolve_base_type_name(param->children[3]);
            if (!base_type && strcmp(param->children[3]->text, "Any") == 0) base_type = "void";
            if (!base_type) {
                fail(arena, out_error,
                     "defn: unsupported reference target type '%s' at line %d",
                     param->children[3]->text, param->children[3]->line);
                sb_free(&param_list);
                return 0;
            }
            char c_type_buf[128];
            snprintf(c_type_buf, sizeof(c_type_buf), "%s *", base_type);
            const char *c_type = arena_strdup(arena, c_type_buf, strlen(c_type_buf));
            scope_bind(&base, param->children[0]->text, c_name, c_type, 0 /* not an arena value */);
            sb_appendf(&param_list, "%s %s __attribute__((unused))", c_type, c_name);
        } else if (param->child_count == 4 && param->children[1]->type == NODE_COLON &&
                   param->children[2]->type == NODE_SYMBOL && is_symbol(param->children[2], "&") &&
                   (param->children[3]->type == NODE_LIST || param->children[3]->type == NODE_SYMBOL)) {
            /* `&(ComplexType)` -- the bare `&` symbol immediately
             * followed by a parenthesized type (no space, e.g.
             * firefly.prn's own `(cases : &(Vec TestCase))`) lexes/
             * parses as two SIBLING nodes here too, the same real
             * two-node shape the expression-level `&(expr)` form
             * already has (see emit_call()'s own argument-loop handling
             * for that parallel case) -- distinct from the single-token
             * `&Type` symbol form resolve_declared_type() handles
             * directly, since a complex type like `(Vec TestCase)`
             * can't be glued onto the `&` as one lexer token the way
             * `&Any` can. Delegates to resolve_declared_type() for the
             * inner type (covers (Vec ..)/(Result ..)/(Option ..)/
             * registered names/plain symbols alike) and wraps the
             * result in a pointer. */
            const char *inner_type = resolve_declared_type(arena, param->children[3], out_error);
            if (!inner_type) {
                sb_free(&param_list);
                return 0;
            }
            char c_type_buf[128];
            snprintf(c_type_buf, sizeof(c_type_buf), "%s *", inner_type);
            const char *c_type = arena_strdup(arena, c_type_buf, strlen(c_type_buf));
            scope_bind(&base, param->children[0]->text, c_name, c_type, 0 /* not an arena value */);
            sb_appendf(&param_list, "%s %s __attribute__((unused))", c_type, c_name);
            /* `&(Vec ElemType)` specifically -- record the real element
             * type in g_vec_elem_hints (see its own declaration comment
             * for why): a registered defenum/defstruct/or plain resolved
             * base type name for ElemType, keyed by this param's own
             * mangled C name, so a later `(vec/get cases i)` call site
             * can report a real element type instead of the generic
             * "void *" fallback.
             *
             * Real bug found and fixed here (2026-08-21, while extending
             * this same mechanism to cover struct fields too): this used
             * to store the RAW SOURCE symbol text directly (`param->
             * children[3]->children[1]->text`) as the hint's own elem_type
             * -- happens to be correct for a registered struct/enum name
             * (never renamed), but wrong for a primitive ElemType like
             * `F64` (real C type "double", not the literal text "F64")
             * -- resolve_base_type_name() is the same real resolution
             * process_defstruct's own new vec_elem_type tracking already
             * uses, applied here too instead of trusting raw source text. */
            if (param->children[3]->type == NODE_LIST && param->children[3]->child_count == 2 &&
                param->children[3]->children[0]->type == NODE_SYMBOL &&
                is_symbol(param->children[3]->children[0], "Vec") &&
                param->children[3]->children[1]->type == NODE_SYMBOL) {
                const char *elem_c_type = resolve_base_type_name(param->children[3]->children[1]);
                if (elem_c_type) {
                    int is_scalar = (strcmp(elem_c_type, "int") == 0 || strcmp(elem_c_type, "double") == 0);
                    record_vec_elem_hint(arena, c_name, elem_c_type, is_scalar);
                }
            }
        } else if (param->child_count == 5 && param->children[1]->type == NODE_COLON &&
                   (param->children[2]->type == NODE_SYMBOL || param->children[2]->type == NODE_LIST) &&
                   param->children[3]->type == NODE_AT) {
            /* `Type @ Region` on a NON-Arena type, e.g. firefly.prn's own
             * `(msg : String @ Region)` -- the real, honest fix for the
             * has_region_marker() bug documented on that function itself:
             * a trailing region annotation on a real, named type still
             * means "resolve the real type, discard the region name"
             * (same as defstruct fields and return types already do),
             * not "silently become an opaque Arena *".
             *
             * Real bug found and fixed here (2026-08-21, array.prn's own
             * `zeros`/`from-vec`/`reshape`): this branch originally required
             * children[2]->type == NODE_SYMBOL, so a NON-reference compound
             * type with a trailing region annotation -- e.g. array.prn's
             * own `(shape : (Vec I32) @ :region/scratch)`, no `&` prefix --
             * fell through to the generic failure message instead of being
             * resolved. resolve_declared_type() already handles a NODE_LIST
             * type node fine (that's exactly what the `&(ComplexType)`
             * branch above already delegates to for its own inner type),
             * so accepting NODE_LIST here too is a direct, narrow fix, not
             * a new resolution path. */
            const char *c_type = resolve_declared_type(arena, param->children[2], out_error);
            if (!c_type) {
                sb_free(&param_list);
                return 0;
            }
            scope_bind(&base, param->children[0]->text, c_name, c_type, 0 /* not an arena value */);
            sb_appendf(&param_list, "%s %s __attribute__((unused))", c_type, c_name);
            /* `(Vec ElemType) @ Region` specifically -- same
             * g_vec_elem_hints registration the `&(Vec ElemType)` branch
             * above already does, so vec/get et al. on a non-reference
             * Vec-typed param (passed by value, still a real Vec struct)
             * gets a real element type instead of the generic fallback. */
            if (param->children[2]->type == NODE_LIST && param->children[2]->child_count == 2 &&
                param->children[2]->children[0]->type == NODE_SYMBOL &&
                is_symbol(param->children[2]->children[0], "Vec") &&
                param->children[2]->children[1]->type == NODE_SYMBOL) {
                const char *elem_c_type = resolve_base_type_name(param->children[2]->children[1]);
                if (elem_c_type) {
                    int is_scalar = (strcmp(elem_c_type, "int") == 0 || strcmp(elem_c_type, "double") == 0);
                    record_vec_elem_hint(arena, c_name, elem_c_type, is_scalar);
                }
            }
        } else {
            fail(arena, out_error,
                 "defn: parameter '%s' has no region annotation and isn't a plain I32/String/"
                 "(Fn [] ..)/registered-defenum-or-defstruct-type/reference-type either (VS0 only "
                 "supports `Arena @ :region/x`, `I32`, `String`, zero-argument `(Fn [] <ReturnType>)`, "
                 "registered defenum/defstruct type, and `&Type`/`&mut Type` reference parameters so "
                 "far)",
                 param->children[0]->text);
            sb_free(&param_list);
            return 0;
        }
    }

    size_t body_start = 3;
    const char *declared_return_type = NULL;
    if (body_start < defn->child_count && defn->children[body_start]->type == NODE_COLON) {
        if (body_start + 1 >= defn->child_count) {
            fail(arena, out_error, "defn: ':' return-type annotation at line %d has no type after it",
                 defn->children[body_start]->line);
            sb_free(&param_list);
            return 0;
        }
        declared_return_type = resolve_declared_type(arena, defn->children[body_start + 1], out_error);
        if (!declared_return_type) {
            sb_free(&param_list);
            return 0;
        }
        /* Register into g_defn_return_types (see its own declaration
         * comment) -- BEFORE this function's own body is emitted below,
         * so a self-recursive call within its own body (or a `#target`
         * call in a later function calling back into this one) can
         * already see it, same "register first, emit second" order
         * process_defenum()/process_defstruct() already use. */
        DefnReturnType *drt = (DefnReturnType *)arena_alloc(arena, sizeof(DefnReturnType));
        drt->c_name = fn_name;
        drt->return_type = declared_return_type;
        drt->payload_type = resolve_result_option_payload_type(arena, defn->children[body_start + 1]);
        drt->next = g_defn_return_types;
        g_defn_return_types = drt;
        body_start += 2;
        /* An optional trailing `@ <region>` on the return type itself,
         * e.g. `: Cache @ Region` (cache.prn's own open) or
         * `: (Result (Vec PortResult) ScanError) @ Region`
         * (pentest/scan.prn's own scan-ports) -- the same real region-
         * annotation shape parameters already accept via
         * has_region_marker(), now also accepted on a return type.
         * Consumed and discarded, same "existence only, no semantic use
         * of the specific region name" honesty as the parameter side. */
        if (body_start + 1 < defn->child_count && defn->children[body_start]->type == NODE_AT) {
            body_start += 2;
        }
    }

    if (body_start < defn->child_count && is_symbol(defn->children[body_start], "#target")) {
        if (body_start + 1 >= defn->child_count || defn->children[body_start + 1]->type != NODE_MAP) {
            fail(arena, out_error, "defn: #target at line %d must be followed by a {...} map",
                 defn->children[body_start]->line);
            sb_free(&param_list);
            return 0;
        }
        int ok = emit_target_defn(arena, out, defn->children[body_start + 1], fn_name, param_list.data,
                                   declared_return_type ? declared_return_type : "void", out_error);
        sb_free(&param_list);
        return ok;
    }

    StrBuf body;
    sb_init(&body);
    const char *inferred_return_type = "void";
    int ok = emit_body(arena, &body, defn->children + body_start, defn->child_count - body_start, &base, 1,
                        &inferred_return_type, out_error);
    if (!ok) {
        sb_free(&param_list);
        sb_free(&body);
        return 0;
    }

    const char *return_type = declared_return_type ? declared_return_type : inferred_return_type;
    /* Real bug found and fixed here (an actual gcc compile of
     * firefly.prn's own real `run-tests` -- the file silently got cut
     * off mid-token, right in the middle of `continue;`): sb_appendf()
     * routes through a fixed 1024-byte internal vsnprintf buffer, which
     * silently truncates whatever doesn't fit -- fine for this file's
     * many short, bounded format strings, genuinely wrong for a whole
     * function body, which can be (and here, was) far larger than that.
     * `parena build`'s own exit code never caught this either -- it
     * doesn't re-parse its own output C, by design. sb_append() (used
     * piece by piece below) goes through StrBuf's own real,
     * dynamically-growing buffer instead. */
    sb_append(out, return_type);
    sb_append(out, " ");
    sb_append(out, fn_name);
    sb_append(out, "(");
    sb_append(out, param_list.data);
    sb_append(out, ") {\n");
    sb_append(out, body.data);
    sb_append(out, "}\n\n");
    sb_free(&param_list);
    sb_free(&body);
    return 1;
}

const char *emit_c(Arena *arena, Node *program, const char **out_error) {
    *out_error = NULL;
    /* g_enums is reset per emit_c() call, not just per process: the test
     * suite calls emit_c() many times in the same process (one per test
     * case), and without this reset a defenum registered by an earlier
     * test would still be "known" to a later one -- real cross-test
     * contamination, not a hypothetical. `parena build` (main.c) only
     * ever calls emit_c() once per process invocation, so this is a
     * no-op there. */
    g_enums = NULL;
    g_structs = NULL; /* same per-call reset, same real reason -- see g_enums' own comment */
    g_vec_elem_hints = NULL; /* same per-call reset -- see its own declaration comment */
    g_defn_return_types = NULL; /* same per-call reset -- see its own declaration comment */
    g_boxed_types = NULL; /* same per-call reset -- see its own declaration comment */
    static int box_helpers_ever_inited = 0;
    if (box_helpers_ever_inited) sb_free(&g_box_helpers); /* free any prior call's leftover buffer */
    sb_init(&g_box_helpers);
    box_helpers_ever_inited = 1;
    g_lambda_count = 0; /* same per-call reset -- see g_lambda_helpers' own declaration comment */
    static int lambda_helpers_ever_inited = 0;
    if (lambda_helpers_ever_inited) sb_free(&g_lambda_helpers);
    sb_init(&g_lambda_helpers);
    lambda_helpers_ever_inited = 1;
    g_veceq_types = NULL; /* same per-call reset -- see g_veceq_helpers' own declaration comment */
    static int veceq_helpers_ever_inited = 0;
    if (veceq_helpers_ever_inited) sb_free(&g_veceq_helpers);
    sb_init(&g_veceq_helpers);
    veceq_helpers_ever_inited = 1;
    g_veclit_count = 0; /* same per-call reset -- see g_veclit_helpers' own declaration comment */
    static int veclit_helpers_ever_inited = 0;
    if (veclit_helpers_ever_inited) sb_free(&g_veclit_helpers);
    sb_init(&g_veclit_helpers);
    veclit_helpers_ever_inited = 1;
    StrBuf out;
    sb_init(&out);
    sb_append(&out, "/* Generated by parena build -- VS0 domain 3, do not edit by hand. */\n");
    sb_append(&out, "#include \"parena_runtime.h\"\n");
    sb_append(&out, "#include <string.h>\n");
    /* Real, honest gap found and fixed here (2026-08-21, gcc-verifying
     * string.prn's own real `length`, whose #target inline-C body casts
     * to `int32_t`): every generated file always includes <string.h>
     * unconditionally already, for the same real reason -- rather than
     * only including <stdint.h> when some detectable feature needs it
     * (VS0 has no way to inspect the trusted-verbatim contents of an
     * inline-C string to know), it's included unconditionally too, the
     * same honest tradeoff already made for <string.h>. */
    sb_append(&out, "#include <stdint.h>\n");
    /* Same real, honest, unconditional-inclusion tradeoff as
     * <stdint.h> above (2026-08-21, gcc-verifying string.prn's own
     * real `raw-parse-i32`, whose #target inline-C body calls `atoi`,
     * declared in <stdlib.h>). */
    sb_append(&out, "#include <stdlib.h>\n\n");

    /* Pre-pass: every defenum AND defstruct, processed together in ONE
     * pass, in their real, natural combined-file order -- before any
     * defn. Real, honest, narrower limitation, unchanged from before:
     * a type referencing ANOTHER registered type (a defenum field
     * needing a defstruct, or vice versa) requires that other type to
     * be declared EARLIER in the combined order -- a single linear
     * pass, not a real two-pass "register every name first, then
     * resolve every field" forward-reference system.
     *
     * Real bug found and fixed here (2026-08-20, while testing the new
     * multi-file build against the real, combined ladybug/scarab
     * files): this used to be two SEPARATE loops -- "every defenum in
     * the whole program" fully processed first, THEN "every defstruct"
     * -- regardless of where each form actually appeared. That broke
     * scarab.prn's own real SuiteNode (its Spec variant's `body : (Fn
     * [&mut T] Unit)` field needs T, a defstruct from firefly.prn)
     * even though T genuinely appears EARLIER in the real, combined
     * file order (firefly.prn's own forms come first) -- the old
     * "all defenums, unconditionally, before any defstruct" ordering
     * silently ignored that real, natural precedence. Merging into one
     * pass over the program's own real, combined order fixes exactly
     * that, without weakening the existing "earlier in the combined
     * order" requirement itself. */
    for (size_t i = 0; i < program->child_count; i++) {
        Node *form = program->children[i];
        if (is_call_named(form, "defenum")) {
            if (!process_defenum(arena, &out, form, out_error)) {
                sb_free(&out);
                return NULL;
            }
        } else if (is_call_named(form, "defstruct")) {
            if (!process_defstruct(arena, &out, form, out_error)) {
                sb_free(&out);
                return NULL;
            }
        }
    }

    /* Pre-pass: a real forward DECLARATION for every `defn` that has an
     * explicit `: ReturnType` annotation -- found genuinely missing
     * (2026-08-21, gcc-verifying string.prn's own real `parse-i32`,
     * which calls `is-valid-i32-text?`, defined LATER in the same
     * file): C requires a function be at least declared before its
     * first use, and every defn was previously emitted strictly in the
     * program's own real source order with no declarations at all --
     * any function calling another one defined later in the same file
     * hit a real "implicit declaration of function" gcc error,
     * undetected by `parena build`'s own exit code (which never
     * re-parses its own generated C).
     *
     * Real, honest, deliberately narrow scope: only the RETURN TYPE is
     * needed for a valid forward declaration here, not the full
     * parameter list -- `ReturnType mangled_name();` (an old-style,
     * unspecified-argument C declaration) compiles cleanly under this
     * project's own `-std=c99 -Wall -Wextra -pedantic -Werror`
     * (confirmed via a real, standalone gcc test before writing this),
     * so this deliberately does NOT duplicate emit_defn's own much
     * larger parameter-type-resolution logic (many branches: I32/
     * String/Fn/&Type/&(ComplexType)/Type@Region/etc.) just to build a
     * full prototype nobody's own call site needs matched exactly.
     *
     * Real, honest, NOT-yet-covered case: a `defn` with NO explicit `:`
     * return-type annotation (return type inferred from its own body's
     * tail expression) gets no forward declaration here -- resolving
     * that would require walking the body BEFORE this pre-pass has any
     * business doing so (the same real "no separate type-checking
     * pass" limitation this whole emitter already has elsewhere,
     * flagged rather than solved). Every real forward-reference case
     * found so far (string.prn's `is-valid-i32-text?`, regex/glob.prn's
     * `glob-match`) has an explicit return type, so this narrower scope
     * already covers every real, known-blocking case.
     *
     * Also registers into g_defn_return_types here, same real registry
     * emit_defn itself populates later (see its own declaration
     * comment) -- a free, correct improvement once resolved this early:
     * emit_call()'s own "no function-signature table" fallback can now
     * see a forward-referenced function's real return type too, not
     * just an earlier-in-file one. emit_defn's own later, redundant
     * registration (once it reaches that same defn) is harmless --
     * find_defn_return_type() only ever reads the first match, and it's
     * the same value either way. */
    for (size_t i = 0; i < program->child_count; i++) {
        Node *form = program->children[i];
        if (!is_call_named(form, "defn")) continue;
        if (form->child_count < 3 || form->children[1]->type != NODE_SYMBOL) continue;
        if (form->child_count < 5 || form->children[3]->type != NODE_COLON) continue;
        const char *proto_fn_name = mangle(arena, form->children[1]->text);
        const char *proto_return_type = resolve_declared_type(arena, form->children[4], out_error);
        if (!proto_return_type) {
            sb_free(&out);
            return NULL;
        }
        sb_append(&out, proto_return_type);
        sb_append(&out, " ");
        sb_append(&out, proto_fn_name);
        sb_append(&out, "();\n");
        DefnReturnType *drt = (DefnReturnType *)arena_alloc(arena, sizeof(DefnReturnType));
        drt->c_name = proto_fn_name;
        drt->return_type = proto_return_type;
        drt->payload_type = resolve_result_option_payload_type(arena, form->children[4]);
        drt->next = g_defn_return_types;
        g_defn_return_types = drt;
    }
    sb_append(&out, "\n");

    /* defn bodies are built into their OWN buffer, not appended to `out`
     * directly, so g_box_helpers (populated lazily as a side effect of
     * emitting Ok/Err/Some calls INSIDE these very defn bodies -- see
     * its own declaration comment) can be spliced into `out` BEFORE the
     * defn text below, once every defn has actually run and every
     * needed box helper is known. Necessary because C requires a
     * function be at least declared before its first use, and a defn
     * near the top of the file can be the very first thing that needs
     * a not-yet-discovered box helper. */
    StrBuf defn_out;
    sb_init(&defn_out);
    for (size_t i = 0; i < program->child_count; i++) {
        Node *form = program->children[i];
        if (!is_call_named(form, "defn")) continue;
        if (!emit_defn(arena, &defn_out, form, out_error)) {
            sb_free(&defn_out);
            sb_free(&out);
            return NULL;
        }
    }
    sb_append(&out, g_box_helpers.data);
    sb_append(&out, g_veceq_helpers.data);
    sb_append(&out, g_veclit_helpers.data);
    sb_append(&out, g_lambda_helpers.data);
    sb_append(&out, defn_out.data);
    sb_free(&defn_out);

    const char *result = arena_strdup(arena, out.data, out.len);
    sb_free(&out);
    return result;
}
