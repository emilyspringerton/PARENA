/* emit_ts.c -- the TypeScript emitter. v0 (2026-08-30) was a single-expression scalar function
 * emitter; 2026-10-16 (BRAWLPIT web, founder: "whatever we do need JS for, use PARENA into TS")
 * grew it into something a browser host can be written in: statements, locals, loops, module
 * state and a small typed bridge to JavaScript. See emit_ts.h for the language surface.
 *
 * Shape: a body that is one pure expression still emits `return <expr>;` exactly as before. A body
 * that uses let / do / when / loop / set! / js/set! emits real statements. Expression-oriented
 * PARENA is lowered to statements by ts_stmt(), which carries a "destination" (discard, return,
 * assign to a variable, or "tail of a loop") the way emit_llvm.c carries its loop context.
 */
#include "emit_ts.h"
#include "ast_sugar.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* --- minimal, self-contained string builder (independent of emit.c's StrBuf, see emit_ts.h) --- */
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

/* Two-pass vsnprintf: measure, allocate exactly, format. (A fixed 512-byte buffer here silently
 * truncated long ternary chains once -- DEADWEIGHT's card_rules.prn found it.) */
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

static int is_symbol(Node *n, const char *text) {
    return n && n->type == NODE_SYMBOL && strcmp(n->text, text) == 0;
}

static int is_call_named(Node *n, const char *name) {
    return n && n->type == NODE_LIST && n->child_count >= 1 && is_symbol(n->children[0], name);
}

/* camel_case: kebab-case -> camelCase ("bezier-interp" -> "bezierInterp"), TypeScript's own naming
   convention rather than a 1:1 transliteration of the C emitter's snake_case. */
