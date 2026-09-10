/* emit_llvm.c — real, v0, narrow-scope LLVM IR emitter. See emit_llvm.h's own header comment for
 * the full real scope statement and the genuinely new SSA-register/bottom-up-type architecture
 * this file needs that emit_ts.c/emit_java.c don't.
 */
#include "emit_llvm.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* --- minimal, self-contained string builder -- same real shape emit_java.c's own JavaBuf/
   emit_ts.c's own TsBuf already use, deliberately NOT shared (see emit_llvm.h's own header
   comment on this file's own independence). --- */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} LlvmBuf;

static void lb_init(LlvmBuf *b) {
    b->cap = 256;
    b->data = malloc(b->cap);
    b->data[0] = '\0';
    b->len = 0;
}

static void lb_free(LlvmBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void lb_append(LlvmBuf *b, const char *s) {
    size_t add_len = strlen(s);
    if (b->len + add_len + 1 > b->cap) {
        while (b->len + add_len + 1 > b->cap) b->cap *= 2;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, add_len + 1);
    b->len += add_len;
}

static void lb_appendf(LlvmBuf *b, const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    lb_append(b, tmp);
}

/* --- small AST helpers, same real shape every other emitter's own is_symbol/is_call_named
   already use, reimplemented independently here. --- */
static int is_symbol(Node *n, const char *text) {
    return n && n->type == NODE_SYMBOL && strcmp(n->text, text) == 0;
}

static int is_call_named(Node *n, const char *name) {
    return n && n->type == NODE_LIST && n->child_count >= 1 && is_symbol(n->children[0], name);
}

/* mangle_name: PARENA's own kebab-case identifiers aren't legal LLVM identifiers as-is (a bare
   `-` in an LLVM name would be read as part of a numeric suffix in some contexts and is, at
   minimum, unconventional) -- real, minimal kebab -> snake_case conversion, matching LLVM's own
   real, standard C-like identifier convention (unlike Java/TS's own camelCase target-language
   convention). */
static const char *mangle_name(Arena *arena, const char *kebab) {
    size_t len = strlen(kebab);
    char *out = arena_alloc(arena, len + 1);
    for (size_t i = 0; i < len; i++) {
        out[i] = (kebab[i] == '-') ? '_' : kebab[i];
    }
    out[len] = '\0';
    return out;
}

/* resolve_llvm_type: the real I32/F64/Bool/String/Unit -> LLVM type mapping this v0 understands.
   String -> `ptr` (2026-09-10, real String support -- see the NODE_STRING case in
   emit_llvm_expr's own doc comment for the real, opaque-pointer-era reasoning behind that
   mapping). Any other type name is a real, honest "unsupported" error, same real boundary every
   other emitter's own resolve_*_type draws. */
static const char *resolve_llvm_type(Node *type_sym, const char **out_error) {
    if (!type_sym || type_sym->type != NODE_SYMBOL) {
        *out_error = "emit_llvm: expected a type symbol";
        return NULL;
    }
    if (strcmp(type_sym->text, "I32") == 0) return "i32";
    if (strcmp(type_sym->text, "F64") == 0) return "double";
    if (strcmp(type_sym->text, "Bool") == 0) return "i1";
    if (strcmp(type_sym->text, "String") == 0) return "ptr";
    if (strcmp(type_sym->text, "Unit") == 0) return "void";
    *out_error = "emit_llvm: unsupported parameter/return type (v0 only understands I32/F64/Bool/String/Unit)";
    return NULL;
}

/* format_double_literal: LLVM IR requires a floating-point constant to LOOK like one (a decimal
   point or exponent present) -- PARENA's own lexer just hands back the source text verbatim
   (`"60"` for a bare integer-looking literal used in F64 context), so a real, minimal reformat is
   needed here: append ".0" when no '.' is already present. Real, narrow, honest limitation: does
   not handle scientific notation or other real numeric edge cases -- not exercised by any real
   .prn source this v0 has been run against yet. */
static const char *format_double_literal(Arena *arena, const char *text) {
    if (strchr(text, '.') != NULL) return text;
    size_t len = strlen(text);
    char *out = arena_alloc(arena, len + 3);
    memcpy(out, text, len);
    out[len] = '.';
    out[len + 1] = '0';
    out[len + 2] = '\0';
    return out;
}

/* --- per-function emission state --- */

#define LLVM_MAX_LOCALS 16

typedef struct {
    const char *name;   /* mangled */
    const char *type;   /* llvm type */
} LlvmLocal;

typedef struct {
    LlvmBuf body;                     /* accumulated instruction lines */
    int next_reg;                     /* next fresh SSA register number */
    LlvmLocal locals[LLVM_MAX_LOCALS];
    size_t local_count;
} LlvmFn;

typedef struct {
    const char *name;      /* mangled */
    const char *ret_type;  /* llvm return type */
} LlvmFnSig;

#define LLVM_MAX_FNS 64

/* LlvmModule -- real, whole-compile-unit state, threaded through every recursive call instead of
   the two separate (sigs, sig_count) parameters this file started with (the real, minimal
   refactor String support below needed: string literals need a real, SHARED, module-level place
   to accumulate their own global constant declarations, the same real scope every LLVM global
   lives at, not per-function like everything else this emitter tracks). `strings`/`next_str` are
   real and new (2026-09-10, real String support); `sigs`/`sig_count` are the same real two-pass
   forward-reference table this file already had. */
typedef struct {
    LlvmFnSig *sigs;
    size_t sig_count;
    LlvmBuf strings;   /* accumulated `@.str.N = ...` global constant lines */
    int next_str;      /* next fresh string-global suffix */
} LlvmModule;

static const char *lookup_local_type(LlvmFn *fn, const char *mangled_name) {
    for (size_t i = 0; i < fn->local_count; i++) {
        if (strcmp(fn->locals[i].name, mangled_name) == 0) return fn->locals[i].type;
    }
    return NULL;
}

static const char *lookup_fn_ret_type(LlvmModule *mod, const char *mangled_name) {
    for (size_t i = 0; i < mod->sig_count; i++) {
        if (strcmp(mod->sigs[i].name, mangled_name) == 0) return mod->sigs[i].ret_type;
    }
    return NULL;
}

static const char *fresh_reg(Arena *arena, LlvmFn *fn) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%%%d", fn->next_reg++);
    return arena_strdup(arena, tmp, strlen(tmp));
}

