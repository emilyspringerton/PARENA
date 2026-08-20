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
 * "buffer/set-data", "load-config") become '_'. */
static const char *mangle(Arena *arena, const char *name) {
    size_t len = strlen(name);
    char *out = (char *)arena_alloc(arena, len + 1);
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        /* '!' joins '-'/'/' here for the same reason: real source uses it
         * both as a linear-binding-name prefix (`!t`) and a mutating-call
         * suffix (`vec/push!`, `set!`) -- neither is a valid C identifier
         * character. Untested until now because reference-type support
         * (the thing that actually lets `!t : &mut T`-shaped code reach
         * this function) didn't exist yet. */
        out[i] = (c == '-' || c == '/' || c == '!') ? '_' : c;
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
 * payload-field limitation -- restated here, not solved differently for
 * user enums). Real, narrow scope: each variant carries at most one
 * payload field (every real defenum in this stdlib's own .prn files --
 * editor/events.prn's EditorEvent, gfd.prn's PanelKind, etc. -- fits this
 * shape; a variant with two or more payload fields is separate, real,
 * unstarted work, same honest boundary as everywhere else in this file).
 *
 * g_enums is file-scope state, not threaded through every emit_* function
 * signature -- a deliberate, scoped simplification: `parena build`
 * processes exactly one file per process invocation (see main.c), so
 * there's no real reentrancy concern here, and threading a new parameter
 * through emit_expr/emit_match/emit_body/emit_defn/emit_loop/emit_if/
 * emit_binop/emit_call/emit_with_arena/emit_let (ten-plus call sites)
 * would be a lot of mechanical churn for a property that's genuinely
 * global to one compilation. */
typedef struct {
    const char *name;
    int has_payload;
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
} StructField;

typedef struct StructInfo {
    const char *name;
    StructField *fields;
    size_t field_count;
    struct StructInfo *next;
} StructInfo;

static StructInfo *g_structs = NULL;

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

