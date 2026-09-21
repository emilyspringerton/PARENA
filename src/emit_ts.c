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

/* Real bug, found live dogfooding this emitter against DEADWEIGHT's card_rules.prn: a fixed
 * 512-byte stack buffer here silently truncated any single format call whose result exceeded 511
 * characters -- exactly the same bug already found and fixed in emit_java.c's own jb_appendf
 * (PARENA 8141f8f, DEADWEIGHT's own README still documents it as "the Java emitter used to
 * truncate expressions longer than 511 characters"). card_rules.prn's deeply nested ternary
 * chains (fxaLo/fxbLo/... card-effect lookups) blow past 511 characters routinely -- silently
 * truncated output then got concatenated with whatever the next emitted function happened to be,
 * producing syntactically-broken TypeScript that still "succeeded" (no error, no crash). Same
 * two-pass vsnprintf fix as jb_appendf: measure first, allocate exactly enough, then format. */
static void tb_appendf(TsBuf *b, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return; }
    char *tmp = malloc((size_t)n + 1);
    if (!tmp) { va_end(ap2); return; }
    vsnprintf(tmp, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    tb_append(b, tmp);
    free(tmp);
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

/* MATH_PRIM_TABLE -- the real, recognized external math/ primitives this v0 lowers directly
   to a real host `Math.*` call, same real "FFI-shaped gap, explicitly named, not silently
   guessed" reasoning every other stdlib package with a real host dependency already documents
   (net/tcp, the crypto packages, sdl2). Grown from the original single `math/random` entry
   (2026-08-30, founder real-time: "continue rewriting MISHRI using parena using parena mods") to
   cover the real, additional primitives MISHRI's own `HumannessLayer.randInt`/`addNoise` need --
   see stdlib/math/math.prn's own doc comment for each real, matching PARENA-side signature. */
typedef struct { const char *prn_name; const char *ts_fn; int arg_count; } MathPrimEntry;
static const MathPrimEntry MATH_PRIM_TABLE[] = {
    /* renamed math/random -> math/random-f64 2026-09-07 (stdlib/math/math.prn's own doc comment
     * has the real reason -- a genuine C-target naming collision with glibc's `random()`, found
     * live the first time the C target ever actually compiled this function for real). */
    {"math/random-f64", "Math.random", 0},
    {"math/floor", "Math.floor", 1},
    {"math/sqrt", "Math.sqrt", 1},
    {"math/log", "Math.log", 1},
    {"math/cos", "Math.cos", 1},
};
#define MATH_PRIM_TABLE_COUNT (sizeof(MATH_PRIM_TABLE) / sizeof(MATH_PRIM_TABLE[0]))

static const MathPrimEntry *find_math_prim(const char *name) {
    for (size_t i = 0; i < MATH_PRIM_TABLE_COUNT; i++) {
        if (strcmp(MATH_PRIM_TABLE[i].prn_name, name) == 0) return &MATH_PRIM_TABLE[i];
    }
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

/* FnSig / ParamEntry -- the real, minimal type table this v0 now carries so `/` can be emitted
 * correctly. Found live, dogfooding the TS target against DEADWEIGHT's card_rules.prn (all-I32):
 * the emitter used to lower `/` straight to TypeScript's `/`, which is always real-number
 * division -- correct for F64 (bezier_interp.prn's own real, live use), silently WRONG for I32
 * (C's and Java's `/` both truncate toward zero; `card_rules.prn`'s packed-bitfield decode chain
 * -- `(/ id 16)` etc. -- depends on that truncation for every single card lookup). Fixing this
 * needs to know, per-expression, whether it's I32 or F64, since TypeScript's own `number` erases
 * the distinction PARENA's type system draws. Two-pass: collect every defn's declared return
 * type first (so forward/out-of-order calls resolve), then thread the current defn's own param
 * types through the expression walk. */
typedef struct { const char *name; const char *ret_type; } FnSig;
typedef struct { const char *name; const char *type; } ParamEntry;

typedef struct {
    FnSig *fns;
    size_t fn_count;
    ParamEntry *params;
    size_t param_count;
} TsTypeCtx;

static const char *lookup_fn_ret_type(const TsTypeCtx *ctx, const char *name) {
    for (size_t i = 0; i < ctx->fn_count; i++) {
        if (strcmp(ctx->fns[i].name, name) == 0) return ctx->fns[i].ret_type;
    }
    return NULL;
}

static const char *lookup_param_type(const TsTypeCtx *ctx, const char *name) {
    for (size_t i = 0; i < ctx->param_count; i++) {
        if (strcmp(ctx->params[i].name, name) == 0) return ctx->params[i].type;
    }
    return NULL;
}

static const char *emit_ts_expr(Arena *arena, Node *expr, const TsTypeCtx *ctx, const char **out_error,
                                 const char **out_type);

/* emit_ts_expr: the real, recursive expression emitter -- number/symbol literals, the narrow
   binop set above, `if` as a ternary expression (no statement-level `if`/`let`/block support in
   this v0 -- a `defn` body is exactly one real expression, matching xp_award_mod.prn's own
   already-proven shape), a call to the one real, recognized external primitive this v0 knows
   (`math/random` -> `Math.random()`, see stdlib/math/random.prn's own doc comment), or a call to
   another top-level function defined in the same file (camelCased, matching this file's own
   emitted defn names). */
static const char *emit_ts_expr(Arena *arena, Node *expr, const TsTypeCtx *ctx, const char **out_error,
                                 const char **out_type) {
    if (!expr) {
        *out_error = "emit_ts: null expression";
        return NULL;
    }

    if (expr->type == NODE_NUMBER) {
        /* A literal with a '.' in its source text is F64 (e.g. "3.0"); a bare integer literal
           (e.g. "16") is I32 -- matches how the C/Java targets' own native integer/double literal
           distinction is driven by the same source syntax. */
        *out_type = memchr(expr->text, '.', expr->text_len) ? "F64" : "I32";
        return arena_strdup(arena, expr->text, expr->text_len);
    }

    if (expr->type == NODE_SYMBOL) {
        /* math/pi -- the one real, recognized external CONSTANT this v0 knows (distinct from the
           MATH_PRIM_TABLE calls above -- a bare symbol reference, not a call), lowered directly
           to Math.PI. Checked before the generic camel_case fallback so it isn't mistaken for a
           local parameter reference. */
        if (strcmp(expr->text, "math/pi") == 0) {
            *out_type = "F64";
            return "Math.PI";
        }
        /* true/false -- a real, latent gap found live writing fx_rules.prn's own fx-is-crit
           (2026-09-21): the C emitter has recognized these literals since "firefly.prn's own
           real (set! (get-field !t :failed) true)" (see emit.c's own comment), but this target
           never had one until now. Without this, "true"/"false" fell through to the generic
           param-lookup fallback below -- it happened to still emit valid-looking TypeScript
           (camel_case("true") == "true", a coincidence: TS's own `true` keyword matches), but
           *out_type was wrongly reported "I32" instead of "boolean", a real latent type-tracking
           bug waiting to corrupt a future `/` truncation decision the moment a bool literal ever
           reached one. */
        if (strcmp(expr->text, "true") == 0 || strcmp(expr->text, "false") == 0) {
            *out_type = "boolean";
            return expr->text;
        }
        const char *p_type = lookup_param_type(ctx, expr->text);
        *out_type = p_type ? p_type : "I32"; /* only reachable for a real param in this v0's grammar */
        return camel_case(arena, expr->text);
    }

    if (expr->type != NODE_LIST || expr->child_count == 0 || expr->children[0]->type != NODE_SYMBOL) {
        *out_error = "emit_ts: unsupported expression form (v0 only understands numbers, symbols, "
                     "binops, if, and calls)";
        return NULL;
    }

    const char *head = expr->children[0]->text;

    /* if -- real ternary, the one real control-flow form this v0 understands. Result type is the
       then-branch's type (PARENA's own type checker already guarantees both branches agree; this
       v0 does not re-verify that here, same as it doesn't re-verify param arity elsewhere). */
    if (strcmp(head, "if") == 0) {
        if (expr->child_count != 4) {
            *out_error = "emit_ts: if requires exactly (if cond then else)";
            return NULL;
        }
        const char *cond_type;
        const char *cond = emit_ts_expr(arena, expr->children[1], ctx, out_error, &cond_type);
        if (!cond) return NULL;
        const char *then_type;
        const char *then_e = emit_ts_expr(arena, expr->children[2], ctx, out_error, &then_type);
        if (!then_e) return NULL;
        const char *else_type;
        const char *else_e = emit_ts_expr(arena, expr->children[3], ctx, out_error, &else_type);
        if (!else_e) return NULL;
        *out_type = then_type;
        TsBuf b;
        tb_init(&b);
        tb_appendf(&b, "(%s ? %s : %s)", cond, then_e, else_e);
        const char *result = arena_strdup(arena, b.data, b.len);
        tb_free(&b);
        return result;
    }

    /* `(not x)` -- real, genuine gap found live dogfooding this emitter against
       card_rules.prn's own fx-cond-ok (2026-09-21) -- the SAME real gap already found and fixed
       for src/emit.c (2026-08-21), src/emit_java.c (SPIDERBEETLE, 2026-08-30's own kanban card
       32445324) and BURROW/emit_c.go+emit_go.go, this target had simply never hit a real .prn
       using `not` yet either. `not` is a real, distinct 1-argument form, not a 2-argument binop,
       so without this it fell through into the generic call fallback below, mangling into a
       bogus call to a never-defined TypeScript function `not(...)`. TypeScript's `!` is the exact
       real equivalent. */
    if (strcmp(head, "not") == 0) {
        if (expr->child_count != 2) {
            *out_error = "emit_ts: not requires exactly 1 operand";
            return NULL;
        }
        const char *inner_type;
        const char *inner = emit_ts_expr(arena, expr->children[1], ctx, out_error, &inner_type);
        if (!inner) return NULL;
        *out_type = "boolean";
        TsBuf b;
        tb_init(&b);
        tb_appendf(&b, "(!(%s))", inner);
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
        const char *lhs_type;
        const char *lhs = emit_ts_expr(arena, expr->children[1], ctx, out_error, &lhs_type);
        if (!lhs) return NULL;
        const char *rhs_type;
        const char *rhs = emit_ts_expr(arena, expr->children[2], ctx, out_error, &rhs_type);
        if (!rhs) return NULL;
        int is_i32_pair = strcmp(lhs_type, "I32") == 0 && strcmp(rhs_type, "I32") == 0;
        TsBuf b;
        tb_init(&b);
        if (strcmp(head, "/") == 0 && is_i32_pair) {
            /* Real bug, found live dogfooding this emitter against DEADWEIGHT's card_rules.prn:
               TypeScript's `/` is always real-number division, but I32/I32 division must truncate
               toward zero to match the C and Java targets compiled from the exact same .prn --
               card_rules.prn's packed-bitfield decode chain (id/16, x/3, x/6, ...) is silently
               wrong without this. Math.trunc matches C/Java's toward-zero truncation exactly
               (unlike Math.floor, which rounds toward -Infinity and disagrees on negative inputs). */
            tb_appendf(&b, "Math.trunc(%s / %s)", lhs, rhs);
            *out_type = "I32";
        } else {
            tb_appendf(&b, "(%s %s %s)", lhs, ts_op, rhs);
            *out_type = (strcmp(head, "=") == 0 || strcmp(head, "<") == 0 || strcmp(head, ">") == 0 ||
                         strcmp(head, "<=") == 0 || strcmp(head, ">=") == 0 || strcmp(head, "and") == 0 ||
                         strcmp(head, "or") == 0)
                            ? "boolean"
                            : (is_i32_pair ? "I32" : "F64");
        }
        const char *result = arena_strdup(arena, b.data, b.len);
        tb_free(&b);
        return result;
    }

    /* Real, recognized external math primitives -- the table above. Every real entry today
       returns F64 (Math.random/floor/sqrt/log/cos all do in TypeScript). */
    const MathPrimEntry *math_prim = find_math_prim(head);
    if (math_prim) {
        size_t got_args = expr->child_count - 1;
        if ((int)got_args != math_prim->arg_count) {
            *out_error = "emit_ts: math primitive called with the wrong number of arguments";
            return NULL;
        }
        TsBuf b;
        tb_init(&b);
        tb_appendf(&b, "%s(", math_prim->ts_fn);
        for (size_t i = 1; i < expr->child_count; i++) {
            if (i > 1) tb_append(&b, ", ");
            const char *arg_type;
            const char *arg = emit_ts_expr(arena, expr->children[i], ctx, out_error, &arg_type);
            if (!arg) {
                tb_free(&b);
                return NULL;
            }
            tb_append(&b, arg);
        }
        tb_append(&b, ")");
        *out_type = "F64";
        const char *result = arena_strdup(arena, b.data, b.len);
        tb_free(&b);
        return result;
    }

    /* Otherwise: a real call to another top-level defn in the same generated file. */
    const char *callee_ret = lookup_fn_ret_type(ctx, head);
    TsBuf b;
    tb_init(&b);
    tb_appendf(&b, "%s(", camel_case(arena, head));
    for (size_t i = 1; i < expr->child_count; i++) {
        if (i > 1) tb_append(&b, ", ");
        const char *arg_type;
        const char *arg = emit_ts_expr(arena, expr->children[i], ctx, out_error, &arg_type);
        if (!arg) {
            tb_free(&b);
            return NULL;
        }
        tb_append(&b, arg);
    }
    tb_append(&b, ")");
    *out_type = callee_ret ? callee_ret : "I32"; /* unknown callee: matches this v0's prior no-check behavior */
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
static int emit_ts_defn(Arena *arena, TsBuf *out, Node *defn, const TsTypeCtx *fn_ctx, const char **out_error) {
    if (defn->child_count < 3 || defn->children[1]->type != NODE_SYMBOL || defn->children[2]->type != NODE_VEC) {
        *out_error = "emit_ts: defn: malformed function definition";
        return 0;
    }
    const char *fn_name = camel_case(arena, defn->children[1]->text);
    Node *params = defn->children[2];

    TsBuf param_list;
    tb_init(&param_list);
    ParamEntry *param_types = params->child_count
                                   ? arena_alloc(arena, sizeof(ParamEntry) * params->child_count)
                                   : NULL;
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
        /* Original (kebab-case) name here, matching what emit_ts_expr looks up by -- the source
           .prn text, not the camelCased TS identifier. Declared param type name (I32/F64/...),
           not the resolved TS type ("number"), so the division-truncation check above can tell
           I32 from F64. */
        param_types[i].name = param->children[0]->text;
        param_types[i].type = param->children[2]->text;
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
    TsTypeCtx body_ctx = *fn_ctx;
    body_ctx.params = param_types;
    body_ctx.param_count = params->child_count;
    const char *body_type;
    const char *body = emit_ts_expr(arena, defn->children[5], &body_ctx, out_error, &body_type);
    if (!body) {
        tb_free(&param_list);
        return 0;
    }

    tb_appendf(out, "export function %s(%s): %s {\n    return %s;\n}\n\n", fn_name, param_list.data, ret_type, body);
    tb_free(&param_list);
    return 1;
}

const char *emit_ts(Arena *arena, Node *program, const char **out_error) {
    /* First pass: collect every defn's name + declared return type (kebab-case source name,
       declared type name -- e.g. "I32", not the resolved TS "number") so emit_ts_expr can look up
       a called function's return type regardless of source order (a defn may call one defined
       later in the file, same as the C/Java targets already allow). */
    size_t defn_count = 0;
    for (size_t i = 0; i < program->child_count; i++) {
        if (is_call_named(program->children[i], "defn")) defn_count++;
    }
    FnSig *fn_sigs = defn_count ? arena_alloc(arena, sizeof(FnSig) * defn_count) : NULL;
    size_t fn_i = 0;
    for (size_t i = 0; i < program->child_count; i++) {
        Node *form = program->children[i];
        if (!is_call_named(form, "defn")) continue;
        if (form->child_count == 6 && form->children[1]->type == NODE_SYMBOL &&
            form->children[3]->type == NODE_COLON && form->children[4]->type == NODE_SYMBOL) {
            fn_sigs[fn_i].name = form->children[1]->text;
            fn_sigs[fn_i].ret_type = form->children[4]->text;
            fn_i++;
        }
        /* A malformed defn here is silently skipped in this first pass -- the real, second pass
           below still walks every form and reports the exact same "malformed function definition"
           error it always did, so no real error case gets swallowed. */
    }
    TsTypeCtx fn_ctx = {fn_sigs, fn_i, NULL, 0};

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
            if (!emit_ts_defn(arena, &out, form, &fn_ctx, out_error)) {
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