static const char *camel_case(Arena *arena, const char *kebab) {
    size_t len = strlen(kebab);
    char *out = arena_alloc(arena, len * 3 + 2);
    size_t oi = 0;
    int capitalize_next = 0;
    for (size_t i = 0; i < len; i++) {
        char c = kebab[i];
        if (c == '-') {
            capitalize_next = 1;
            continue;
        }
        /* `!` and `?` are legal PARENA name characters but not TypeScript identifier characters */
        if (c == '!') { memcpy(out + oi, "Bang", 4); oi += 4; continue; }
        if (c == '?') { out[oi++] = 'P'; continue; }
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

static const char *TS_RESERVED[] = {
    "break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do", "else",
    "enum", "export", "extends", "false", "finally", "for", "function", "if", "import", "in", "instanceof",
    "let", "new", "null", "return", "super", "switch", "this", "throw", "true", "try", "typeof", "var",
    "void", "while", "with", "yield", "await", "async", "static", "interface", "type", "namespace",
    "undefined", "arguments", "eval", "of", "package", "private", "protected", "public", "implements",
};

/* ident: the TypeScript identifier for a PARENA name -- camelCased, and a reserved word gets a
   trailing underscore so a binding called `new` or `default` still compiles. */
static const char *ident(Arena *arena, const char *kebab) {
    const char *c = camel_case(arena, kebab);
    for (size_t i = 0; i < sizeof(TS_RESERVED) / sizeof(TS_RESERVED[0]); i++) {
        if (strcmp(c, TS_RESERVED[i]) == 0) {
            size_t n = strlen(c);
            char *out = arena_alloc(arena, n + 2);
            memcpy(out, c, n);
            out[n] = '_';
            out[n + 1] = '\0';
            return out;
        }
    }
    return c;
}

/* resolve_ts_type: I32/F64/Bool/String/Unit plus `Any` -- an opaque JavaScript value (a DOM node, an
   event, a promise, an ImageData ...). Any other type name (a defstruct/defenum, Arena) is a real,
   honest "unsupported" error. */
static const char *resolve_ts_type(Node *type_sym, const char **out_error) {
    if (!type_sym || type_sym->type != NODE_SYMBOL) {
        *out_error = "emit_ts: expected a type symbol";
        return NULL;
    }
    if (strcmp(type_sym->text, "I32") == 0 || strcmp(type_sym->text, "F64") == 0) return "number";
    if (strcmp(type_sym->text, "Bool") == 0) return "boolean";
    if (strcmp(type_sym->text, "String") == 0) return "string";
    if (strcmp(type_sym->text, "Unit") == 0) return "void";
    if (strcmp(type_sym->text, "Any") == 0) return "any";
    *out_error = "emit_ts: unsupported parameter/return type (v0 only understands I32/F64/Bool/String/Unit/Any)";
    return NULL;
}

/* MATH_PRIM_TABLE -- recognized external math/ primitives lowered straight to Math.* */
typedef struct { const char *prn_name; const char *ts_fn; int arg_count; } MathPrimEntry;
static const MathPrimEntry MATH_PRIM_TABLE[] = {
    /* renamed math/random -> math/random-f64 2026-09-07 (a real C-target collision with glibc's random()) */
    {"math/random-f64", "Math.random", 0},
    {"math/floor", "Math.floor", 1},
    {"math/sqrt", "Math.sqrt", 1},
    {"math/log", "Math.log", 1},
    {"math/cos", "Math.cos", 1},
    {"math/sin", "Math.sin", 1},
    {"math/abs", "Math.abs", 1},
    {"math/min", "Math.min", 2},
    {"math/max", "Math.max", 2},
    {"math/round", "Math.round", 1},
};
#define MATH_PRIM_TABLE_COUNT (sizeof(MATH_PRIM_TABLE) / sizeof(MATH_PRIM_TABLE[0]))

static const MathPrimEntry *find_math_prim(const char *name) {
    for (size_t i = 0; i < MATH_PRIM_TABLE_COUNT; i++) {
        if (strcmp(MATH_PRIM_TABLE[i].prn_name, name) == 0) return &MATH_PRIM_TABLE[i];
    }
    return NULL;
}

/* BINOP_TABLE -- arithmetic / comparison / logical / bitwise operators, exactly two operands. */
typedef struct { const char *prn_op; const char *ts_op; int bool_result; int int_result; } BinopEntry;
static const BinopEntry BINOP_TABLE[] = {
    {"+", "+", 0, 0}, {"-", "-", 0, 0}, {"*", "*", 0, 0}, {"/", "/", 0, 0},
    {"=", "===", 1, 0}, {"!=", "!==", 1, 0}, {"<", "<", 1, 0}, {">", ">", 1, 0},
    {"<=", "<=", 1, 0}, {">=", ">=", 1, 0},
    {"and", "&&", 1, 0}, {"or", "||", 1, 0},
    {"mod", "%", 0, 0},
    {"bit-and", "&", 0, 1}, {"bit-or", "|", 0, 1}, {"bit-xor", "^", 0, 1},
    {"shl", "<<", 0, 1}, {"shr", ">>", 0, 1}, {"ushr", ">>>", 0, 1},
};
#define BINOP_TABLE_COUNT (sizeof(BINOP_TABLE) / sizeof(BINOP_TABLE[0]))

static const BinopEntry *find_binop(const char *prn_op) {
    for (size_t i = 0; i < BINOP_TABLE_COUNT; i++) {
        if (strcmp(BINOP_TABLE[i].prn_op, prn_op) == 0) return &BINOP_TABLE[i];
    }
    return NULL;
}

/* ---- type context: function signatures, the local-variable stack, module state --------------- */
typedef struct { const char *name; const char *ret_type; } FnSig;
typedef struct { const char *name; const char *type; } ParamEntry;

#define TS_MAX_VARS 192

typedef struct {
    FnSig *fns;
    size_t fn_count;
    ParamEntry *params;   /* params + let/loop locals, newest last (a stack; newest wins on lookup) */
    size_t param_count;
    ParamEntry *globals;  /* top-level (def ...) */
    size_t global_count;
    int tmp;              /* fresh temp-name counter */
    int ret_void;         /* the function being emitted returns Unit */
} TsTypeCtx;

static const char *lookup_fn_ret_type(const TsTypeCtx *ctx, const char *name) {
    for (size_t i = 0; i < ctx->fn_count; i++) {
        if (strcmp(ctx->fns[i].name, name) == 0) return ctx->fns[i].ret_type;
    }
    return NULL;
}

static const char *lookup_param_type(const TsTypeCtx *ctx, const char *name) {
    for (size_t i = ctx->param_count; i > 0; i--) {
        if (strcmp(ctx->params[i - 1].name, name) == 0) return ctx->params[i - 1].type;
    }
    for (size_t i = 0; i < ctx->global_count; i++) {
        if (strcmp(ctx->globals[i].name, name) == 0) return ctx->globals[i].type;
    }
    return NULL;
}

static int push_var(TsTypeCtx *ctx, const char *name, const char *type, const char **out_error) {
    if (ctx->param_count >= TS_MAX_VARS) {
        *out_error = "emit_ts: too many locals in one function";
        return 0;
    }
    ctx->params[ctx->param_count].name = name;
    ctx->params[ctx->param_count].type = type;
    ctx->param_count++;
    return 1;
}

static const char *ts_annot(const char *type_name) {
    if (strcmp(type_name, "I32") == 0 || strcmp(type_name, "F64") == 0) return "number";
    if (strcmp(type_name, "boolean") == 0) return "boolean";
    if (strcmp(type_name, "String") == 0) return "string";
    if (strcmp(type_name, "Unit") == 0) return "void";
    return "any";
}

static const char *emit_ts_expr(Arena *arena, Node *expr, TsTypeCtx *ctx, const char **out_error,
                                const char **out_type);

static const char *dup_buf(Arena *arena, TsBuf *b) {
    const char *r = arena_strdup(arena, b->data, b->len);
    tb_free(b);
    return r;
}

/* a string literal as TypeScript source text */
static const char *ts_string_literal(Arena *arena, const char *s, size_t len) {
    TsBuf b;
    tb_init(&b);
    tb_append(&b, "\"");
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"': tb_append(&b, "\\\""); break;
            case '\\': tb_append(&b, "\\\\"); break;
            case '\n': tb_append(&b, "\\n"); break;
            case '\r': tb_append(&b, "\\r"); break;
            case '\t': tb_append(&b, "\\t"); break;
            default:
                if (c < 32) tb_appendf(&b, "\\u%04x", c);
                else { char ch[2] = { (char)c, '\0' }; tb_append(&b, ch); }
        }
    }
    tb_append(&b, "\"");
    return dup_buf(arena, &b);
}

static int is_js_ident(const char *s, size_t len) {
    if (len == 0) return 0;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || (i > 0 && c >= '0' && c <= '9');
        if (!ok) return 0;
    }
    return 1;
}

/* a comma-joined argument list from children[from..] */
static const char *emit_args(Arena *arena, Node *expr, size_t from, TsTypeCtx *ctx, const char **out_error) {
    TsBuf b;
    tb_init(&b);
    for (size_t i = from; i < expr->child_count; i++) {
        if (i > from) tb_append(&b, ", ");
        const char *t;
        const char *a = emit_ts_expr(arena, expr->children[i], ctx, out_error, &t);
        if (!a) { tb_free(&b); return NULL; }
        tb_append(&b, a);
    }
    return dup_buf(arena, &b);
}

