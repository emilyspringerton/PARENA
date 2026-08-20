#include "region.h"
#include <stdio.h>
#include <string.h>

#define MAX_BINDINGS 64

typedef struct {
    const char *name;
    int rank;                /* -1 = unknown/unconstrained */
    const char *region_name; /* e.g. ":region/scratch"; NULL if rank == -1 */
} Binding;

typedef struct Scope {
    struct Scope *parent;
    Binding bindings[MAX_BINDINGS];
    int count;
} Scope;

static void scope_init(Scope *s, Scope *parent) {
    s->parent = parent;
    s->count = 0;
}

static void scope_bind(Scope *s, const char *name, int rank, const char *region_name) {
    if (s->count < MAX_BINDINGS) {
        s->bindings[s->count].name = name;
        s->bindings[s->count].rank = rank;
        s->bindings[s->count].region_name = region_name;
        s->count++;
    }
}

static Binding *scope_lookup(Scope *s, const char *name) {
    for (Scope *cur = s; cur; cur = cur->parent) {
        for (int i = cur->count - 1; i >= 0; i--) {
            if (strcmp(cur->bindings[i].name, name) == 0) return &cur->bindings[i];
        }
    }
    return NULL;
}

/* region_rank_for covers only the two ranks NORTHSTAR.md's own "Memory
 * model" section actually specifies (":region/scratch" = 0, ":region/
 * buffer" = 2) -- any other region keyword is real, unconstrained
 * territory (rank -1), not guessed at. */
static int region_rank_for(const char *keyword) {
    if (strcmp(keyword, ":region/scratch") == 0) return 0;
    if (strcmp(keyword, ":region/buffer") == 0) return 2;
    return -1;
}

static int is_symbol(Node *n, const char *text) {
    return n && n->type == NODE_SYMBOL && n->text && strcmp(n->text, text) == 0;
}

static int is_call_named(Node *n, const char *fn_name) {
    return n && n->type == NODE_LIST && n->child_count > 0 && is_symbol(n->children[0], fn_name);
}

/* find_keyword_child finds the first NODE_KEYWORD among n's direct
 * children -- pulls the region annotation out of a param form
 * `(name : Type @ :region/xxx)` without depending on its exact position
 * among the colon/at markers. */
static const char *find_keyword_child(Node *n) {
    for (size_t i = 0; i < n->child_count; i++) {
        if (n->children[i]->type == NODE_KEYWORD) return n->children[i]->text;
    }
    return NULL;
}

static char *fmt_error(Arena *arena, const char *src_region, const char *dst_region, int line) {
    char buf[256];
    snprintf(buf, sizeof(buf),
             "Compile Error: Escaping region pointer from %s to %s at line %d",
             src_region, dst_region, line);
    return arena_strdup(arena, buf, strlen(buf));
}

static const char *walk(Arena *arena, Node *node, Scope *scope);

/* walk_children recurses into every child of `node` under `scope`,
 * returning the first error found. The fallback for any compound form
 * this analyzer has no specific rule for, so a violation nested
 * arbitrarily deep inside an ordinary call's arguments, or a plain
 * vec/map literal, still gets caught. */
static const char *walk_children(Arena *arena, Node *node, Scope *scope) {
    for (size_t i = 0; i < node->child_count; i++) {
        const char *err = walk(arena, node->children[i], scope);
        if (err) return err;
    }
    return NULL;
}

/* check_call_escape is the actual invariant: in a call shaped `(fn
 * dest-expr src-expr...)`, if dest-expr names a binding with a known
 * region rank and any later src-expr names a binding with a strictly
 * lower rank (shorter-lived), that source's region pointer is escaping
 * into the destination's longer-lived scope -- exactly test.prn's own
 * `(buffer/set-data buf-arena bad-str)` shape. Real, honest limitation:
 * this only recognizes "first argument is the destination," not a
 * function's own real parameter semantics (VS0 has no function-
 * signature table yet) -- correct for every stdlib function shaped
 * like `set-data`/`write-string` (destination arena or handle first,
 * matching every STDLIB.md signature that takes one), not a general
 * call-graph analysis. */
static const char *check_call_escape(Arena *arena, Node *call, Scope *scope) {
    if (call->child_count < 3) return NULL; /* need fn + dest + >=1 source */

    Node *dest_node = call->children[1];
    if (dest_node->type != NODE_SYMBOL) return NULL;
    Binding *dest = scope_lookup(scope, dest_node->text);
    if (!dest || dest->rank < 0) return NULL;

    for (size_t i = 2; i < call->child_count; i++) {
        Node *arg = call->children[i];
        if (arg->type != NODE_SYMBOL) continue;
        Binding *src = scope_lookup(scope, arg->text);
        if (src && src->rank >= 0 && src->rank < dest->rank) {
            return fmt_error(arena, src->region_name, dest->region_name, arg->line);
        }
    }
    return NULL;
}

/* walk_with_arena handles `(with-arena [name region-kw size] body...)`:
 * binds `name` at region-kw's rank in a fresh child scope, then walks
 * every body form under it. */
