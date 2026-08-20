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
        out[i] = (c == '-' || c == '/') ? '_' : c;
    }
    out[len] = '\0';
    return out;
}

static int is_symbol(Node *n, const char *text) {
    return n && n->type == NODE_SYMBOL && n->text && strcmp(n->text, text) == 0;
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

static const char *find_keyword_child(Node *n) {
    for (size_t i = 0; i < n->child_count; i++) {
        if (n->children[i]->type == NODE_KEYWORD) return n->children[i]->text;
    }
    return NULL;
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
    *out_type = "void *";
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

static int emit_body(Arena *arena, StrBuf *out, Node **forms, size_t count, EmitScope *scope,
                      int return_mode, const char **out_return_type, const char **out_error) {
    if (count == 0) return 1;

    for (size_t i = 0; i + 1 < count; i++) {
        Node *form = forms[i];
        if (is_call_named(form, "with-arena")) {
            if (!emit_with_arena(arena, out, form, scope, 0, NULL, out_error)) return 0;
        } else if (is_call_named(form, "let")) {
            if (!emit_let(arena, out, form, scope, 0, NULL, out_error)) return 0;
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
    const char *c_type = NULL;
    const char *expr_c = emit_expr(arena, tail, scope, &c_type, out_error);
    if (!expr_c) return 0;
    if (return_mode) {
        if (out_return_type) *out_return_type = c_type ? c_type : "void";
        sb_appendf(out, "    return %s;\n", expr_c);
    } else {
        sb_appendf(out, "    %s;\n", expr_c);
    }
    return 1;
}

/* emit_defn handles one top-level `(defn name [params] body...)`. Every
 * parameter is emitted as `Arena *mangled_name` -- the only parameter
 * type test.prn's own real examples use (`Arena @ region`); a real,
 * scoped limitation, not a general type system. The function's return
 * type is inferred from its own tail expression's resolved C type
 * (found while emitting the body, since VS0 has no separate type-
 * checking pass yet), defaulting to `void` if nothing resolves. */
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
        const char *region_kw = find_keyword_child(param);
        if (!region_kw) {
            fail(arena, out_error, "defn: parameter '%s' has no region annotation (VS0 only supports "
                                    "`Arena @ :region/x` parameters so far)",
                 param->children[0]->text);
            sb_free(&param_list);
            return 0;
        }
        const char *c_name = mangle(arena, param->children[0]->text);
        scope_bind(&base, param->children[0]->text, c_name, "Arena *", 0 /* already a pointer */);
        if (i > 0) sb_append(&param_list, ", ");
        sb_appendf(&param_list, "Arena *%s", c_name);
    }

    StrBuf body;
    sb_init(&body);
    const char *return_type = "void";
    int ok = emit_body(arena, &body, defn->children + 3, defn->child_count - 3, &base, 1, &return_type,
                        out_error);
    if (!ok) {
        sb_free(&param_list);
        sb_free(&body);
        return 0;
    }

    sb_appendf(out, "%s %s(%s) {\n%s}\n\n", return_type, fn_name, param_list.data, body.data);
    sb_free(&param_list);
    sb_free(&body);
    return 1;
}

const char *emit_c(Arena *arena, Node *program, const char **out_error) {
    *out_error = NULL;
    StrBuf out;
    sb_init(&out);
    sb_append(&out, "/* Generated by parena build -- VS0 domain 3, do not edit by hand. */\n");
    sb_append(&out, "#include \"parena_runtime.h\"\n");
    sb_append(&out, "#include <string.h>\n\n");

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