/* LlvmVal -- one emitted expression's own real (type, value-reference) pair. `ref` is either a
   literal constant's own text (e.g. "60", "1.500000e+01", "true") or a real SSA register name
   (e.g. "%3") -- LLVM IR instructions accept either interchangeably in an operand position, no
   real distinction needed at the call sites below. */
typedef struct {
    const char *type;
    const char *ref;
} LlvmVal;

static LlvmVal emit_llvm_expr(Arena *arena, LlvmFn *fn, Node *expr, const char *expected_type,
                               LlvmModule *mod, const char **out_error);

/* BINOP_TABLE -- real, narrow arithmetic operator set. Each entry names the real LLVM opcode for
   an i32 operand and, separately, for a double operand (LLVM has genuinely distinct integer vs.
   float instructions, unlike C/TS/Java's own single shared `+`/`-`/`*`/`/` token) -- resolved by
   the operands' own real, bottom-up-computed type, not by any top-down hint. */
typedef struct { const char *prn_op; const char *i32_op; const char *f64_op; } ArithEntry;
static const ArithEntry ARITH_TABLE[] = {
    {"+", "add", "fadd"},
    {"-", "sub", "fsub"},
    {"*", "mul", "fmul"},
    {"/", "sdiv", "fdiv"},
};
#define ARITH_TABLE_COUNT (sizeof(ARITH_TABLE) / sizeof(ARITH_TABLE[0]))

/* CMP_TABLE -- real comparison operators. Every comparison's own real result type is `i1`
   regardless of its operands' type (the one real, structural place this emitter's type inference
   goes bottom-up rather than top-down propagating the operand type outward) -- `icmp`'s own
   real condition code for i32/i1 operands (signed, matching this v0's own I32 semantics) vs.
   `fcmp`'s own real ordered condition code for double operands (`o*` -- "ordered", real IEEE 754
   semantics for finite/NaN-free comparisons, the same real default choice a plain C `<`/`>`/`==`
   on `double` already makes). */
