/* emit_java.c — real, v0, narrow-scope Java emitter. See emit_java.h's own header comment for the
 * full real scope statement (a scalar `defn`, no Arena/region, one expression body, wrapped in one
 * real class matching the output file's own basename). */
#include "emit_java.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* --- minimal, self-contained string builder -- deliberately NOT shared with emit.c's own StrBuf
   or emit_ts.c's own TsBuf (see emit_java.h's own header comment for why this file stays
   independent). Same real "temporary C-heap scratch, arena-owned result" shape both of those
   already use for their own final buffer. */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} JavaBuf;

static void jb_init(JavaBuf *b) {
    b->cap = 256;
    b->data = malloc(b->cap);
    b->data[0] = '\0';
    b->len = 0;
}

static void jb_free(JavaBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void jb_append(JavaBuf *b, const char *s) {
    size_t add_len = strlen(s);
    if (b->len + add_len + 1 > b->cap) {
        while (b->len + add_len + 1 > b->cap) b->cap *= 2;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, add_len + 1);
    b->len += add_len;
}

static void jb_appendf(JavaBuf *b, const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    jb_append(b, tmp);
}

/* --- small AST helpers, same real shape emit.c's and emit_ts.c's own is_symbol/is_call_named
   use, reimplemented independently here rather than exported/shared. */
static int is_symbol(Node *n, const char *text) {
    return n && n->type == NODE_SYMBOL && strcmp(n->text, text) == 0;
}

static int is_call_named(Node *n, const char *name) {
    return n && n->type == NODE_LIST && n->child_count >= 1 && is_symbol(n->children[0], name);
}

/* camel_case: real, minimal kebab-case -> camelCase converter, identical real logic to emit_ts.c's
   own (Java methods are also real, conventionally camelCase, the exact same target-language naming
   convention TypeScript uses here) -- reimplemented independently rather than shared, matching
   this file's own deliberate "no cross-target sharing" discipline. */
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

/* resolve_java_type: the real, narrow I32/F64/Bool/String/Unit -> Java type mapping this v0
   understands -- any other type name is a real, honest "unsupported" error, same real boundary
   emit_ts.c's own resolve_ts_type draws. Note real, deliberate choice of primitive `int`/`double`/
   `boolean` (not boxed `Integer`/`Double`/`Boolean`) -- this v0's own scalar-only scope never
   needs a nullable/boxed value, so the real, cheaper primitive types are the honest match. */
static const char *resolve_java_type(Node *type_sym, const char **out_error) {
    if (!type_sym || type_sym->type != NODE_SYMBOL) {
        *out_error = "emit_java: expected a type symbol";
        return NULL;
    }
    if (strcmp(type_sym->text, "I32") == 0) return "int";
    if (strcmp(type_sym->text, "F64") == 0) return "double";
    if (strcmp(type_sym->text, "Bool") == 0) return "boolean";
    if (strcmp(type_sym->text, "String") == 0) return "String";
    if (strcmp(type_sym->text, "Unit") == 0) return "void";
    *out_error = "emit_java: unsupported parameter/return type (v0 only understands I32/F64/Bool/String/Unit)";
    return NULL;
}

/* MATH_PRIM_TABLE -- the exact same real, recognized external math/ primitives emit_ts.c's own
   table names, reusing the same real method-name STRINGS verbatim: java.lang.Math has identical
   real method names to JS's own Math (Math.random/floor/sqrt/log/cos, Math.PI) -- a real,
   convenient overlap this file leans on rather than re-deriving from scratch. See
   stdlib/math/math.prn's own doc comment for each real, matching PARENA-side signature. */
typedef struct { const char *prn_name; const char *java_fn; int arg_count; } MathPrimEntry;
static const MathPrimEntry MATH_PRIM_TABLE[] = {
    {"math/random", "Math.random", 0},
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

/* BINOP_TABLE -- the same real narrow operator set emit_ts.c's own table recognizes, with exactly
   one real, deliberate difference: `=` -> `==` (not `===` -- Java has no triple-equals token at
   all), which gives correct real value-equality semantics for every real scalar primitive type
   (int/double/boolean) this v0's own narrow type table understands. */
typedef struct { const char *prn_op; const char *java_op; } BinopEntry;
static const BinopEntry BINOP_TABLE[] = {
    {"+", "+"}, {"-", "-"}, {"*", "*"}, {"/", "/"},
    {"=", "=="}, {"<", "<"}, {">", ">"}, {"<=", "<="}, {">=", ">="},
    {"and", "&&"}, {"or", "||"},
};
#define BINOP_TABLE_COUNT (sizeof(BINOP_TABLE) / sizeof(BINOP_TABLE[0]))

static const char *find_binop(const char *prn_op) {
    for (size_t i = 0; i < BINOP_TABLE_COUNT; i++) {
        if (strcmp(BINOP_TABLE[i].prn_op, prn_op) == 0) return BINOP_TABLE[i].java_op;
    }
    return NULL;
}

static const char *emit_java_expr(Arena *arena, Node *expr, const char **out_error);

/* emit_java_expr: the real, recursive expression emitter -- structurally identical real shape to
   emit_ts.c's own emit_ts_expr (see that file's own doc comment for the full real rationale, not
   repeated here): number/symbol literals, the narrow binop set above, `if` as a ternary
   expression, a call to a recognized `math/` primitive, or a call to another top-level function
   defined in the same file (camelCased, matching this file's own emitted method names). */
static const char *emit_java_expr(Arena *arena, Node *expr, const char **out_error) {
    if (!expr) {
        *out_error = "emit_java: null expression";
        return NULL;
    }

    if (expr->type == NODE_NUMBER) {
        return arena_strdup(arena, expr->text, expr->text_len);
    }

    if (expr->type == NODE_SYMBOL) {
        /* math/pi -- the one real, recognized external CONSTANT this v0 knows (distinct from the
           MATH_PRIM_TABLE calls above -- a bare symbol reference, not a call), lowered directly to
           Math.PI, same real java.lang.Math overlap the whole table above leans on. Checked before
           the generic camel_case fallback so it isn't mistaken for a local parameter reference. */
        if (strcmp(expr->text, "math/pi") == 0) return "Math.PI";
        return camel_case(arena, expr->text);
    }

    if (expr->type != NODE_LIST || expr->child_count == 0 || expr->children[0]->type != NODE_SYMBOL) {
        *out_error = "emit_java: unsupported expression form (v0 only understands numbers, symbols, "
                     "binops, if, and calls)";
        return NULL;
    }

    const char *head = expr->children[0]->text;

    /* if -- real ternary, the one real control-flow form this v0 understands. */
    if (strcmp(head, "if") == 0) {
        if (expr->child_count != 4) {
            *out_error = "emit_java: if requires exactly (if cond then else)";
            return NULL;
        }
        const char *cond = emit_java_expr(arena, expr->children[1], out_error);
        if (!cond) return NULL;
        const char *then_e = emit_java_expr(arena, expr->children[2], out_error);
        if (!then_e) return NULL;
        const char *else_e = emit_java_expr(arena, expr->children[3], out_error);
        if (!else_e) return NULL;
        JavaBuf b;
        jb_init(&b);
        jb_appendf(&b, "(%s ? %s : %s)", cond, then_e, else_e);
        const char *result = arena_strdup(arena, b.data, b.len);
        jb_free(&b);
        return result;
    }

    /* real, narrow binop set -- exactly 2 operands, matching every real call site this mirrors. */
    const char *java_op = find_binop(head);
    if (java_op) {
        if (expr->child_count != 3) {
            *out_error = "emit_java: binary operator requires exactly 2 operands (v0 has no variadic +/and/or)";
            return NULL;
        }
        const char *lhs = emit_java_expr(arena, expr->children[1], out_error);
        if (!lhs) return NULL;
        const char *rhs = emit_java_expr(arena, expr->children[2], out_error);
        if (!rhs) return NULL;
        JavaBuf b;
        jb_init(&b);
        jb_appendf(&b, "(%s %s %s)", lhs, java_op, rhs);
        const char *result = arena_strdup(arena, b.data, b.len);
        jb_free(&b);
        return result;
    }

    /* Real, recognized external math primitives -- the table above. */
    const MathPrimEntry *math_prim = find_math_prim(head);
    if (math_prim) {
        size_t got_args = expr->child_count - 1;
        if ((int)got_args != math_prim->arg_count) {
            *out_error = "emit_java: math primitive called with the wrong number of arguments";
            return NULL;
        }
        JavaBuf b;
        jb_init(&b);
        jb_appendf(&b, "%s(", math_prim->java_fn);
        for (size_t i = 1; i < expr->child_count; i++) {
            if (i > 1) jb_append(&b, ", ");
            const char *arg = emit_java_expr(arena, expr->children[i], out_error);
            if (!arg) {
                jb_free(&b);
                return NULL;
            }
            jb_append(&b, arg);
        }
        jb_append(&b, ")");
        const char *result = arena_strdup(arena, b.data, b.len);
        jb_free(&b);
        return result;
    }

    /* Otherwise: a real call to another top-level defn in the same generated file (a real, plain
       unqualified static-method call -- both are `static` methods of the one wrapping class, so no
       receiver/class-qualifier is needed, same as any real Java method calling a sibling static
       method in its own class). */
    JavaBuf b;
    jb_init(&b);
    jb_appendf(&b, "%s(", camel_case(arena, head));
    for (size_t i = 1; i < expr->child_count; i++) {
        if (i > 1) jb_append(&b, ", ");
        const char *arg = emit_java_expr(arena, expr->children[i], out_error);
        if (!arg) {
            jb_free(&b);
            return NULL;
        }
        jb_append(&b, arg);
    }
    jb_append(&b, ")");
    const char *result = arena_strdup(arena, b.data, b.len);
    jb_free(&b);
    return result;
}

/* emit_java_defn: one top-level (defn name [(param : Type) ...] : RetType body) -> one real
   `public static <RetType> <camelName>(<params>) { return <expr>; }` method body line, appended
   into `out` (the caller wraps everything already appended by the time it emits into one real
   class -- see emit_java's own doc comment below). Real, narrow scope identical to emit_ts.c's own
   emit_ts_defn (see that file's own doc comment for the full real rationale). */
static int emit_java_defn(Arena *arena, JavaBuf *out, Node *defn, const char **out_error) {
    if (defn->child_count < 3 || defn->children[1]->type != NODE_SYMBOL || defn->children[2]->type != NODE_VEC) {
        *out_error = "emit_java: defn: malformed function definition";
        return 0;
    }
    const char *fn_name = camel_case(arena, defn->children[1]->text);
    Node *params = defn->children[2];

    JavaBuf param_list;
    jb_init(&param_list);
    for (size_t i = 0; i < params->child_count; i++) {
        Node *param = params->children[i];
        if (param->type != NODE_LIST || param->child_count != 3 || param->children[0]->type != NODE_SYMBOL ||
            param->children[1]->type != NODE_COLON || param->children[2]->type != NODE_SYMBOL) {
            *out_error = "emit_java: defn: unsupported parameter shape (v0 only understands plain "
                         "(name : I32|F64|Bool|String) params -- no Arena/region annotations)";
            jb_free(&param_list);
            return 0;
        }
        const char *p_type = resolve_java_type(param->children[2], out_error);
        if (!p_type) {
            jb_free(&param_list);
            return 0;
        }
        if (i > 0) jb_append(&param_list, ", ");
        /* Java's own real parameter order is `Type name` (the reverse of TypeScript's own
           `name: Type`) -- a real, plain target-language syntax difference, not a design choice. */
        jb_appendf(&param_list, "%s %s", p_type, camel_case(arena, param->children[0]->text));
    }

    /* Return type + body: same real (defn name [params] : RetType body) 6-child shape emit_ts.c's
       own emit_ts_defn already validates -- see that file's own doc comment for the full real
       rationale, not repeated here. */
    if (defn->child_count != 6 || defn->children[3]->type != NODE_COLON) {
        *out_error = "emit_java: defn: expected (defn name [params] : RetType body) with exactly one body expression";
        jb_free(&param_list);
        return 0;
    }
    const char *ret_type = resolve_java_type(defn->children[4], out_error);
    if (!ret_type) {
        jb_free(&param_list);
        return 0;
    }
    const char *body = emit_java_expr(arena, defn->children[5], out_error);
    if (!body) {
        jb_free(&param_list);
        return 0;
    }

    jb_appendf(out, "    public static %s %s(%s) {\n        return %s;\n    }\n\n", ret_type, fn_name,
               param_list.data, body);
    jb_free(&param_list);
    return 1;
}

const char *emit_java(Arena *arena, Node *program, const char *class_name, const char **out_error) {
    JavaBuf out;
    jb_init(&out);
    jb_append(&out, "// Generated by parena build (Java target) -- VS0-for-Java v0, do not edit by hand.\n\n");
    jb_appendf(&out, "public final class %s {\n", class_name);

    for (size_t i = 0; i < program->child_count; i++) {
        Node *form = program->children[i];
        /* (module ...) / (export ...) / (import ...) are real, no-op metadata for this v0, same
           real precedent emit_ts.c's own top-level emit_ts() already establishes -- every
           top-level defn becomes a real `public static` method of the one wrapping class
           unconditionally, so a real (export name) list doesn't change what actually gets
           emitted. */
        if (is_call_named(form, "module") || is_call_named(form, "export") || is_call_named(form, "import")) {
            continue;
        }
        if (is_call_named(form, "defn")) {
            if (!emit_java_defn(arena, &out, form, out_error)) {
                jb_free(&out);
                return NULL;
            }
            continue;
        }
        *out_error = "emit_java: unsupported top-level form (v0 only understands defn, module, export, import)";
        jb_free(&out);
        return NULL;
    }

    jb_append(&out, "}\n");
    const char *result = arena_strdup(arena, out.data, out.len);
    jb_free(&out);
    return result;
}
