/* emit_lua.c -- real, v0, narrow-scope Lua/Luau emitter. See emit_lua.h's own header comment for the scope
 * statement and the target-specific lowering rules (this file does not repeat them). */
#include "emit_lua.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} LuaBuf;

static void lb_init(LuaBuf *b) {
    b->cap = 256;
    b->data = malloc(b->cap);
    b->data[0] = '\0';
    b->len = 0;
}

static void lb_free(LuaBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void lb_append(LuaBuf *b, const char *s) {
    size_t add_len = strlen(s);
    if (b->len + add_len + 1 > b->cap) {
        while (b->len + add_len + 1 > b->cap) b->cap *= 2;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, add_len + 1);
    b->len += add_len;
}

/* Exactly-sized formatting: a fixed buffer silently truncated long `if` ladders in the Java emitter once, and
 * these files are all long ladders by nature. */
static void lb_appendf(LuaBuf *b, const char *fmt, ...) {
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
    lb_append(b, tmp);
    free(tmp);
}

static int is_symbol(Node *n, const char *text) {
    return n && n->type == NODE_SYMBOL && strcmp(n->text, text) == 0;
}

static int is_call_named(Node *n, const char *name) {
    return n && n->type == NODE_LIST && n->child_count >= 1 && is_symbol(n->children[0], name);
}

static const char *camel_case(Arena *arena, const char *kebab) {
    size_t len = strlen(kebab);
    char *out = arena_alloc(arena, len + 1);
    size_t oi = 0;
    int capitalize_next = 0;
    for (size_t i = 0; i < len; i++) {
        char c = kebab[i];
        if (c == '-') { capitalize_next = 1; continue; }
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

/* BINOP_TABLE -- the same narrow set the other targets recognize, lowered per emit_lua.h's own notes: `/` is
 * integer division, so it must be `//`; `=` is equality, so it must be `==`. `and`/`or` are already Lua. */
typedef struct { const char *prn_op; const char *lua_op; } BinopEntry;
static const BinopEntry BINOP_TABLE[] = {
    {"+", "+"}, {"-", "-"}, {"*", "*"}, {"/", "//"},
    {"=", "=="}, {"<", "<"}, {">", ">"}, {"<=", "<="}, {">=", ">="},
    {"and", "and"}, {"or", "or"},
};
#define BINOP_TABLE_COUNT (sizeof(BINOP_TABLE) / sizeof(BINOP_TABLE[0]))

static const char *find_binop(const char *prn_op) {
    for (size_t i = 0; i < BINOP_TABLE_COUNT; i++) {
        if (strcmp(BINOP_TABLE[i].prn_op, prn_op) == 0) return BINOP_TABLE[i].lua_op;
    }
    return NULL;
}

typedef struct { const char *prn_name; const char *lua_fn; int arg_count; } MathPrimEntry;
static const MathPrimEntry MATH_PRIM_TABLE[] = {
    {"math/random-f64", "math.random", 0},
    {"math/floor", "math.floor", 1},
    {"math/sqrt", "math.sqrt", 1},
    {"math/log", "math.log", 1},
    {"math/cos", "math.cos", 1},
};
#define MATH_PRIM_TABLE_COUNT (sizeof(MATH_PRIM_TABLE) / sizeof(MATH_PRIM_TABLE[0]))

static const MathPrimEntry *find_math_prim(const char *name) {
    for (size_t i = 0; i < MATH_PRIM_TABLE_COUNT; i++) {
        if (strcmp(MATH_PRIM_TABLE[i].prn_name, name) == 0) return &MATH_PRIM_TABLE[i];
    }
    return NULL;
}

static const char *emit_lua_expr(Arena *arena, Node *expr, const char **out_error);

static const char *emit_lua_expr(Arena *arena, Node *expr, const char **out_error) {
    if (!expr) {
        *out_error = "emit_lua: null expression";
        return NULL;
    }

    if (expr->type == NODE_NUMBER) {
        return arena_strdup(arena, expr->text, expr->text_len);
    }

    if (expr->type == NODE_STRING) {
        /* Strings are real values in this emitter (BIG_O's rules return them for callsigns/lines). Lua escapes are
         * the same for the ordinary quote/backslash cases these tables use; %q-style handling is deliberately not
         * attempted here, so an unsupported escape is the source author's problem, named rather than mangled. */
        LuaBuf b;
        lb_init(&b);
        lb_appendf(&b, "\"%.*s\"", (int)expr->text_len, expr->text);
        const char *result = arena_strdup(arena, b.data, b.len);
        lb_free(&b);
        return result;
    }

    if (expr->type == NODE_SYMBOL) {
        if (strcmp(expr->text, "math/pi") == 0) return "math.pi";
        if (strcmp(expr->text, "true") == 0 || strcmp(expr->text, "false") == 0) return expr->text;
        return camel_case(arena, expr->text);
    }

    if (expr->type != NODE_LIST || expr->child_count == 0 || expr->children[0]->type != NODE_SYMBOL) {
        *out_error = "emit_lua: unsupported expression form (v0 only understands numbers, strings, symbols, "
                     "binops, if, not, and calls)";
        return NULL;
    }

    const char *head = expr->children[0]->text;

    if (strcmp(head, "if") == 0) {
        if (expr->child_count != 4) {
            *out_error = "emit_lua: if requires exactly (if cond then else)";
            return NULL;
        }
        const char *cond = emit_lua_expr(arena, expr->children[1], out_error);
        if (!cond) return NULL;
        const char *then_e = emit_lua_expr(arena, expr->children[2], out_error);
        if (!then_e) return NULL;
        const char *else_e = emit_lua_expr(arena, expr->children[3], out_error);
        if (!else_e) return NULL;
        LuaBuf b;
        lb_init(&b);
        /* NOT `(cond and then or else)`. That idiom is wrong whenever the THEN branch's VALUE is
         * false: `and false or` discards the false and takes the else. Worse -- and this is the one
         * that bit -- a false branch inside a nested ladder falls through into the NEXT `or` branch, so
         * an if-chain that RETURNS Bool answers with whatever later branch happens to be true. That is
         * not hypothetical: DEADWEIGHT's `fx-cond-ok` is a 21-branch if-ladder returning Bool, and the
         * ternary lowering made "opponent kind == 2" answer true whenever the ladder's own final
         * `roll >= PP` test passed -- a live rules bug in the Luau port, caught by the match parity
         * vectors in PARENA/stdlib/deadweight/match_parity.txt, that a strict `if` never has. Lua has no
         * expression-if, so this lowers to an immediately-invoked function: still ONE expression (so it
         * composes inside any parent expression), branches still evaluated LAZILY, and false is a value
         * like any other. */
        lb_appendf(&b, "(function() if %s then return %s else return %s end end)()", cond, then_e, else_e);
        const char *result = arena_strdup(arena, b.data, b.len);
        lb_free(&b);
        return result;
    }

    if (strcmp(head, "not") == 0) {
        if (expr->child_count != 2) {
            *out_error = "emit_lua: not requires exactly 1 operand";
            return NULL;
        }
        const char *inner = emit_lua_expr(arena, expr->children[1], out_error);
        if (!inner) return NULL;
        LuaBuf b;
        lb_init(&b);
        lb_appendf(&b, "(not (%s))", inner);
        const char *result = arena_strdup(arena, b.data, b.len);
        lb_free(&b);
        return result;
    }

    /* `let` -- found missing by the first REAL rules module this target was pointed at
     * (stdlib/deadweight/citizen_policy.prn: "unsupported expression form" on the binding vector, which
     * had fallen through to the call branch and been emitted as an argument). A `let` is a sequence of
     * scoped bindings around one expression, and Lua has no expression-local binding, so it lowers to an
     * immediately-invoked function: `(function() local a = ...; return ... end)()`. That is still ONE
     * expression, so it composes inside the `(cond and then or else)` an `if` lowers to, and the bindings
     * are `local` in order -- which is exactly `let`'s own rule that each binding sees the ones before it.
     * v0 shape: (let [name expr ...] body), one body expression, no `do` sequence. */
    if (strcmp(head, "let") == 0) {
        if (expr->child_count != 3 || expr->children[1]->type != NODE_VEC) {
            *out_error = "emit_lua: let requires (let [name expr ...] body)";
            return NULL;
        }
        Node *bindings = expr->children[1];
        if (bindings->child_count % 2 != 0) {
            *out_error = "emit_lua: let bindings must be name/expression pairs";
            return NULL;
        }
        LuaBuf b;
        lb_init(&b);
        lb_append(&b, "(function() ");
        for (size_t i = 0; i + 1 < bindings->child_count; i += 2) {
            Node *name = bindings->children[i];
            if (name->type != NODE_SYMBOL) {
                *out_error = "emit_lua: let binding name must be a symbol";
                lb_free(&b);
                return NULL;
            }
            const char *value = emit_lua_expr(arena, bindings->children[i + 1], out_error);
            if (!value) { lb_free(&b); return NULL; }
            lb_appendf(&b, "local %s = %s; ", camel_case(arena, name->text), value);
        }
        const char *body = emit_lua_expr(arena, expr->children[2], out_error);
        if (!body) { lb_free(&b); return NULL; }
        lb_appendf(&b, "return %s end)()", body);
        const char *result = arena_strdup(arena, b.data, b.len);
        lb_free(&b);
        return result;
    }

    /* Statement forms this target cannot express in expression position (no `do` sequence, no mutation, no
     * loops, no type declarations). WITHOUT this they fell through to the "call to another defn" branch below
     * and emitted `M.do(1, 2)` / `M.set!(...)` -- plausible-LOOKING Lua that only a real interpreter rejects on
     * the next run, which is the worst of both worlds: the emitter says it succeeded. Found by this target's
     * own new test (`(defn f [] : I32 (do 1 2))` emitted `M.do(1, 2)` instead of failing), the same class of gap
     * the C emitter's own honest-refusal list exists to prevent. A named refusal beats a guessed call. */
    static const char *const UNSUPPORTED_STATEMENT_FORMS[] = {
        "do", "set!", "while", "dotimes", "loop", "recur", "def", "defstruct", "defenum", "match", "case"
    };
    for (size_t i = 0; i < sizeof(UNSUPPORTED_STATEMENT_FORMS) / sizeof(UNSUPPORTED_STATEMENT_FORMS[0]); i++) {
        if (strcmp(head, UNSUPPORTED_STATEMENT_FORMS[i]) == 0) {
            *out_error = "emit_lua: unsupported statement form in expression position (v0 has no do/set!/loops/"
                         "declarations -- only defn, let, if, not, binops and calls)";
            return NULL;
        }
    }

    const char *lua_op = find_binop(head);
    if (lua_op) {
        if (expr->child_count != 3) {
            *out_error = "emit_lua: binary operator requires exactly 2 operands (v0 has no variadic +/and/or)";
            return NULL;
        }
        const char *lhs = emit_lua_expr(arena, expr->children[1], out_error);
        if (!lhs) return NULL;
        const char *rhs = emit_lua_expr(arena, expr->children[2], out_error);
        if (!rhs) return NULL;
        LuaBuf b;
        lb_init(&b);
        /* `/` is NOT `//`. Luau's and Lua 5.4's `//` FLOOR; the C emitter's `/` on an I32 TRUNCATES toward
         * zero, and those differ for every negative dividend: -70/50 is -1 in C and -2 with `//`. That is
         * not a corner: DEADWEIGHT's rating ladder divides a (theirs - mine) difference by 50 and by 100,
         * which is negative whenever the higher-rated player is the one being asked about, so the two
         * worlds printed different leaderboard numbers for the same server. `/` therefore lowers to a
         * helper that reproduces C's truncation exactly (defined once in the module prologue below). */
        if (strcmp(head, "/") == 0) {
            lb_appendf(&b, "__idiv(%s, %s)", lhs, rhs);
        } else {
            lb_appendf(&b, "(%s %s %s)", lhs, lua_op, rhs);
        }
        const char *result = arena_strdup(arena, b.data, b.len);
        lb_free(&b);
        return result;
    }

    const MathPrimEntry *math_prim = find_math_prim(head);
    if (math_prim) {
        size_t got_args = expr->child_count - 1;
        if ((int)got_args != math_prim->arg_count) {
            *out_error = "emit_lua: math primitive called with the wrong number of arguments";
            return NULL;
        }
        LuaBuf b;
        lb_init(&b);
        lb_appendf(&b, "%s(", math_prim->lua_fn);
        for (size_t i = 1; i < expr->child_count; i++) {
            if (i > 1) lb_append(&b, ", ");
            const char *arg = emit_lua_expr(arena, expr->children[i], out_error);
            if (!arg) { lb_free(&b); return NULL; }
            lb_append(&b, arg);
        }
        lb_append(&b, ")");
        const char *result = arena_strdup(arena, b.data, b.len);
        lb_free(&b);
        return result;
    }

    /* Otherwise: a call to another defn in this same module. Emitted through `M.` because that is how the
     * function itself was declared, so an intra-module call resolves without any global namespace. */
    LuaBuf b;
    lb_init(&b);
    lb_appendf(&b, "M.%s(", camel_case(arena, head));
    for (size_t i = 1; i < expr->child_count; i++) {
        if (i > 1) lb_append(&b, ", ");
        const char *arg = emit_lua_expr(arena, expr->children[i], out_error);
        if (!arg) { lb_free(&b); return NULL; }
        lb_append(&b, arg);
    }
    lb_append(&b, ")");
    const char *result = arena_strdup(arena, b.data, b.len);
    lb_free(&b);
    return result;
}

/* find_target_lua_src -- `:lua (inline-lua "...")` from a `#target {...}` map, the same FFI escape hatch
 * emit.c's `:c` and emit_java.c's `:java` already provide. */
static Node *find_target_lua_src(Node *target_map, const char **out_error) {
    if (target_map->child_count % 2 != 0) {
        *out_error = "emit_lua: #target map has an odd number of forms (expected key/value pairs)";
        return NULL;
    }
    Node *lua_value = NULL;
    for (size_t i = 0; i + 1 < target_map->child_count; i += 2) {
        Node *key = target_map->children[i];
        if (key->type == NODE_KEYWORD && key->text && strcmp(key->text, ":lua") == 0) {
            lua_value = target_map->children[i + 1];
            break;
        }
    }
    if (!lua_value) {
        *out_error = "emit_lua: #target map has no :lua key";
        return NULL;
    }
    if (lua_value->type != NODE_LIST || lua_value->child_count != 2 ||
        lua_value->children[0]->type != NODE_SYMBOL || !is_symbol(lua_value->children[0], "inline-lua") ||
        lua_value->children[1]->type != NODE_STRING) {
        *out_error = "emit_lua: #target :lua value must be (inline-lua \"...\")";
        return NULL;
    }
    return lua_value->children[1];
}

static int emit_target_lua_defn(LuaBuf *out, Node *target_map, const char *fn_name,
                                const char *param_list, const char **out_error) {
    Node *src = find_target_lua_src(target_map, out_error);
    if (!src) return 0;
    lb_appendf(out, "function M.%s(%s)\n    %.*s\nend\n\n", fn_name, param_list, (int)src->text_len, src->text);
    return 1;
}

static int emit_lua_defn(Arena *arena, LuaBuf *out, Node *defn, const char **out_error) {
    if (defn->child_count < 3 || defn->children[1]->type != NODE_SYMBOL || defn->children[2]->type != NODE_VEC) {
        *out_error = "emit_lua: defn: malformed function definition";
        return 0;
    }
    const char *fn_name = camel_case(arena, defn->children[1]->text);
    Node *params = defn->children[2];

    LuaBuf param_list;
    lb_init(&param_list);
    for (size_t i = 0; i < params->child_count; i++) {
        Node *param = params->children[i];
        if (param->type != NODE_LIST || param->child_count != 3 || param->children[0]->type != NODE_SYMBOL ||
            param->children[1]->type != NODE_COLON || param->children[2]->type != NODE_SYMBOL) {
            *out_error = "emit_lua: defn: unsupported parameter shape (v0 only understands plain "
                         "(name : I32|F64|Bool|String) params -- no Arena/region annotations)";
            lb_free(&param_list);
            return 0;
        }
        /* Lua has no type annotations on parameters, so `(x : I32)` emits as just `x`. */
        if (i > 0) lb_append(&param_list, ", ");
        lb_append(&param_list, camel_case(arena, param->children[0]->text));
    }

    if (defn->child_count == 7 && is_symbol(defn->children[5], "#target")) {
        if (defn->children[6]->type != NODE_MAP) {
            *out_error = "emit_lua: defn: #target must be followed by a {...} map";
            lb_free(&param_list);
            return 0;
        }
        int ok = emit_target_lua_defn(out, defn->children[6], fn_name, param_list.data, out_error);
        lb_free(&param_list);
        return ok;
    }

    if (defn->child_count != 6 || defn->children[3]->type != NODE_COLON) {
        *out_error = "emit_lua: defn: expected (defn name [params] : RetType body) with exactly one body "
                     "expression, or a #target {...} FFI body";
        lb_free(&param_list);
        return 0;
    }
    const char *body = emit_lua_expr(arena, defn->children[5], out_error);
    if (!body) {
        lb_free(&param_list);
        return 0;
    }

    lb_appendf(out, "function M.%s(%s)\n    return %s\nend\n\n", fn_name, param_list.data, body);
    lb_free(&param_list);
    return 1;
}

const char *emit_lua(Arena *arena, Node *program, const char **out_error) {
    LuaBuf out;
    lb_init(&out);
    lb_append(&out, "-- Generated by parena build (Lua target) -- do not edit by hand.\n"
                    "-- Source of truth: the .prn this was emitted from. SHANKPIT compiles the SAME file to C.\n\n"
                    "local M = {}\n\n"
                    "-- `a / b` in PARENA is C's integer division (truncation toward zero), not Lua's `//`\n"
                    "-- (floor). They disagree on every negative dividend, so the emitter routes `/` here\n"
                    "-- instead of pretending the two are the same operator.\n"
                    "local function __idiv(a, b)\n"
                    "    local q = a // b\n"
                    "    if q < 0 and q * b ~= a then q = q + 1 end\n"
                    "    return q\n"
                    "end\n\n");

    for (size_t i = 0; i < program->child_count; i++) {
        Node *form = program->children[i];
        /* module/export/import/llvm-extern are metadata for other targets; a Lua module simply returns every
         * defn it was given, which is what `export` would have said anyway. */
        if (is_call_named(form, "module") || is_call_named(form, "export") || is_call_named(form, "import") ||
            is_call_named(form, "llvm-extern")) {
            continue;
        }
        if (is_call_named(form, "defn")) {
            if (!emit_lua_defn(arena, &out, form, out_error)) {
                lb_free(&out);
                return NULL;
            }
            continue;
        }
        *out_error = "emit_lua: unsupported top-level form (v0 only understands defn, module, export, import)";
        lb_free(&out);
        return NULL;
    }

    lb_append(&out, "return M\n");
    const char *result = arena_strdup(arena, out.data, out.len);
    lb_free(&out);
    return result;
}