typedef struct { const char *prn_op; const char *icmp_cc; const char *fcmp_cc; } CmpEntry;
static const CmpEntry CMP_TABLE[] = {
    {"=", "eq", "oeq"},
    {"<", "slt", "olt"},
    {">", "sgt", "ogt"},
    {"<=", "sle", "ole"},
    {">=", "sge", "oge"},
};
#define CMP_TABLE_COUNT (sizeof(CMP_TABLE) / sizeof(CMP_TABLE[0]))

static const ArithEntry *find_arith(const char *prn_op) {
    for (size_t i = 0; i < ARITH_TABLE_COUNT; i++) {
        if (strcmp(ARITH_TABLE[i].prn_op, prn_op) == 0) return &ARITH_TABLE[i];
    }
    return NULL;
}

static const CmpEntry *find_cmp(const char *prn_op) {
    for (size_t i = 0; i < CMP_TABLE_COUNT; i++) {
        if (strcmp(CMP_TABLE[i].prn_op, prn_op) == 0) return &CMP_TABLE[i];
    }
    return NULL;
}

static LlvmVal llvm_val_err(void) {
    LlvmVal v;
    v.type = NULL;
    v.ref = NULL;
    return v;
}

/* emit_llvm_expr -- the real, recursive expression emitter. Unlike emit_java_expr/emit_ts_expr
   (which return a flat text expression), this returns a real (type, SSA-or-literal-ref) pair AND
   may append real instruction lines to `fn->body` along the way -- see emit_llvm.h's own header
   comment for the full real "why" behind this structural difference and this file's own
   top-down-for-literals-only / bottom-up-everywhere-else type-inference rule. */