/* ---- the JavaScript bridge: the js bridge forms ------------------------------------------------------- */
/* Returns non-NULL on a recognized js-bridge form (sets *out_type); *handled says whether head was one. */
static const char *emit_js_form(Arena *arena, Node *expr, const char *head, TsTypeCtx *ctx,
                                const char **out_error, const char **out_type, int *handled) {
    *handled = 1;
    TsBuf b;
    if (strcmp(head, "js/global") == 0) {
        if (expr->child_count != 2 || expr->children[1]->type != NODE_STRING) {
            *out_error = "emit_ts: js/global takes one string literal (a global name)";
            return NULL;
        }
        Node *n = expr->children[1];
        for (size_t i = 0; i < n->text_len; i++) {
            char c = n->text[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' || c == '.')) {
                *out_error = "emit_ts: js/global name must be a plain identifier path";
                return NULL;
            }
        }
        *out_type = "Any";
        return arena_strdup(arena, n->text, n->text_len);
    }
    if (strcmp(head, "js/get") == 0 || strcmp(head, "js/aget") == 0) {
        if (expr->child_count != 3) {
            *out_error = "emit_ts: js/get / js/aget take (object key)";
            return NULL;
        }
        const char *ot;
        const char *obj = emit_ts_expr(arena, expr->children[1], ctx, out_error, &ot);
        if (!obj) return NULL;
        Node *k = expr->children[2];
        tb_init(&b);
        if (k->type == NODE_STRING && is_js_ident(k->text, k->text_len) && strcmp(head, "js/get") == 0) {
            tb_appendf(&b, "%s.%.*s", obj, (int)k->text_len, k->text);
        } else {
            const char *kt;
            const char *key = emit_ts_expr(arena, k, ctx, out_error, &kt);
            if (!key) { tb_free(&b); return NULL; }
            tb_appendf(&b, "%s[%s]", obj, key);
        }
        *out_type = "Any";
        return dup_buf(arena, &b);
    }
    if (strcmp(head, "js/call") == 0) {
        if (expr->child_count < 3 || expr->children[2]->type != NODE_STRING ||
            !is_js_ident(expr->children[2]->text, expr->children[2]->text_len)) {
            *out_error = "emit_ts: js/call takes (object \"method\" args...) with a string-literal method name";
            return NULL;
        }
        const char *ot;
        const char *obj = emit_ts_expr(arena, expr->children[1], ctx, out_error, &ot);
        if (!obj) return NULL;
        const char *args = emit_args(arena, expr, 3, ctx, out_error);
        if (!args) return NULL;
        tb_init(&b);
        tb_appendf(&b, "%s.%.*s(%s)", obj, (int)expr->children[2]->text_len, expr->children[2]->text, args);
        *out_type = "Any";
        return dup_buf(arena, &b);
    }
    if (strcmp(head, "js/call-fn") == 0) {
        if (expr->child_count < 2) {
            *out_error = "emit_ts: js/call-fn takes (function args...)";
            return NULL;
        }
        const char *ft;
        const char *f = emit_ts_expr(arena, expr->children[1], ctx, out_error, &ft);
        if (!f) return NULL;
        const char *args = emit_args(arena, expr, 2, ctx, out_error);
        if (!args) return NULL;
        tb_init(&b);
        tb_appendf(&b, "%s(%s)", f, args);
        *out_type = "Any";
        return dup_buf(arena, &b);
    }
    if (strcmp(head, "js/new") == 0) {
        if (expr->child_count < 2 || expr->children[1]->type != NODE_STRING) {
            *out_error = "emit_ts: js/new takes (\"Constructor\" args...)";
            return NULL;
        }
        const char *args = emit_args(arena, expr, 2, ctx, out_error);
        if (!args) return NULL;
        tb_init(&b);
        tb_appendf(&b, "new %.*s(%s)", (int)expr->children[1]->text_len, expr->children[1]->text, args);
        *out_type = "Any";
        return dup_buf(arena, &b);
    }
    if (strcmp(head, "js/null?") == 0 || strcmp(head, "js/typeof") == 0) {
        if (expr->child_count != 2) {
            *out_error = "emit_ts: js/null? and js/typeof take one operand";
            return NULL;
        }
        const char *t;
        const char *a = emit_ts_expr(arena, expr->children[1], ctx, out_error, &t);
        if (!a) return NULL;
        tb_init(&b);
        if (strcmp(head, "js/null?") == 0) { tb_appendf(&b, "(%s == null)", a); *out_type = "boolean"; }
        else { tb_appendf(&b, "(typeof %s)", a); *out_type = "String"; }
        return dup_buf(arena, &b);
    }
    if (strcmp(head, "js/defined?") == 0) {
        if (expr->child_count != 2 || expr->children[1]->type != NODE_STRING) {
            *out_error = "emit_ts: js/defined? takes a string-literal global name";
            return NULL;
        }
        tb_init(&b);
        tb_appendf(&b, "(typeof %.*s !== \"undefined\")", (int)expr->children[1]->text_len, expr->children[1]->text);
        *out_type = "boolean";
        return dup_buf(arena, &b);
    }
    if (strcmp(head, "js/obj") == 0) {
        if ((expr->child_count - 1) % 2 != 0) {
            *out_error = "emit_ts: js/obj takes key/value pairs";
            return NULL;
        }
        tb_init(&b);
        tb_append(&b, "{");
        for (size_t i = 1; i + 1 < expr->child_count; i += 2) {
            if (expr->children[i]->type != NODE_STRING) {
                tb_free(&b);
                *out_error = "emit_ts: js/obj keys must be string literals";
                return NULL;
            }
            const char *vt;
            const char *v = emit_ts_expr(arena, expr->children[i + 1], ctx, out_error, &vt);
            if (!v) { tb_free(&b); return NULL; }
            if (i > 1) tb_append(&b, ", ");
            tb_appendf(&b, "%s: %s", ts_string_literal(arena, expr->children[i]->text, expr->children[i]->text_len), v);
        }
        tb_append(&b, "}");
        *out_type = "Any";
        return dup_buf(arena, &b);
    }
    if (strcmp(head, "js/array") == 0) {
        const char *args = emit_args(arena, expr, 1, ctx, out_error);
        if (!args) return NULL;
        tb_init(&b);
        tb_appendf(&b, "[%s]", args);
        *out_type = "Any";
        return dup_buf(arena, &b);
    }
    if (strcmp(head, "str") == 0) {
        tb_init(&b);
        tb_append(&b, "(\"\"");
        for (size_t i = 1; i < expr->child_count; i++) {
            const char *t;
            const char *a = emit_ts_expr(arena, expr->children[i], ctx, out_error, &t);
            if (!a) { tb_free(&b); return NULL; }
            tb_appendf(&b, " + %s", a);
        }
        tb_append(&b, ")");
        *out_type = "String";
        return dup_buf(arena, &b);
    }
    if (strcmp(head, "callback") == 0) {
        /* (callback name): the named top-level defn as a function value, for addEventListener /
           requestAnimationFrame / promise.then. PARENA has no lambdas; a named function is the
           closure, and module-level (def ...) state is what it closes over. */
        if (expr->child_count != 2 || expr->children[1]->type != NODE_SYMBOL ||
            !lookup_fn_ret_type(ctx, expr->children[1]->text)) {
            *out_error = "emit_ts: callback takes the name of a top-level defn";
            return NULL;
        }
        *out_type = "Any";
        return ident(arena, expr->children[1]->text);
    }
    *handled = 0;
    return NULL;
}