static const char *walk_with_arena(Arena *arena, Node *node, Scope *scope) {
    if (node->child_count < 2 || node->children[1]->type != NODE_VEC) {
        return walk_children(arena, node, scope);
    }
    Node *binding_vec = node->children[1];
    if (binding_vec->child_count < 1 || binding_vec->children[0]->type != NODE_SYMBOL) {
        return walk_children(arena, node, scope);
    }
    const char *name = binding_vec->children[0]->text;
    const char *region_kw = NULL;
    for (size_t i = 0; i < binding_vec->child_count; i++) {
        if (binding_vec->children[i]->type == NODE_KEYWORD) {
            region_kw = binding_vec->children[i]->text;
            break;
        }
    }

    Scope child;
    scope_init(&child, scope);
    if (region_kw) scope_bind(&child, name, region_rank_for(region_kw), region_kw);
    else scope_bind(&child, name, -1, NULL);

    for (size_t i = 2; i < node->child_count; i++) {
        const char *err = walk(arena, node->children[i], &child);
        if (err) return err;
    }
    return NULL;
}

/* alloc_rank resolves the region rank an `(alloc arena-expr Type
 * value...)` call produces -- the allocated value inherits arena-expr's
 * own region, per NORTHSTAR's own "Scratch-to-buffer promotion" idiom
 * (a `let` binding built from `alloc` is exactly how that promotion
 * pattern is written). */
static int alloc_rank(Node *call, Scope *scope, const char **out_region_name) {
    *out_region_name = NULL;
    if (!is_call_named(call, "alloc") || call->child_count < 2) return -1;
    Node *arena_expr = call->children[1];
    if (arena_expr->type != NODE_SYMBOL) return -1;
    Binding *b = scope_lookup(scope, arena_expr->text);
    if (!b) return -1;
    *out_region_name = b->region_name;
    return b->rank;
}

/* walk_let handles `(let [name1 expr1 name2 expr2 ...] body...)`: each
 * binding's rank comes from alloc_rank if its expr is an `alloc` call
 * (the only rank-producing form this pass understands); anything else
 * binds as unconstrained (rank -1) -- a real, honest limitation, not a
 * silent wrong answer. Each expr is walked under the *outer* scope
 * first, so an escape nested inside a binding's own expr is caught
 * before the new bindings even exist. */
static const char *walk_let(Arena *arena, Node *node, Scope *scope) {
    if (node->child_count < 2 || node->children[1]->type != NODE_VEC) {
        return walk_children(arena, node, scope);
    }
    Node *bindings = node->children[1];

    Scope child;
    scope_init(&child, scope);

    for (size_t i = 0; i + 1 < bindings->child_count; i += 2) {
        Node *name_node = bindings->children[i];
        Node *expr_node = bindings->children[i + 1];
        if (name_node->type != NODE_SYMBOL) continue;

        const char *err = walk(arena, expr_node, scope);
        if (err) return err;

        const char *region_name = NULL;
        int rank = alloc_rank(expr_node, scope, &region_name);
        scope_bind(&child, name_node->text, rank, region_name);
    }

    for (size_t i = 2; i < node->child_count; i++) {
        const char *err = walk(arena, node->children[i], &child);
        if (err) return err;
    }
    return NULL;
}

static const char *walk(Arena *arena, Node *node, Scope *scope) {
    if (!node) return NULL;

    if (is_call_named(node, "with-arena")) return walk_with_arena(arena, node, scope);
    if (is_call_named(node, "let")) return walk_let(arena, node, scope);

    if (node->type == NODE_LIST && node->child_count > 0 && node->children[0]->type == NODE_SYMBOL) {
        const char *err = check_call_escape(arena, node, scope);
        if (err) return err;
    }

    return walk_children(arena, node, scope);
}

/* analyze_defn handles one top-level `(defn name [params] body...)`:
 * builds the base scope from each param's own region annotation, then
 * walks the body. */
static const char *analyze_defn(Arena *arena, Node *defn) {
    if (defn->child_count < 3 || defn->children[2]->type != NODE_VEC) return NULL;
    Node *params = defn->children[2];

    Scope base;
    scope_init(&base, NULL);

    for (size_t i = 0; i < params->child_count; i++) {
        Node *param = params->children[i];
        if (param->type != NODE_LIST || param->child_count == 0) continue;
        if (param->children[0]->type != NODE_SYMBOL) continue;
        const char *name = param->children[0]->text;
        const char *region_kw = find_keyword_child(param);
        if (region_kw) scope_bind(&base, name, region_rank_for(region_kw), region_kw);
        else scope_bind(&base, name, -1, NULL);
    }

    for (size_t i = 3; i < defn->child_count; i++) {
        const char *err = walk(arena, defn->children[i], &base);
        if (err) return err;
    }
    return NULL;
}

const char *region_analyze(Arena *arena, Node *program) {
    for (size_t i = 0; i < program->child_count; i++) {
        Node *form = program->children[i];
        if (is_call_named(form, "defn")) {
            const char *err = analyze_defn(arena, form);
            if (err) return err;
        }
    }
    return NULL;
}