static LlvmVal emit_llvm_expr(Arena *arena, LlvmFn *fn, Node *expr, const char *expected_type,
                               LlvmModule *mod, const char **out_error) {
    if (!expr) {
        *out_error = "emit_llvm: null expression";
        return llvm_val_err();
    }

    /* Real number literal -- the one real place `expected_type` (a top-down hint) is actually
       consulted, since PARENA's own lexer hands back undifferentiated numeric text with no type
       suffix. */
    if (expr->type == NODE_NUMBER) {
        if (!expected_type) {
            *out_error = "emit_llvm: numeric literal with no type context to disambiguate I32 vs F64";
            return llvm_val_err();
        }
        LlvmVal v;
        if (strcmp(expected_type, "double") == 0) {
            v.type = "double";
            v.ref = format_double_literal(arena, arena_strdup(arena, expr->text, expr->text_len));
        } else if (strcmp(expected_type, "i32") == 0) {
            /* Real, honest validation: an i32-typed literal's own source text must not already
               look like a float (a decimal point present) -- LLVM IR has no implicit float ->
               int truncation on a bare constant, so blindly emitting `i32 2.0` would be genuinely
               invalid IR. Caught here rather than handed to llc to fail on. */
            if (strchr(expr->text, '.') != NULL) {
                *out_error = "emit_llvm: numeric literal looks like a float but I32 was expected";
                return llvm_val_err();
            }
            v.type = "i32";
            v.ref = arena_strdup(arena, expr->text, expr->text_len);
        } else {
            *out_error = "emit_llvm: numeric literal used where a non-numeric type was expected";
            return llvm_val_err();
        }
        return v;
    }

    /* Real String literal support (2026-09-10) -- `expr->text`/`expr->text_len` are the real,
       already-unescaped raw bytes the lexer decoded (a real embedded newline byte, not the two
       characters `\` and `n` -- see emit.c's own NODE_STRING doc comment, which this file's own
       RE-escaping loop mirrors for LLVM's own real `c"...\XX..."` constant syntax instead of C's
       `"...\n..."`). Every byte outside printable ASCII, plus `"`/`\` themselves, is hex-escaped
       (`\XX`, two uppercase hex digits) -- LLVM's own real, exact requirement, stricter than C's
       (which allows named escapes like `\n`). A real, required trailing `\00` NUL terminator is
       appended, matching this repo's own already-established "NUL-terminated-C-string-shaped
       String" convention (see `hw/serial.prn`'s own doc comment).

       The literal itself becomes a real, private, module-level global constant
       (`@.str.N = private unnamed_addr constant [LEN x i8] c"...\00"`, accumulated into
       `mod->strings` rather than emitted inline -- LLVM globals live at module scope, never inside
       a function body). Real, load-bearing detail, verified live against real `llc 18`, not
       assumed from older typed-pointer-era LLVM IR examples: with LLVM's OPAQUE pointers (the
       default since LLVM 14+, definitely default in 18), a global array's own name IS already a
       plain `ptr` value -- no `getelementptr` decay/indexing instruction is needed the way older,
       typed-pointer LLVM IR required (`[N x i8]* -> i8*`). So this literal's own bottom-up type is
       simply `ptr`, and its `ref` is the global's own name, usable directly in any operand
       position exactly like an SSA register would be. */
    if (expr->type == NODE_STRING) {
        LlvmBuf escaped;
        lb_init(&escaped);
        for (size_t i = 0; i < expr->text_len; i++) {
            unsigned char c = (unsigned char)expr->text[i];
            if (c >= 32 && c < 127 && c != '"' && c != '\\') {
                char ch[2] = { (char)c, '\0' };
                lb_append(&escaped, ch);
            } else {
                char hex[4];
                snprintf(hex, sizeof(hex), "\\%02X", c);
                lb_append(&escaped, hex);
            }
        }
        char gname[32];
        snprintf(gname, sizeof(gname), "@.str.%d", mod->next_str++);
        lb_appendf(&mod->strings, "%s = private unnamed_addr constant [%zu x i8] c\"%s\\00\"\n",
                   gname, expr->text_len + 1, escaped.data);
        lb_free(&escaped);
        LlvmVal v;
        v.type = "ptr";
        v.ref = arena_strdup(arena, gname, strlen(gname));
        return v;
    }

    if (expr->type == NODE_SYMBOL) {
        /* true/false -- real Bool literals, same real recognition emit.c's own emit_expr already
           establishes for its own C target (checked before the generic local-variable-reference
           fallback below, same real ordering that file uses). */
        if (is_symbol(expr, "true")) {
            LlvmVal v; v.type = "i1"; v.ref = "true"; return v;
        }
        if (is_symbol(expr, "false")) {
            LlvmVal v; v.type = "i1"; v.ref = "false"; return v;
        }
        /* A plain parameter reference -- bottom-up type from this function's own real, declared
           parameter list (never from `expected_type`: a symbol's own real type is whatever it was
           declared as, not whatever the calling context hoped for -- a real mismatch here is
           caught by the binop/if/return type-check below, not silently coerced). */
        const char *mangled = mangle_name(arena, expr->text);
        const char *local_type = lookup_local_type(fn, mangled);
        if (!local_type) {
            *out_error = "emit_llvm: reference to an undeclared symbol (v0 has no let-bindings, only defn parameters)";
            return llvm_val_err();
        }
        LlvmVal v;
        v.type = local_type;
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%%%s", mangled);
        v.ref = arena_strdup(arena, tmp, strlen(tmp));
        return v;
    }

    if (expr->type != NODE_LIST || expr->child_count == 0 || expr->children[0]->type != NODE_SYMBOL) {
        *out_error = "emit_llvm: unsupported expression form (v0 only understands numbers, symbols, "
                     "true/false, not, binops, comparisons, if, and calls)";
        return llvm_val_err();
    }

    const char *head = expr->children[0]->text;

    /* if -- lowers to a real `select` instruction, not a branch/phi pair -- see emit_llvm.h's own
       header comment for the full real rationale. */
    if (strcmp(head, "if") == 0) {
        if (expr->child_count != 4) {
            *out_error = "emit_llvm: if requires exactly (if cond then else)";
            return llvm_val_err();
        }
        LlvmVal cond = emit_llvm_expr(arena, fn, expr->children[1], "i1", mod, out_error);
        if (!cond.ref) return llvm_val_err();
        if (strcmp(cond.type, "i1") != 0) {
            *out_error = "emit_llvm: if condition must be Bool (i1)";
            return llvm_val_err();
        }
        LlvmVal then_v = emit_llvm_expr(arena, fn, expr->children[2], expected_type, mod, out_error);
        if (!then_v.ref) return llvm_val_err();
        LlvmVal else_v = emit_llvm_expr(arena, fn, expr->children[3], then_v.type, mod, out_error);
        if (!else_v.ref) return llvm_val_err();
        if (strcmp(then_v.type, else_v.type) != 0) {
            *out_error = "emit_llvm: if branches have mismatched types";
            return llvm_val_err();
        }
        const char *reg = fresh_reg(arena, fn);
        lb_appendf(&fn->body, "  %s = select i1 %s, %s %s, %s %s\n",
                   reg, cond.ref, then_v.type, then_v.ref, then_v.type, else_v.ref);
        LlvmVal v;
        v.type = then_v.type;
        v.ref = reg;
        return v;
    }

    /* `(not x)` -- real, single-operand Bool negation. LLVM's own real idiom for boolean not is
       `xor i1 %x, true` -- there is no dedicated `not` opcode, matching every other backend's own
       real "not is xor/! against the type's own all-ones value" convention. */
    if (strcmp(head, "not") == 0) {
        if (expr->child_count != 2) {
            *out_error = "emit_llvm: not requires exactly 1 operand";
            return llvm_val_err();
        }
        LlvmVal inner = emit_llvm_expr(arena, fn, expr->children[1], "i1", mod, out_error);
        if (!inner.ref) return llvm_val_err();
        if (strcmp(inner.type, "i1") != 0) {
            *out_error = "emit_llvm: not requires a Bool (i1) operand";
            return llvm_val_err();
        }
        const char *reg = fresh_reg(arena, fn);
        lb_appendf(&fn->body, "  %s = xor i1 %s, true\n", reg, inner.ref);
        LlvmVal v;
        v.type = "i1";
        v.ref = reg;
        return v;
    }

    /* Real comparison operators -- own real i1 result type regardless of operand type (the one
       real bottom-up-only type rule; see this file's own ARITH/CMP table comments above). Both
       operands must share one real, mutually-inferred type: the lhs is emitted first (seeded with
       "i32" as this v0's own honest default operand-type guess when the lhs itself gives no
       better hint, e.g. a bare literal), then the rhs is emitted using the lhs's OWN real computed
       type as its own expected_type, so a literal rhs picks up the correct type to match. */
    const CmpEntry *cmp = find_cmp(head);
    if (cmp) {
        if (expr->child_count != 3) {
            *out_error = "emit_llvm: comparison operator requires exactly 2 operands";
            return llvm_val_err();
        }
        LlvmVal lhs = emit_llvm_expr(arena, fn, expr->children[1], "i32", mod, out_error);
        if (!lhs.ref) return llvm_val_err();
        LlvmVal rhs = emit_llvm_expr(arena, fn, expr->children[2], lhs.type, mod, out_error);
        if (!rhs.ref) return llvm_val_err();
        if (strcmp(lhs.type, rhs.type) != 0) {
            *out_error = "emit_llvm: comparison operands have mismatched types";
            return llvm_val_err();
        }
        const char *reg = fresh_reg(arena, fn);
        if (strcmp(lhs.type, "double") == 0) {
            lb_appendf(&fn->body, "  %s = fcmp %s double %s, %s\n", reg, cmp->fcmp_cc, lhs.ref, rhs.ref);
        } else {
            lb_appendf(&fn->body, "  %s = icmp %s %s %s, %s\n", reg, cmp->icmp_cc, lhs.type, lhs.ref, rhs.ref);
        }
        LlvmVal v;
        v.type = "i1";
        v.ref = reg;
        return v;
    }

    /* Real arithmetic operators -- result type = the (shared, checked) operand type, same real
       "seed lhs with i32, retype rhs from lhs's own real result" approach comparisons use above. */
    const ArithEntry *arith = find_arith(head);
    if (arith) {
        if (expr->child_count != 3) {
            *out_error = "emit_llvm: arithmetic operator requires exactly 2 operands (v0 has no variadic +)";
            return llvm_val_err();
        }
        LlvmVal lhs = emit_llvm_expr(arena, fn, expr->children[1], "i32", mod, out_error);
        if (!lhs.ref) return llvm_val_err();
        LlvmVal rhs = emit_llvm_expr(arena, fn, expr->children[2], lhs.type, mod, out_error);
        if (!rhs.ref) return llvm_val_err();
        if (strcmp(lhs.type, rhs.type) != 0) {
            *out_error = "emit_llvm: arithmetic operands have mismatched types";
            return llvm_val_err();
        }
        const char *opcode = (strcmp(lhs.type, "double") == 0) ? arith->f64_op : arith->i32_op;
        const char *reg = fresh_reg(arena, fn);
        lb_appendf(&fn->body, "  %s = %s %s %s, %s\n", reg, opcode, lhs.type, lhs.ref, rhs.ref);
        LlvmVal v;
        v.type = lhs.type;
        v.ref = reg;
        return v;
    }

    /* `and`/`or` -- real, honestly non-short-circuiting Bool operators. See emit_llvm.h's own
       header comment for the full real rationale on why this is a safe, observably-equivalent
       simplification for this v0's own pure, side-effect-free scalar scope. */
    if (strcmp(head, "and") == 0 || strcmp(head, "or") == 0) {
        if (expr->child_count != 3) {
            *out_error = "emit_llvm: and/or requires exactly 2 operands (v0 has no variadic and/or)";
            return llvm_val_err();
        }
        LlvmVal lhs = emit_llvm_expr(arena, fn, expr->children[1], "i1", mod, out_error);
        if (!lhs.ref) return llvm_val_err();
        if (strcmp(lhs.type, "i1") != 0) {
            *out_error = "emit_llvm: and/or requires Bool (i1) operands";
            return llvm_val_err();
        }
        LlvmVal rhs = emit_llvm_expr(arena, fn, expr->children[2], "i1", mod, out_error);
        if (!rhs.ref) return llvm_val_err();
        if (strcmp(rhs.type, "i1") != 0) {
            *out_error = "emit_llvm: and/or requires Bool (i1) operands";
            return llvm_val_err();
        }
        const char *opcode = (strcmp(head, "and") == 0) ? "and" : "or";
        const char *reg = fresh_reg(arena, fn);
        lb_appendf(&fn->body, "  %s = %s i1 %s, %s\n", reg, opcode, lhs.ref, rhs.ref);
        LlvmVal v;
        v.type = "i1";
        v.ref = reg;
        return v;
    }

    /* Otherwise: a real call to another top-level defn in the same module. Its own real return
       type comes from the sig table built by emit_llvm() below (bottom-up, independent of
       expected_type) -- see emit_llvm.h's own header comment on the real, named limitation that
       argument types are NOT independently re-verified against the callee's own declared
       parameter types in this v0. */
    const char *mangled_callee = mangle_name(arena, head);
    const char *ret_type = lookup_fn_ret_type(mod, mangled_callee);
    if (!ret_type) {
        *out_error = "emit_llvm: call to an unrecognized function (v0 has no external FFI/math-primitive table yet)";
        return llvm_val_err();
    }
    LlvmBuf arglist;
    lb_init(&arglist);
    for (size_t i = 1; i < expr->child_count; i++) {
        if (i > 1) lb_append(&arglist, ", ");
        /* Real, narrow v0 limitation named directly: each argument is emitted with NO expected
           type hint (NULL) unless it's a symbol/call (which compute their own type regardless) --
           a bare numeric literal argument would fail here with a real, honest "no type context"
           error rather than silently guessing. Not exercised by any real call site this v0 has
           been run against yet (every real call site passes symbol/call arguments, never a bare
           literal). */
        LlvmVal arg = emit_llvm_expr(arena, fn, expr->children[i], NULL, mod, out_error);
        if (!arg.ref) {
            lb_free(&arglist);
            return llvm_val_err();
        }
        lb_appendf(&arglist, "%s %s", arg.type, arg.ref);
    }
    const char *reg = NULL;
    if (strcmp(ret_type, "void") == 0) {
        lb_appendf(&fn->body, "  call void @%s(%s)\n", mangled_callee, arglist.data);
    } else {
        reg = fresh_reg(arena, fn);
        lb_appendf(&fn->body, "  %s = call %s @%s(%s)\n", reg, ret_type, mangled_callee, arglist.data);
    }
    lb_free(&arglist);
    LlvmVal v;
    v.type = ret_type;
    v.ref = reg ? reg : "undef"; /* void-returning calls used in value position: real, honest gap, not reached by any current real .prn source */
    return v;
}