/* A form that is a statement, not a value (let/do/when/loop/set!/...). In expression position it
   is a compile error rather than silently wrong code. */
static int is_stmt_head(const char *h) {
    return !strcmp(h, "let") || !strcmp(h, "do") || !strcmp(h, "when") || !strcmp(h, "loop") ||
           !strcmp(h, "recur") || !strcmp(h, "set!") || !strcmp(h, "js/set!") || !strcmp(h, "js/aset!");
}

/* true if the tree contains any statement-only form (so an `if` over it cannot be a ternary) */
static int needs_stmt(Node *n) {
    if (!n || n->type != NODE_LIST || n->child_count == 0) return 0;
    if (n->children[0]->type == NODE_SYMBOL && is_stmt_head(n->children[0]->text)) return 1;
    for (size_t i = 0; i < n->child_count; i++)
        if (needs_stmt(n->children[i])) return 1;
    return 0;
}

static const char *emit_ts_expr(Arena *arena, Node *expr, TsTypeCtx *ctx, const char **out_error,
                                const char **out_type) {
    if (!expr) {
        *out_error = "emit_ts: null expression";
        return NULL;
    }

    if (expr->type == NODE_NUMBER) {
        /* a '.' in the source text means F64 ("3.0"); a bare integer ("16") is I32 */
        *out_type = memchr(expr->text, '.', expr->text_len) ? "F64" : "I32";
        return arena_strdup(arena, expr->text, expr->text_len);
    }

    if (expr->type == NODE_STRING) {
        *out_type = "String";
        return ts_string_literal(arena, expr->text, expr->text_len);
    }

    if (expr->type == NODE_SYMBOL) {
        if (strcmp(expr->text, "math/pi") == 0) {
            *out_type = "F64";
            return "Math.PI";
        }
        if (strcmp(expr->text, "true") == 0 || strcmp(expr->text, "false") == 0) {
            *out_type = "boolean";
            return expr->text;
        }
        if (strcmp(expr->text, "null") == 0) { *out_type = "Any"; return "null"; }
        if (strcmp(expr->text, "undefined") == 0 || strcmp(expr->text, "unit") == 0) { *out_type = "Unit"; return "undefined"; }
        const char *p_type = lookup_param_type(ctx, expr->text);
        if (!p_type) {
            char *msg = arena_alloc(arena, strlen(expr->text) + 64);
            sprintf(msg, "emit_ts: reference to an undeclared symbol '%s'", expr->text);
            *out_error = msg;
            return NULL;
        }
        *out_type = p_type;
        return ident(arena, expr->text);
    }

    if (expr->type != NODE_LIST || expr->child_count == 0 || expr->children[0]->type != NODE_SYMBOL) {
        *out_error = "emit_ts: unsupported expression form (v0 only understands numbers, strings, symbols, "
                     "binops, if, and calls)";
        return NULL;
    }

    const char *head = expr->children[0]->text;

    if (is_stmt_head(head)) {
        char *msg = arena_alloc(arena, strlen(head) + 96);
        sprintf(msg, "emit_ts: statement form (%s ...) used where a value is needed (v0 allows it only in statement position)", head);
        *out_error = msg;
        return NULL;
    }

    /* if -- a ternary; branches must be plain expressions here (statement ifs go through ts_stmt) */
    if (strcmp(head, "if") == 0) {
        if (expr->child_count != 4) {
            *out_error = "emit_ts: if requires exactly (if cond then else) where a value is needed";
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
        *out_type = strcmp(then_type, "Unit") == 0 ? else_type : then_type;
        TsBuf b;
        tb_init(&b);
        tb_appendf(&b, "(%s ? %s : %s)", cond, then_e, else_e);
        return dup_buf(arena, &b);
    }

    /* (not x): TypeScript's `!` */
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
        return dup_buf(arena, &b);
    }

    /* unary minus */
    if (strcmp(head, "-") == 0 && expr->child_count == 2) {
        const char *t;
        const char *a = emit_ts_expr(arena, expr->children[1], ctx, out_error, &t);
        if (!a) return NULL;
        *out_type = t;
        TsBuf b;
        tb_init(&b);
        tb_appendf(&b, "(-%s)", a);
        return dup_buf(arena, &b);
    }

    /* the js/ bridge, str, callback */
    {
        int handled = 0;
        const char *r = emit_js_form(arena, expr, head, ctx, out_error, out_type, &handled);
        if (handled) return r;
    }

    /* the numeric / logical / bitwise binary operators */
    const BinopEntry *bop = find_binop(head);
    if (bop) {
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
        int any_str = strcmp(lhs_type, "String") == 0 || strcmp(rhs_type, "String") == 0;
        int any_any = strcmp(lhs_type, "Any") == 0 || strcmp(rhs_type, "Any") == 0;
        TsBuf b;
        tb_init(&b);
        if (strcmp(head, "/") == 0 && is_i32_pair) {
            /* TypeScript's / is real-number division; I32 / I32 must truncate toward zero like the
               C and Java targets. Math.trunc matches (Math.floor would not, for negatives). */
            tb_appendf(&b, "Math.trunc(%s / %s)", lhs, rhs);
            *out_type = "I32";
        } else if (strcmp(head, "mod") == 0) {
            tb_appendf(&b, "(%s %% %s)", lhs, rhs);
            *out_type = is_i32_pair ? "I32" : "F64";
        } else {
            tb_appendf(&b, "(%s %s %s)", lhs, bop->ts_op, rhs);
            if (bop->bool_result) *out_type = "boolean";
            else if (bop->int_result) *out_type = "I32";
            else if (any_str && strcmp(head, "+") == 0) *out_type = "String";
            else if (any_any) *out_type = "Any";
            else *out_type = is_i32_pair ? "I32" : "F64";
        }
        return dup_buf(arena, &b);
    }

    /* external math primitives */
    const MathPrimEntry *math_prim = find_math_prim(head);
    if (math_prim) {
        size_t got_args = expr->child_count - 1;
        if ((int)got_args != math_prim->arg_count) {
            *out_error = "emit_ts: math primitive called with the wrong number of arguments";
            return NULL;
        }
        const char *args = emit_args(arena, expr, 1, ctx, out_error);
        if (!args) return NULL;
        TsBuf b;
        tb_init(&b);
        tb_appendf(&b, "%s(%s)", math_prim->ts_fn, args);
        *out_type = "F64";
        return dup_buf(arena, &b);
    }

    /* a call to another top-level defn in the same generated file */
    /* A callee that is not defined in this file is allowed (DEADWEIGHT's fx_rules.prn calls
       card_rules.prn's functions and the generated modules are bundled together); its result is
       assumed I32, the v0 behaviour. tsc / the test run catches a genuine typo. */
    const char *callee_ret = lookup_fn_ret_type(ctx, head);
    const char *args = emit_args(arena, expr, 1, ctx, out_error);
    if (!args) return NULL;
    TsBuf b;
    tb_init(&b);
    tb_appendf(&b, "%s(%s)", ident(arena, head), args);
    *out_type = callee_ret ? callee_ret : "I32";
    return dup_buf(arena, &b);
}

