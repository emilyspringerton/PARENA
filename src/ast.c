#include "ast.h"

Node *node_new_atom(Arena *a, NodeType type, const char *text, size_t text_len, int line) {
    Node *n = (Node *)arena_alloc(a, sizeof(Node));
    n->type = type;
    n->line = line;
    n->text = text;
    n->text_len = text_len;
    n->children = NULL;
    n->child_count = 0;
    n->child_capacity = 0;
    return n;
}

Node *node_new_compound(Arena *a, NodeType type, int line) {
    Node *n = (Node *)arena_alloc(a, sizeof(Node));
    n->type = type;
    n->line = line;
    n->text = NULL;
    n->text_len = 0;
    n->children = NULL;
    n->child_count = 0;
    n->child_capacity = 0;
    return n;
}

/* node_push_child: grows children in-place by doubling. Reallocates via
 * the arena (a fresh, bigger block copied over) rather than realloc(),
 * since arena memory was never malloc'd per-allocation in the first place
 * -- consistent with "no heap allocation outside the compiler's bump
 * arena" for every AST-building path, not just the initial alloc. */
void node_push_child(Arena *a, Node *parent, Node *child) {
    if (parent->child_count == parent->child_capacity) {
        size_t new_cap = parent->child_capacity == 0 ? 4 : parent->child_capacity * 2;
        Node **new_arr = (Node **)arena_alloc(a, new_cap * sizeof(Node *));
        for (size_t i = 0; i < parent->child_count; i++) new_arr[i] = parent->children[i];
        parent->children = new_arr;
        parent->child_capacity = new_cap;
    }
    parent->children[parent->child_count++] = child;
}