/* emit_llvm_defn -- one top-level (defn name [(param : Type) ...] : RetType body) -> one real
   `define <ret_type> @<name>(<params>) { entry: ...instructions... ret <ret_type> <value> }`
   function definition, appended into `out`. */
static int emit_llvm_defn(Arena *arena, LlvmBuf *out, Node *defn, LlvmModule *mod,
                           const char **out_error) {
    if (defn->child_count < 3 || defn->children[1]->type != NODE_SYMBOL || defn->children[2]->type != NODE_VEC) {
        *out_error = "emit_llvm: defn: malformed function definition";
        return 0;
    }
    const char *fn_name = mangle_name(arena, defn->children[1]->text);
    Node *params = defn->children[2];

    LlvmFn fn;
    lb_init(&fn.body);
    fn.next_reg = 0;
    fn.local_count = 0;

    LlvmBuf param_list;
    lb_init(&param_list);
    for (size_t i = 0; i < params->child_count; i++) {
        Node *param = params->children[i];
        if (param->type != NODE_LIST || param->child_count != 3 || param->children[0]->type != NODE_SYMBOL ||
            param->children[1]->type != NODE_COLON || param->children[2]->type != NODE_SYMBOL) {
            *out_error = "emit_llvm: defn: unsupported parameter shape (v0 only understands plain "
                         "(name : I32|F64|Bool) params -- no Arena/region annotations)";
            lb_free(&param_list);
            lb_free(&fn.body);
            return 0;
        }
        const char *p_type = resolve_llvm_type(param->children[2], out_error);
        if (!p_type) {
            lb_free(&param_list);
            lb_free(&fn.body);
            return 0;
        }
        const char *p_mangled = mangle_name(arena, param->children[0]->text);
        if (fn.local_count >= LLVM_MAX_LOCALS) {
            *out_error = "emit_llvm: defn: too many parameters (v0 has a fixed, small real limit)";
            lb_free(&param_list);
            lb_free(&fn.body);
            return 0;
        }
        fn.locals[fn.local_count].name = p_mangled;
        fn.locals[fn.local_count].type = p_type;
        fn.local_count++;
        if (i > 0) lb_append(&param_list, ", ");
        lb_appendf(&param_list, "%s %%%s", p_type, p_mangled);
    }

    if (defn->child_count != 6 || defn->children[3]->type != NODE_COLON) {
        *out_error = "emit_llvm: defn: expected (defn name [params] : RetType body) with exactly one body expression";
        lb_free(&param_list);
        lb_free(&fn.body);
        return 0;
    }
    const char *ret_type = resolve_llvm_type(defn->children[4], out_error);
    if (!ret_type) {
        lb_free(&param_list);
        lb_free(&fn.body);
        return 0;
    }
    LlvmVal body_val = emit_llvm_expr(arena, &fn, defn->children[5], ret_type, mod, out_error);
    if (!body_val.ref) {
        lb_free(&param_list);
        lb_free(&fn.body);
        return 0;
    }
    if (strcmp(body_val.type, ret_type) != 0) {
        *out_error = "emit_llvm: defn: body's own real, inferred type does not match the declared return type";
        lb_free(&param_list);
        lb_free(&fn.body);
        return 0;
    }

    lb_appendf(out, "define %s @%s(%s) {\nentry:\n%s  ret %s %s\n}\n\n",
               ret_type, fn_name, param_list.data, fn.body.data, ret_type, body_val.ref);
    lb_free(&param_list);
    lb_free(&fn.body);
    return 1;
}