/* ---- statements ---------------------------------------------------------------------------- */
typedef enum { D_DISCARD, D_RETURN, D_ASSIGN, D_LOOP } DestKind;

struct LoopCtx;
typedef struct Dest {
    DestKind kind;
    const char *var;           /* D_ASSIGN: target variable */
    struct LoopCtx *loop;      /* D_LOOP: the innermost enclosing loop */
} Dest;

typedef struct LoopCtx {
    size_t nvars;
    const char *vars[16];
    const char *types[16];
    Dest outer;                /* where the loop's terminal value goes */
} LoopCtx;

static void indent(TsBuf *b, int depth) {
    for (int i = 0; i < depth; i++) tb_append(b, "    ");
}

static int ts_stmt(Arena *arena, TsBuf *out, int depth, Node *n, Dest d, TsTypeCtx *ctx, const char **out_error);

/* Emit the statements that deliver a plain value expression `code` to destination d. */
static void finish_value(TsBuf *out, int depth, const char *code, const char *type, Dest d, TsTypeCtx *ctx) {
    switch (d.kind) {
        case D_DISCARD:
            indent(out, depth); tb_appendf(out, "%s;\n", code);
            break;
        case D_RETURN:
            indent(out, depth);
            if (ctx->ret_void || strcmp(type, "Unit") == 0) tb_appendf(out, "%s;\n", code);
            else tb_appendf(out, "return %s;\n", code);
            break;
        case D_ASSIGN:
            indent(out, depth); tb_appendf(out, "%s = %s;\n", d.var, code);
            break;
        case D_LOOP: {
            /* a terminal value of a loop: deliver it to the loop's own destination, then leave */
            Dest outer = d.loop->outer;
            finish_value(out, depth, code, type, outer, ctx);
            if (outer.kind != D_RETURN) { indent(out, depth); tb_append(out, "break;\n"); }
            break;
        }
    }
}

static const char *fresh_tmp(Arena *arena, TsTypeCtx *ctx) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "_t%d", ctx->tmp++);
    return arena_strdup(arena, tmp, strlen(tmp));
}