static char *fail(Arena *arena, const char **out_error, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    *out_error = arena_strdup(arena, buf, strlen(buf));
    return NULL;
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
        if (variant->type != NODE_LIST || variant->child_count < 1 || variant->child_count > 2 ||
            variant->children[0]->type != NODE_SYMBOL) {
            return fail(arena, out_error,
                        "defenum: variant at line %d must be (Name) or (Name (field : Type)) -- "
                        "VS0's emitter only supports at most one payload field per variant so far",
                        node->line) != NULL;
        }
        variants[i].name = variant->children[0]->text;
        variants[i].has_payload = (variant->child_count == 2);
        variants[i].tag_value = (int)i;
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
        if (variants[i].has_payload) {
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

/* emit_alloc_call handles the one rank-producing/value-producing call
 * this pass understands: `(alloc arena-expr String "literal")`. Emits
 * `arena_strdup(<arena-arg>, "literal", strlen("literal"))` and reports
 * its own C type as "char *" via *out_type. */
static const char *emit_alloc_call(Arena *arena, Node *call, EmitScope *scope, const char **out_type,
                                    const char **out_error) {
    if (call->child_count < 4) {
        return fail(arena, out_error, "alloc: expected (alloc arena-expr Type \"literal\"), got %zu forms",
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
    if (value_node->type != NODE_STRING) {
        return fail(arena, out_error, "alloc: expected a string literal value at line %d", value_node->line);
    }

    char buf[512];
    snprintf(buf, sizeof(buf), "arena_strdup(%s, \"%s\", %zu)", arena_arg_expr(arena, arena_local),
              value_node->text, value_node->text_len);
    *out_type = "char *";
    return arena_strdup(arena, buf, strlen(buf));
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

/* emit_call handles a general function call `(fn-name arg1 arg2 ...)`
 * not otherwise recognized -- mangles the function name, recursively
 * emits each argument. Real, honest limitation: VS0 has no function-
 * signature table yet (no separate type-checking pass), so the return
 * type of an arbitrary call is reported as "void *" unless the callee
 * happens to be a known local closure -- a real gap flagged here, not
 * silently guessed at as something more specific. */
static const char *emit_call(Arena *arena, Node *call, EmitScope *scope, const char **out_type,
                              const char **out_error) {
    const char *fn_name = mangle(arena, call->children[0]->text);
    StrBuf args;
    sb_init(&args);
    for (size_t i = 1; i < call->child_count; i++) {
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
        if (call->children[i]->type == NODE_SYMBOL && is_symbol(call->children[i], "&") &&
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
        sb_append(&args, arg_c);
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
    if (strcmp(fn_name, "vec_push_") == 0) {
        *out_type = "void";
    } else if (strcmp(fn_name, "vec_new") == 0) {
        *out_type = "Vec";
    } else if (strcmp(fn_name, "vec_len") == 0) {
        *out_type = "int";
    } else {
        *out_type = "void *";
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
    /* A bare, zero-payload user defenum variant (e.g. `OnSave`) -- same
     * "checked before generic symbol lookup" treatment as `None` above,
     * since it's a real value constructor, not a local variable
     * reference. Checked before scope_lookup so a real variant name never
     * gets misreported as an unbound identifier. */
    if (expr->type == NODE_SYMBOL) {
        EnumVariant *variant = NULL;
        EnumInfo *owner = find_enum_variant(expr->text, &variant);
        if (owner && !variant->has_payload) {
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
         * comment) -- a non-pointer payload (e.g. a bare `double`) can't
         * implicitly convert to void* in real C, so this is reported
         * rather than emitting C that fails to compile downstream with a
         * much more confusing error at the *emitted-C* compile step. */
        if (!inner_type || inner_type[strlen(inner_type) - 1] != '*') {
            return fail(arena, out_error,
                        "%s: VS0's emitter only supports pointer-typed payloads so far (got type "
                        "'%s' at line %d)",
                        expr->children[0]->text, inner_type ? inner_type : "?", expr->line);
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
        if (owner && variant->has_payload) {
            if (expr->child_count != 2) {
                return fail(arena, out_error, "%s: expects exactly one argument at line %d",
                            expr->children[0]->text, expr->line);
            }
            const char *inner_type = NULL;
            const char *inner_c = emit_expr(arena, expr->children[1], scope, &inner_type, out_error);
            if (!inner_c) return NULL;
            if (!inner_type || inner_type[strlen(inner_type) - 1] != '*') {
                return fail(arena, out_error,
                            "%s: VS0's emitter only supports pointer-typed payloads so far (got type "
                            "'%s' at line %d)",
                            expr->children[0]->text, inner_type ? inner_type : "?", expr->line);
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
        char buf[512];
        snprintf(buf, sizeof(buf), "(*(%s))", inner_c);
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
                return arena_strdup(arena, buf, strlen(buf));
            }
        }
        return fail(arena, out_error, "get-field: '%s' has no field '%s' at line %d",
                    sinfo->name, field_name, expr->line);
    }
    if (expr->type == NODE_LIST && expr->child_count > 0 && expr->children[0]->type == NODE_SYMBOL) {
        const char *c_op = binop_c_symbol(expr->children[0]->text);
        if (c_op) return emit_binop(arena, expr, c_op, scope, out_type, out_error);
        return emit_call(arena, expr, scope, out_type, out_error);
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

/* emit_loop_tail handles the real tail position inside a `loop` body --
 * exactly the shape every real `loop`/`recur` use in this stdlib
 * actually has (test.prn/region.c's own C code doesn't use this yet,
 * but stdlib/vec.prn's push!/grow!, stdlib/map.prn's find-slot, etc. --
 * the real .prn source already written this session -- all follow this
 * same shape): `(if cond then else)` where one branch is a plain
 * terminal value and the other is `(recur new-vals...)`. Real, honest
 * scope: only `if` and `recur` are understood in tail position -- a
 * `loop` whose tail is a bare `recur` (no `if` at all, an infinite loop
 * with no base case) or a `cond`/`match` in tail position isn't
 * supported yet, reported not guessed. */
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
    /* A plain value in tail position: this is the loop's own real result. */
    const char *val_type = NULL;
    const char *val_c = emit_expr(arena, tail, scope, &val_type, out_error);
    if (!val_c) return 0;
    sb_appendf(out, "        %s = %s;\n", result_var, val_c);
    sb_append(out, "        break;\n");
    if (out_result_type) *out_result_type = val_type;
    return 1;
}

/* emit_loop handles `(loop [var1 init1 var2 init2 ...] body...)` as a
 * real C `while (1) { ... }`, mutable loop-variable locals reassigned
 * (via emit_loop_tail's own real simultaneous-assignment) on `recur`,
 * `break` on the real terminal case. The loop's own result type is
 * inferred the same "emit into a temp buffer first, read the type back
 * as a side effect" way emit_defn's own return type already is (the
 * result variable's own declaration needs a real type, but that type
 * isn't known until the body -- specifically its terminal branch -- has
 * actually been walked). */
static int emit_loop(Arena *arena, StrBuf *out, Node *node, EmitScope *scope, int return_mode,
                      const char **out_return_type, const char **out_error) {
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

    static int loop_counter = 0;
    char result_var[64];
    snprintf(result_var, sizeof(result_var), "__loop_result_%d", loop_counter++);

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
            sb_appendf(&body, "        %s;\n", expr_c);
        }
    }

    const char *result_type = NULL;
    if (!emit_loop_tail(arena, &body, body_forms[body_count - 1], &child, loop_locals, loop_var_count,
                         result_var, &result_type, out_error)) {
        sb_free(&body);
        return 0;
    }

    sb_appendf(out, "    %s %s;\n", result_type ? result_type : "void *", result_var);
    sb_append(out, "    while (1) {\n");
    sb_append(out, body.data);
    sb_append(out, "    }\n");
    sb_free(&body);

    if (return_mode) {
        if (out_return_type) *out_return_type = result_type ? result_type : "void *";
        sb_appendf(out, "    return %s;\n", result_var);
    }
    return 1;
}

/* emit_match handles `(match scrutinee-expr (pattern body) (pattern
 * body) ...)` -- real, honest scope: only the two real built-in tagged
 * unions NORTHSTAR.md's own "Zero-allocation pattern matching" section
 * names (`Result`/`Option`, via their real `Ok`/`Err`/`Some`/`None`
 * constructors), not a general N-variant `defenum` matcher (`defenum`
 * itself has no emission at all yet). Each clause's own pattern is
 * either `(Ctor binding)` or a bare symbol (`None`, or `_` as a real
 * wildcard); each clause body is exactly one expression -- real,
 * deliberately narrower than emit_body's own full with-arena/let/loop
 * support, the same scoping judgment `emit_if`'s own ternary-only,
 * expression-position-only design already made. */
static int emit_match(Arena *arena, StrBuf *out, Node *node, EmitScope *scope, int return_mode,
                       const char **out_return_type, const char **out_error) {
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
    char tmp_var[64], result_var[64];
    snprintf(tmp_var, sizeof(tmp_var), "__match_tmp_%d", id);
    snprintf(result_var, sizeof(result_var), "__match_result_%d", id);

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
        const char *clause_c = emit_expr(arena, clause->children[1], &clause_scope, &clause_type, out_error);
        if (!clause_c) {
            sb_free(&clauses);
            return 0;
        }
        sb_appendf(&clauses, "        %s = %s;\n", result_var, clause_c);
        if (!result_type) result_type = clause_type;
        sb_append(&clauses, "    }\n");
    }

    sb_appendf(out, "    %s %s;\n", result_type ? result_type : "void *", result_var);
    sb_append(out, clauses.data);
    sb_free(&clauses);

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
        } else {
            const char *c_type = NULL;
            const char *expr_c = emit_expr(arena, form, scope, &c_type, out_error);
            if (!expr_c) return 0;
            sb_appendf(out, "    %s;\n", expr_c);
        }
    }

    Node *tail = forms[count - 1];
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
            sb_appendf(out, "    %s;\n", expr_c);
        } else {
            sb_appendf(out, "    return %s;\n", expr_c);
        }
    } else {
        sb_appendf(out, "    %s;\n", expr_c);
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
    }

    StructInfo *info = (StructInfo *)arena_alloc(arena, sizeof(StructInfo));
    info->name = struct_name;
    info->fields = fields;
    info->field_count = field_count;
    info->next = g_structs;
    g_structs = info;

    sb_appendf(out, "typedef struct {\n");
    for (size_t i = 0; i < field_count; i++) {
        sb_appendf(out, "    %s %s;\n", fields[i].c_type, fields[i].c_name);
    }
    sb_appendf(out, "} %s;\n", struct_name);

    sb_appendf(out, "static inline %s %s_new(", struct_name, struct_name);
    for (size_t i = 0; i < field_count; i++) {
        if (i > 0) sb_append(out, ", ");
        sb_appendf(out, "%s %s", fields[i].c_type, fields[i].c_name);
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
static int emit_target_defn(Arena *arena, StrBuf *out, Node *target_map, const char *fn_name,
                             const char *param_list, const char *return_type, const char **out_error) {
    if (target_map->child_count % 2 != 0) {
        return fail(arena, out_error, "defn: #target map at line %d has an odd number of forms "
                                       "(expected key/value pairs)",
                    target_map->line) != NULL;
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
        return fail(arena, out_error,
                     "defn: #target map at line %d has no :c key (VS0's emitter only understands "
                     "the C target so far)",
                     target_map->line) != NULL;
    }
    if (c_value->type != NODE_LIST || c_value->child_count != 2 || c_value->children[0]->type != NODE_SYMBOL ||
        !is_symbol(c_value->children[0], "inline-c") || c_value->children[1]->type != NODE_STRING) {
        return fail(arena, out_error,
                     "defn: #target :c value at line %d must be (inline-c \"...\")",
                     c_value->line) != NULL;
    }
    Node *src = c_value->children[1];
    StrBuf body;
    sb_init(&body);
    if (strcmp(return_type, "void") == 0) {
        sb_appendf(&body, "    %.*s\n", (int)src->text_len, src->text);
    } else {
        sb_appendf(&body, "    return (%.*s);\n", (int)src->text_len, src->text);
    }
    sb_appendf(out, "%s %s(%s) {\n%s}\n\n", return_type, fn_name, param_list, body.data);
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
        } else if (param->child_count == 5 && param->children[1]->type == NODE_COLON &&
                   param->children[2]->type == NODE_SYMBOL && param->children[3]->type == NODE_AT) {
            /* `Type @ Region` on a NON-Arena type, e.g. firefly.prn's own
             * `(msg : String @ Region)` -- the real, honest fix for the
             * has_region_marker() bug documented on that function itself:
             * a trailing region annotation on a real, named type still
             * means "resolve the real type, discard the region name"
             * (same as defstruct fields and return types already do),
             * not "silently become an opaque Arena *". */
            const char *c_type = resolve_declared_type(arena, param->children[2], out_error);
            if (!c_type) {
                sb_free(&param_list);
                return 0;
            }
            scope_bind(&base, param->children[0]->text, c_name, c_type, 0 /* not an arena value */);
            sb_appendf(&param_list, "%s %s __attribute__((unused))", c_type, c_name);
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
    sb_appendf(out, "%s %s(%s) {\n%s}\n\n", return_type, fn_name, param_list.data, body.data);
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
    StrBuf out;
    sb_init(&out);
    sb_append(&out, "/* Generated by parena build -- VS0 domain 3, do not edit by hand. */\n");
    sb_append(&out, "#include \"parena_runtime.h\"\n");
    sb_append(&out, "#include <string.h>\n\n");

    /* Pre-pass: register every defenum and emit its real C type
     * definitions before any function body is emitted, since a defn
     * anywhere in the file can reference a defenum declared anywhere
     * else in the file (order-independent, matching how a real C
     * program's own typedefs would need to come first regardless of
     * where in the source they were declared). */
    for (size_t i = 0; i < program->child_count; i++) {
        Node *form = program->children[i];
        if (!is_call_named(form, "defenum")) continue;
        if (!process_defenum(arena, &out, form, out_error)) {
            sb_free(&out);
            return NULL;
        }
    }

    /* Pre-pass: every defstruct, after every defenum (a struct field can
     * reference a registered enum type) but still before any defn. Real,
     * honest, narrower limitation than defenum's own order-independence:
     * a struct field referencing ANOTHER struct type requires that
     * other struct to be declared earlier in the same file -- this is a
     * single linear pass, not a two-pass "register every name first,
     * then resolve every field" -- not attempted here since no real
     * stdlib file needs struct-in-struct nesting yet. */
    for (size_t i = 0; i < program->child_count; i++) {
        Node *form = program->children[i];
        if (!is_call_named(form, "defstruct")) continue;
        if (!process_defstruct(arena, &out, form, out_error)) {
            sb_free(&out);
            return NULL;
        }
    }

    for (size_t i = 0; i < program->child_count; i++) {
        Node *form = program->children[i];
        if (!is_call_named(form, "defn")) continue;
        if (!emit_defn(arena, &out, form, out_error)) {
            sb_free(&out);
            return NULL;
        }
    }

    const char *result = arena_strdup(arena, out.data, out.len);
    sb_free(&out);
    return result;
}