const char *emit_llvm(Arena *arena, Node *program, const char **out_error) {
    /* Real, first pass: collect every top-level defn's own real, declared return type BEFORE
       emitting any body, so a call to a defn declared LATER in the same file (or a mutually
       recursive pair) still resolves correctly -- the same real two-pass shape a proper compiler
       needs for forward references, not attempted by emit_java.c/emit_ts.c (whose text-based
       target languages don't need a caller to know a callee's type ahead of time). */
    LlvmFnSig sigs[LLVM_MAX_FNS];
    size_t sig_count = 0;
    for (size_t i = 0; i < program->child_count; i++) {
        Node *form = program->children[i];
        if (!is_call_named(form, "defn")) continue;
        if (form->child_count != 6 || form->children[1]->type != NODE_SYMBOL || form->children[3]->type != NODE_COLON) {
            continue; /* real, honest: a malformed defn is reported properly by emit_llvm_defn's own second pass below, not here */
        }
        const char *ret_type_err = NULL;
        const char *ret_type = resolve_llvm_type(form->children[4], &ret_type_err);
        if (!ret_type) continue; /* same real reasoning -- reported properly below */
        if (sig_count >= LLVM_MAX_FNS) {
            *out_error = "emit_llvm: too many top-level defns (v0 has a fixed, small real limit)";
            return NULL;
        }
        sigs[sig_count].name = mangle_name(arena, form->children[1]->text);
        sigs[sig_count].ret_type = ret_type;
        sig_count++;
    }

    LlvmModule mod;
    mod.sigs = sigs;
    mod.sig_count = sig_count;
    lb_init(&mod.strings);
    mod.next_str = 0;

    /* `out` accumulates only the real function definitions -- the header comment and any real
       string-global constants are prepended once, after this loop, into `final_out` below (needs
       to happen after, since string literals are only discovered while emitting bodies). */
    LlvmBuf out;
    lb_init(&out);

    for (size_t i = 0; i < program->child_count; i++) {
        Node *form = program->children[i];
        /* (module ...) / (export ...) / (import ...) are real, no-op metadata for this v0, same
           real precedent every other emitter's own top-level dispatch already establishes. */
        if (is_call_named(form, "module") || is_call_named(form, "export") || is_call_named(form, "import")) {
            continue;
        }
        if (is_call_named(form, "defn")) {
            if (!emit_llvm_defn(arena, &out, form, &mod, out_error)) {
                lb_free(&mod.strings);
                lb_free(&out);
                return NULL;
            }
            continue;
        }
        *out_error = "emit_llvm: unsupported top-level form (v0 only understands defn, module, export, import)";
        lb_free(&mod.strings);
        lb_free(&out);
        return NULL;
    }

    /* Real string-global constants (if any were collected above) go BEFORE the function
       definitions -- LLVM IR doesn't strictly require this ordering (globals are resolved
       module-wide regardless of declaration order), but it's the real, conventional, more
       readable placement every real-world .ll file uses. Only discoverable AFTER the loop above
       (string literals are found while emitting bodies), so the header + strings + function defs
       are assembled here, in that final real order. */
    LlvmBuf final_out;
    lb_init(&final_out);
    lb_append(&final_out, "; Generated by parena build (LLVM target) -- VS0-for-LLVM v0, do not edit by hand.\n\n");
    if (mod.strings.len > 0) {
        lb_append(&final_out, mod.strings.data);
        lb_append(&final_out, "\n");
    }
    lb_append(&final_out, out.data);

    const char *result = arena_strdup(arena, final_out.data, final_out.len);
    lb_free(&mod.strings);
    lb_free(&out);
    lb_free(&final_out);
    return result;
}