/* (let [name init ...] body...) / (loop ...) bindings -> `let name: T = init;` lines */
static int emit_bindings(Arena *arena, TsBuf *out, int depth, Node *vec, TsTypeCtx *ctx,
                         const char **names, const char **types, size_t *count, const char **out_error) {
    for (size_t i = 0; i < vec->child_count; i += 2) {
        Node *nm = vec->children[i];
        const char *declared = NULL;
        if (nm->type == NODE_LIST && nm->child_count == 3 && nm->children[0]->type == NODE_SYMBOL &&
            nm->children[1]->type == NODE_COLON && nm->children[2]->type == NODE_SYMBOL) {
            if (!resolve_ts_type(nm->children[2], out_error)) return 0;
            declared = strcmp(nm->children[2]->text, "Bool") == 0 ? "boolean" : nm->children[2]->text;
            nm = nm->children[0];
        }
        if (nm->type != NODE_SYMBOL) {
            *out_error = "emit_ts: let/loop binding name must be a symbol";
            return 0;
        }
        const char *t;
        const char *init = emit_ts_expr(arena, vec->children[i + 1], ctx, out_error, &t);
        if (!init) return 0;
        const char *vt = declared ? declared : t;
        const char *id = ident(arena, nm->text);
        indent(out, depth);
        tb_appendf(out, "let %s: %s = %s;\n", id, ts_annot(vt), init);
        if (!push_var(ctx, nm->text, vt, out_error)) return 0;
        if (names && *count < 16) { names[*count] = id; types[*count] = vt; (*count)++; }
    }
    return 1;
}

static int ts_seq(Arena *arena, TsBuf *out, int depth, Node *list, size_t from, Dest d, TsTypeCtx *ctx,
                  const char **out_error) {
    Dest discard = { D_DISCARD, NULL, NULL };
    if (from >= list->child_count) return 1;
    for (size_t i = from; i + 1 < list->child_count; i++) {
        Node *c = list->children[i];
        if (c->type == NODE_NUMBER) continue;
        if (!ts_stmt(arena, out, depth, c, discard, ctx, out_error)) return 0;
    }
    return ts_stmt(arena, out, depth, list->children[list->child_count - 1], d, ctx, out_error);
}

static int ts_stmt(Arena *arena, TsBuf *out, int depth, Node *n, Dest d, TsTypeCtx *ctx, const char **out_error) {
    if (n->type == NODE_LIST && n->child_count > 0 && n->children[0]->type == NODE_SYMBOL) {
        const char *h = n->children[0]->text;

        if (strcmp(h, "do") == 0) return ts_seq(arena, out, depth, n, 1, d, ctx, out_error);

        if (strcmp(h, "let") == 0) {
            if (n->child_count < 2 || n->children[1]->type != NODE_VEC || n->children[1]->child_count % 2 != 0) {
                *out_error = "emit_ts: let requires (let [name init ...] body...)";
                return 0;
            }
            size_t saved = ctx->param_count;
            indent(out, depth); tb_append(out, "{\n");
            if (!emit_bindings(arena, out, depth + 1, n->children[1], ctx, NULL, NULL, NULL, out_error)) return 0;
            if (!ts_seq(arena, out, depth + 1, n, 2, d, ctx, out_error)) return 0;
            indent(out, depth); tb_append(out, "}\n");
            ctx->param_count = saved;
            return 1;
        }

        if (strcmp(h, "when") == 0) {
            if (n->child_count < 2) {
                *out_error = "emit_ts: when requires (when cond body...)";
                return 0;
            }
            const char *ct;
            const char *c = emit_ts_expr(arena, n->children[1], ctx, out_error, &ct);
            if (!c) return 0;
            indent(out, depth); tb_appendf(out, "if (%s) {\n", c);
            Dest bd = (d.kind == D_LOOP) ? d : (Dest){ D_DISCARD, NULL, NULL };
            if (!ts_seq(arena, out, depth + 1, n, 2, bd, ctx, out_error)) return 0;
            indent(out, depth); tb_append(out, "}\n");
            return 1;
        }

        if (strcmp(h, "if") == 0 && (n->child_count == 3 || n->child_count == 4)) {
            /* a pure value `if` stays a ternary, so an expression body round-trips as before */
            if (n->child_count == 4 && !needs_stmt(n) && d.kind != D_LOOP && d.kind != D_DISCARD) {
                const char *t;
                const char *code = emit_ts_expr(arena, n, ctx, out_error, &t);
                if (!code) return 0;
                finish_value(out, depth, code, t, d, ctx);
                return 1;
            }
            const char *ct;
            const char *c = emit_ts_expr(arena, n->children[1], ctx, out_error, &ct);
            if (!c) return 0;
            indent(out, depth); tb_appendf(out, "if (%s) {\n", c);
            if (!ts_stmt(arena, out, depth + 1, n->children[2], d, ctx, out_error)) return 0;
            if (n->child_count == 4) {
                indent(out, depth); tb_append(out, "} else {\n");
                if (!ts_stmt(arena, out, depth + 1, n->children[3], d, ctx, out_error)) return 0;
            }
            indent(out, depth); tb_append(out, "}\n");
            return 1;
        }

        if (strcmp(h, "set!") == 0) {
            if (n->child_count != 3 || n->children[1]->type != NODE_SYMBOL) {
                *out_error = "emit_ts: set! requires (set! name expr)";
                return 0;
            }
            if (!lookup_param_type(ctx, n->children[1]->text)) {
                *out_error = "emit_ts: set! target is not a declared local or module variable";
                return 0;
            }
            const char *t;
            const char *v = emit_ts_expr(arena, n->children[2], ctx, out_error, &t);
            if (!v) return 0;
            indent(out, depth); tb_appendf(out, "%s = %s;\n", ident(arena, n->children[1]->text), v);
            return 1;
        }

        if (strcmp(h, "js/set!") == 0) {
            if (n->child_count != 4 || n->children[2]->type != NODE_STRING) {
                *out_error = "emit_ts: js/set! requires (js/set! object \"property\" value)";
                return 0;
            }
            const char *ot, *vt;
            const char *o = emit_ts_expr(arena, n->children[1], ctx, out_error, &ot);
            if (!o) return 0;
            const char *v = emit_ts_expr(arena, n->children[3], ctx, out_error, &vt);
            if (!v) return 0;
            Node *k = n->children[2];
            indent(out, depth);
            if (is_js_ident(k->text, k->text_len)) tb_appendf(out, "%s.%.*s = %s;\n", o, (int)k->text_len, k->text, v);
            else tb_appendf(out, "%s[%s] = %s;\n", o, ts_string_literal(arena, k->text, k->text_len), v);
            return 1;
        }

        if (strcmp(h, "js/aset!") == 0) {
            if (n->child_count != 4) {
                *out_error = "emit_ts: js/aset! requires (js/aset! array index value)";
                return 0;
            }
            const char *ot, *it, *vt;
            const char *o = emit_ts_expr(arena, n->children[1], ctx, out_error, &ot);
            if (!o) return 0;
            const char *i = emit_ts_expr(arena, n->children[2], ctx, out_error, &it);
            if (!i) return 0;
            const char *v = emit_ts_expr(arena, n->children[3], ctx, out_error, &vt);
            if (!v) return 0;
            indent(out, depth); tb_appendf(out, "%s[%s] = %s;\n", o, i, v);
            return 1;
        }

        if (strcmp(h, "loop") == 0) {
            if (n->child_count < 3 || n->children[1]->type != NODE_VEC || n->children[1]->child_count % 2 != 0) {
                *out_error = "emit_ts: loop requires (loop [name init ...] body...)";
                return 0;
            }
            size_t saved = ctx->param_count;
            LoopCtx *lc = arena_alloc(arena, sizeof(LoopCtx));
            memset(lc, 0, sizeof(*lc));
            lc->outer = d;
            indent(out, depth); tb_append(out, "{\n");
            if (!emit_bindings(arena, out, depth + 1, n->children[1], ctx, lc->vars, lc->types, &lc->nvars, out_error)) return 0;
            indent(out, depth + 1); tb_append(out, "while (true) {\n");
            Dest ld = { D_LOOP, NULL, lc };
            if (!ts_seq(arena, out, depth + 2, n, 2, ld, ctx, out_error)) return 0;
            indent(out, depth + 2); tb_append(out, "break;\n");
            indent(out, depth + 1); tb_append(out, "}\n");
            indent(out, depth); tb_append(out, "}\n");
            ctx->param_count = saved;
            return 1;
        }

        if (strcmp(h, "recur") == 0) {
            if (d.kind != D_LOOP || (n->child_count - 1) != d.loop->nvars) {
                *out_error = "emit_ts: recur must be in tail position of a loop with matching arguments";
                return 0;
            }
            LoopCtx *lc = d.loop;
            if (lc->nvars == 1) {
                const char *t;
                const char *v = emit_ts_expr(arena, n->children[1], ctx, out_error, &t);
                if (!v) return 0;
                indent(out, depth); tb_appendf(out, "%s = %s;\n", lc->vars[0], v);
            } else {
                const char *tmps[16];
                for (size_t i = 0; i < lc->nvars; i++) {
                    const char *t;
                    const char *v = emit_ts_expr(arena, n->children[i + 1], ctx, out_error, &t);
                    if (!v) return 0;
                    tmps[i] = fresh_tmp(arena, ctx);
                    indent(out, depth); tb_appendf(out, "const %s = %s;\n", tmps[i], v);
                }
                for (size_t i = 0; i < lc->nvars; i++) {
                    indent(out, depth); tb_appendf(out, "%s = %s;\n", lc->vars[i], tmps[i]);
                }
            }
            indent(out, depth); tb_append(out, "continue;\n");
            return 1;
        }
    }

    /* the `unit` symbol and bare number literals produce nothing */
    if (n->type == NODE_SYMBOL && strcmp(n->text, "unit") == 0) return 1;
    if (n->type == NODE_NUMBER && d.kind == D_DISCARD) return 1;

    const char *t;
    const char *code = emit_ts_expr(arena, n, ctx, out_error, &t);
    if (!code) return 0;
    finish_value(out, depth, code, t, d, ctx);
    return 1;
}

