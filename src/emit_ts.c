/* emit_ts.c — real, v0, narrow-scope TypeScript emitter. See emit_ts.h's own header comment for
 * the full real scope statement (a scalar `defn`, no Arena/region, one expression body). */
#include "emit_ts.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* --- minimal, self-contained string builder -- deliberately NOT shared with emit.c's own
   StrBuf (see emit_ts.h's own header comment for why this file stays independent). Working
   buffer is plain malloc/realloc, freed once the final string has been arena_strdup'd into the
   caller's own Arena -- the same "temporary C-heap scratch, arena-owned result" shape emit_c's
   own top-level emit_c() uses for its own final StrBuf. */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} TsBuf;

static void tb_init(TsBuf *b) {
    b->cap = 256;
    b->data = malloc(b->cap);
    b->data[0] = '\0';
    b->len = 0;
}

static void tb_free(TsBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void tb_append(TsBuf *b, const char *s) {
    size_t add_len = strlen(s);
    if (b->len + add_len + 1 > b->cap) {
        while (b->len + add_len + 1 > b->cap) b->cap *= 2;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, add_len + 1);
    b->len += add_len;
}

static void tb_appendf(TsBuf *b, const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    tb_append(b, tmp);
}

/* --- small AST helpers, same real shape emit.c's own is_symbol/is_call_named use, reimplemented
   independently here rather than exported/shared (see emit_ts.h's own header comment). */
static int is_symbol(Node *n, const char *text) {
    return n && n->type == NODE_SYMBOL && strcmp(n->text, text) == 0;
}

static int is_call_named(Node *n, const char *name) {
    return n && n->type == NODE_LIST && n->child_count >= 1 && is_symbol(n->children[0], name);
}

/* camel_case: real, minimal kebab-case -> camelCase converter -- "bezier-interp" ->
   "bezierInterp", "on-papercraft-item-for-object-destroyed" -> "onPapercraftItemForObjectDestroyed".
   TypeScript's own real naming convention, matching what a human TS author writing this by hand
   would actually name it, not a mechanical 1:1 transliteration of the C emitter's own snake_case
   `mangle()` (a real, deliberate per-target-language choice, not an oversight). */
static const char *camel_case(Arena *arena, const char *kebab) {
    size_t len = strlen(kebab);
    char *out = arena_alloc(arena, len + 1);
    size_t oi = 0;
    int capitalize_next = 0;
    for (size_t i = 0; i < len; i++) {
        char c = kebab[i];
        if (c == '-') {
            capitalize_next = 1;
            continue;
        }
        if (capitalize_next) {
            out[oi++] = (char)((c >= 'a' && c <= 'z') ? c - 32 : c);
            capitalize_next = 0;
        } else {
            out[oi++] = c;
        }
    }
    out[oi] = '\0';
    return out;
}

/* resolve_ts_type: the real, narrow I32/F64/Bool/String/Unit -> TypeScript type mapping this v0
   understands -- any other type name (a registered defstruct/defenum, a Result/Option, Arena)
   is a real, honest "unsupported" error, matching every other real, narrow-scope boundary this
   file draws elsewhere. */
static const char *resolve_ts_type(Node *type_sym, const char **out_error) {
    if (!type_sym || type_sym->type != NODE_SYMBOL) {
        *out_error = "emit_ts: expected a type symbol";
        return NULL;
    }
    if (strcmp(type_sym->text, "I32") == 0 || strcmp(type_sym->text, "F64") == 0) return "number";
    if (strcmp(type_sym->text, "Bool") == 0) return "boolean";
    if (strcmp(type_sym->text, "String") == 0) return "string";
    if (strcmp(type_sym->text, "Unit") == 0) return "void";
    *out_error = "emit_ts: unsupported parameter/return type (v0 only understands I32/F64/Bool/String/Unit)";
    return NULL;
}

/* BINOP_TABLE -- the real, narrow arithmetic/comparison/logical operator set this v0 recognizes,
   same real set the C emitter's own binop dispatch understands for this exact 2-operand shape
   (see PAPERCRAFT's own xp_award_mod.prn/item_drop_mod.prn/inventory_mod.prn for the real,
   already-proven call sites this mirrors). `=` -> `===`/`and` -> `&&`/`or` -> `||` are the real,
   deliberate TypeScript-idiomatic mappings, not the PARENA/C operator token reused verbatim. */
typedef struct { const char *prn_op; const char *ts_op; } BinopEntry;
static const BinopEntry BINOP_TABLE[] = {
    {"+", "+"}, {"-", "-"}, {"*", "*"}, {"/", "/"},
    {"=", "==="}, {"<", "<"}, {">", ">"}, {"<=", "<="}, {">=", ">="},
    {"and", "&&"}, {"or", "||"},
};
#define BINOP_TABLE_COUNT (sizeof(BINOP_TABLE) / sizeof(BINOP_TABLE[0]))

static const char *find_binop(const char *prn_op) {
    for (size_t i = 0; i < BINOP_TABLE_COUNT; i++) {
        if (strcmp(BINOP_TABLE[i].prn_op, prn_op) == 0) return BINOP_TABLE[i].ts_op;
    }
    return NULL;
}

static const char *emit_ts_expr(Arena *arena, Node *expr, const char **out_error);

/* emit_ts_expr: the real, recursive expression emitter -- number/symbol literals, the narrow
   binop set above, `if` as a ternary expression (no statement-level `if`/`let`/block support in
   this v0 -- a `defn` body is exactly one real expression, matching xp_award_mod.prn's own
   already-proven shape), a call to the one real, recognized external primitive this v0 knows
   (`math/random` -> `Math.random()`, see stdlib/math/random.prn's own doc comment), or a call to
   another top-level function defined in the same file (camelCased, matching this file's own
   emitted defn names). */
static const char *emit_ts_expr(Arena *arena, Node *expr, const char **out_error) {
    if (!expr) {
        *out_error = "emit_ts: null expression";
        return NULL;
    }

    if (expr->type == NODE_NUMBER) {
        return arena_strdup(arena, expr->text, expr->text_len);
    }

    if (expr->type == NODE_SYMBOL) {
        return camel_case(arena, expr->text);
    }

    if (expr->type != NODE_LIST || expr->child_count == 0 || expr->children[0]->type != NODE_SYMBOL) {
        *out_error = "emit_ts: unsupported expression form (v0 only understands numbers, symbols, "
                     "binops, if, and calls)";
        return NULL;
    }

    const char *head = expr->children[0]->text;

    /* if -- real ternary, the one real control-flow form this v0 understands. */
    if (strcmp(head, "if") == 0) {
        if (expr->child_count != 4) {
            *out_error = "emit_ts: if requires exactly (if cond then else)";
            return NULL;
        }
        const char *cond = emit_ts_expr(arena, expr->children[1], out_error);
        if (!cond) return NULL;
        const char *then_e = emit_ts_expr(arena, expr->children[2], out_error);
        if (!then_e) return NULL;
        const char *else_e = emit_ts_expr(arena, expr->children[3], out_error);
        if (!else_e) return NULL;
        TsBuf b;
        tb_init(&b);
        tb_appendf(&b, "(%s ? %s : %s)", cond, then_e, else_e);
        const char *result = arena_strdup(arena, b.data, b.len);
        tb_free(&b);
        return result;
    }

    /* real, narrow binop set -- exactly 2 operands, matching every real call site this mirrors. */
    const char *ts_op = find_binop(head);
    if (ts_op) {
        if (expr->child_count != 3) {
            *out_error = "emit_ts: binary operator requires exactly 2 operands (v0 has no variadic +/and/or)";
            return NULL;
        }
        const char *lhs = emit_ts_expr(arena, expr->children[1], out_error);
        if (!lhs) return NULL;
        const char *rhs = emit_ts_expr(arena, expr->children[2], out_error);
        if (!rhs) return NULL;
        TsBuf b;
        tb_init(&b);
        tb_appendf(&b, "(%s %s %s)", lhs, ts_op, rhs);
        const char *result = arena_strdup(arena, b.data, b.len);
        tb_free(&b);
        return result;
    }

    /* math/random -- the one real, recognized external primitive this v0 lowers directly to a
       real host call, same real "FFI-shaped gap, explicitly named, not silently guessed" reasoning
       every other stdlib package with a real host dependency already documents (net/tcp, the
       crypto packages, sdl2). Zero args, matching stdlib/math/random.prn's own real signature. */
    if (strcmp(head, "math/random") == 0) {
        if (expr->child_count != 1) {
            *out_error = "emit_ts: math/random takes no arguments";
            return NULL;
        }
        return "Math.random()";
    }

    /* Otherwise: a real call to another top-level defn in the same generated file. */
    TsBuf b;
    tb_init(&b);
    tb_appendf(&b, "%s(", camel_case(arena, head));
    for (size_t i = 1; i < expr->child_count; i++) {
        if (i > 1) tb_append(&b, ", ");
        const char *arg = emit_ts_expr(arena, expr->children[i], out_error);
        if (!arg) {
            tb_free(&b);
            return NULL;
        }
        tb_append(&b, arg);
    }
    tb_append(&b, ")");
    const char *result = arena_strdup(arena, b.data, b.len);
    tb_free(&b);
    return result;
}

/* emit_ts_defn: one top-level (defn name [(param : Type) ...] : RetType body) -> one exported
   TypeScript function. Real, narrow scope: every parameter must be a plain, non-region-annotated
   I32/F64/Bool/String (resolve_ts_type's own real, honest boundary) -- an Arena/region-annotated
   parameter (the C emitter's own real bread and butter) is a real, honest "unsupported" error
   here, not silently dropped, since TypeScript's own garbage collector makes the whole concept a
   real no-op for this target, not something to approximate. */
static int emit_ts_defn(Arena *arena, TsBuf *out, Node *defn, const char **out_error) {
    if (defn->child_count < 3 || defn->children[1]->type != NODE_SYMBOL || defn->children[2]->type != NODE_VEC) {
        *out_error = "emit_ts: defn: malformed function definition";
        return 0;
    }
    const char *fn_name = camel_case(arena, defn->children[1]->text);
    Node *params = defn->children[2];

    TsBuf param_list;
    tb_init(&param_list);
    for (size_t i = 0; i < params->child_count; i++) {
        Node *param = params->children[i];
        if (param->type != NODE_LIST || param->child_count != 3 || param->children[0]->type != NODE_SYMBOL ||
            param->children[1]->type != NODE_COLON || param->children[2]->type != NODE_SYMBOL) {
            *out_error = "emit_ts: defn: unsupported parameter shape (v0 only understands plain "
                         "(name : I32|F64|Bool|String) params -- no Arena/region annotations)";
            tb_free(&param_list);
            return 0;
        }
        const char *p_type = resolve_ts_type(param->children[2], out_error);
        if (!p_type) {
            tb_free(&param_list);
            return 0;
        }
        if (i > 0) tb_append(&param_list, ", ");
        tb_appendf(&param_list, "%s: %s", camel_case(arena, param->children[0]->text), p_type);
    }

    /* Return type + body: `(defn name [params] : RetType body)` is 5 children total (defn, name,
       params, colon, rettype) plus the body as a 6th -- OR, matching xp_award_mod.prn's own real
       zero-arg-with-inline-body shape, the body may be the 6th child directly. Real, narrow: this
       v0 only accepts EXACTLY one body expression (no implicit `do`), matching every real target
       shape this file is proven against so far. */
    if (defn->child_count != 6 || defn->children[3]->type != NODE_COLON) {
        *out_error = "emit_ts: defn: expected (defn name [params] : RetType body) with exactly one body expression";
        tb_free(&param_list);
        return 0;
    }
    const char *ret_type = resolve_ts_type(defn->children[4], out_error);
    if (!ret_type) {
        tb_free(&param_list);
        return 0;
    }
    const char *body = emit_ts_expr(arena, defn->children[5], out_error);
    if (!body) {
        tb_free(&param_list);
        return 0;
    }

    tb_appendf(out, "export function %s(%s): %s {\n    return %s;\n}\n\n", fn_name, param_list.data, ret_type, body);
    tb_free(&param_list);
    return 1;
}

const char *emit_ts(Arena *arena, Node *program, const char **out_error) {
    TsBuf out;
    tb_init(&out);
    tb_append(&out, "// Generated by parena build (TypeScript target) -- VS0-for-TS v0, do not edit by hand.\n\n");

    for (size_t i = 0; i < program->child_count; i++) {
        Node *form = program->children[i];
        /* (module ...) / (export ...) / (import ...) are real, no-op metadata for this v0, same
           real precedent cmd_build's own header comment already establishes for the C build path
           -- every top-level defn is exported unconditionally (a single generated module file),
           so a real (export name) list doesn't change what actually gets emitted. */
        if (is_call_named(form, "module") || is_call_named(form, "export") || is_call_named(form, "import")) {
            continue;
        }
        if (is_call_named(form, "defn")) {
            if (!emit_ts_defn(arena, &out, form, out_error)) {
                tb_free(&out);
                return NULL;
            }
            continue;
        }
        *out_error = "emit_ts: unsupported top-level form (v0 only understands defn, module, export, import)";
        tb_free(&out);
        return NULL;
    }

    const char *result = arena_strdup(arena, out.data, out.len);
    tb_free(&out);
    return result;
}