/* emit_ts_defn: (defn name [(param : Type) ...] : RetType body...) -> one exported TypeScript
   function. A single pure-expression body keeps the v0 shape (`return <expr>;`). */
static int emit_ts_defn(Arena *arena, TsBuf *out, Node *defn, TsTypeCtx *ctx, const char **out_error) {
    if (defn->child_count < 3 || defn->children[1]->type != NODE_SYMBOL || defn->children[2]->type != NODE_VEC) {
        *out_error = "emit_ts: defn: malformed function definition";
        return 0;
    }
    const char *fn_name = ident(arena, defn->children[1]->text);
    Node *params = defn->children[2];

    ctx->param_count = 0;
    TsBuf param_list;
    tb_init(&param_list);
    for (size_t i = 0; i < params->child_count; i++) {
        Node *param = params->children[i];
        if (param->type != NODE_LIST || param->child_count != 3 || param->children[0]->type != NODE_SYMBOL ||
            param->children[1]->type != NODE_COLON || param->children[2]->type != NODE_SYMBOL) {
            *out_error = "emit_ts: defn: unsupported parameter shape (v0 only understands plain "
                         "(name : I32|F64|Bool|String|Any) params -- no Arena/region annotations)";
            tb_free(&param_list);
            return 0;
        }
        const char *p_type = resolve_ts_type(param->children[2], out_error);
        if (!p_type) {
            tb_free(&param_list);
            return 0;
        }
        if (i > 0) tb_append(&param_list, ", ");
        tb_appendf(&param_list, "%s: %s", ident(arena, param->children[0]->text), p_type);
        /* declared type NAME (I32/F64/...) so the division-truncation check can tell I32 from F64 */
        const char *tn = param->children[2]->text;
        if (!push_var(ctx, param->children[0]->text,
                      strcmp(tn, "Bool") == 0 ? "boolean" : arena_strdup(arena, tn, strlen(tn)), out_error)) {
            tb_free(&param_list);
            return 0;
        }
    }

    if (defn->child_count < 6 || defn->children[3]->type != NODE_COLON) {
        *out_error = "emit_ts: defn: expected (defn name [params] : RetType body...)";
        tb_free(&param_list);
        return 0;
    }
    const char *ret_type = resolve_ts_type(defn->children[4], out_error);
    if (!ret_type) {
        tb_free(&param_list);
        return 0;
    }
    ctx->ret_void = strcmp(ret_type, "void") == 0;
    ctx->tmp = 0;

    TsBuf body;
    tb_init(&body);
    Dest d = { D_RETURN, NULL, NULL };
    if (!ts_seq(arena, &body, 1, defn, 5, d, ctx, out_error)) {
        tb_free(&body);
        tb_free(&param_list);
        return 0;
    }
    tb_appendf(out, "export function %s(%s): %s {\n%s}\n\n", fn_name, param_list.data, ret_type, body.data);
    tb_free(&body);
    tb_free(&param_list);
    return 1;
}

const char *emit_ts(Arena *arena, Node *program, const char **out_error) {
    *out_error = NULL;
    g_sugar_counter = 0;
    /* sugar first: cond / case / dotimes / unless / inc! ... become core forms */
    for (size_t i = 0; i < program->child_count; i++) {
        Node *form = program->children[i];
        if (!is_call_named(form, "defn")) continue;
        for (size_t c = 4; c < form->child_count; c++) {
            form->children[c] = ast_sugar(arena, form->children[c], out_error);
            if (*out_error) return NULL;
        }
    }

    /* pass 1: every defn's name + declared return type (forward calls resolve) */
    size_t defn_count = 0, def_count = 0;
    for (size_t i = 0; i < program->child_count; i++) {
        if (is_call_named(program->children[i], "defn")) defn_count++;
        if (is_call_named(program->children[i], "def")) def_count++;
    }
    FnSig *fn_sigs = arena_alloc(arena, sizeof(FnSig) * (defn_count + 1));
    size_t fn_i = 0;
    for (size_t i = 0; i < program->child_count; i++) {
        Node *form = program->children[i];
        if (!is_call_named(form, "defn")) continue;
        if (form->child_count >= 6 && form->children[1]->type == NODE_SYMBOL &&
            form->children[3]->type == NODE_COLON && form->children[4]->type == NODE_SYMBOL) {
            fn_sigs[fn_i].name = form->children[1]->text;
            const char *rt = form->children[4]->text;
            fn_sigs[fn_i].ret_type = strcmp(rt, "Bool") == 0 ? "boolean" : rt;
            fn_i++;
        }
        /* a malformed defn is skipped here; pass 2 reports it with the same error as always */
    }

    TsTypeCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.fns = fn_sigs;
    ctx.fn_count = fn_i;
    ctx.params = arena_alloc(arena, sizeof(ParamEntry) * TS_MAX_VARS);
    ctx.globals = arena_alloc(arena, sizeof(ParamEntry) * (def_count + 1));

    TsBuf out;
    tb_init(&out);
    tb_append(&out, "// Generated by parena build (TypeScript target) -- VS0-for-TS v0, do not edit by hand.\n\n");

    /* pass 2: module state, then functions, then start calls, in source order within each group */
    for (size_t i = 0; i < program->child_count; i++) {
        Node *form = program->children[i];
        if (!is_call_named(form, "def")) continue;
        if (form->child_count != 3 || form->children[1]->type != NODE_SYMBOL) {
            *out_error = "emit_ts: def: expected (def name expr)";
            tb_free(&out);
            return NULL;
        }
        ctx.param_count = 0;
        const char *t;
        const char *init = emit_ts_expr(arena, form->children[2], &ctx, out_error, &t);
        if (!init) { tb_free(&out); return NULL; }
        ctx.globals[ctx.global_count].name = form->children[1]->text;
        ctx.globals[ctx.global_count].type = t;
        ctx.global_count++;
        tb_appendf(&out, "let %s: %s = %s;\n", ident(arena, form->children[1]->text), ts_annot(t), init);
    }
    if (ctx.global_count) tb_append(&out, "\n");

    for (size_t i = 0; i < program->child_count; i++) {
        Node *form = program->children[i];
        /* (module ...) / (export ...) / (import ...) / (llvm-extern ...) are metadata for other
           targets; every defn is exported unconditionally in the single generated module */
        if (is_call_named(form, "module") || is_call_named(form, "export") || is_call_named(form, "import") ||
            is_call_named(form, "llvm-extern") || is_call_named(form, "def") || is_call_named(form, "start")) {
            continue;
        }
        if (is_call_named(form, "defn")) {
            if (!emit_ts_defn(arena, &out, form, &ctx, out_error)) {
                tb_free(&out);
                return NULL;
            }
            continue;
        }
        *out_error = "emit_ts: unsupported top-level form (understands defn, def, start, module, export, import)";
        tb_free(&out);
        return NULL;
    }

    for (size_t i = 0; i < program->child_count; i++) {
        Node *form = program->children[i];
        if (!is_call_named(form, "start")) continue;
        if (form->child_count != 2) {
            *out_error = "emit_ts: start takes one expression (run once when the module loads)";
            tb_free(&out);
            return NULL;
        }
        ctx.param_count = 0;
        ctx.ret_void = 1;
        const char *t;
        const char *code = emit_ts_expr(arena, form->children[1], &ctx, out_error, &t);
        if (!code) { tb_free(&out); return NULL; }
        tb_appendf(&out, "%s;\n", code);
    }

    const char *result = arena_strdup(arena, out.data, out.len);
    tb_free(&out);
    return result;
}
