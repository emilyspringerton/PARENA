/* ast.h — the generic S-expression tree the parser produces. Deliberately
 * structural, not semantic: a List/Vec/Map of children plus atom leaves.
 * The region analyzer (a later, separate compiler phase — not part of
 * VS0's DoD domain 1) is what gives forms like (x : Type @ Region) their
 * actual meaning; the parser's job is only "is this well-formed
 * S-expression syntax," matching the DoD's own separation between the
 * "Lexer & Parser" and "Region Analyzer" verification domains.
 */
#ifndef PARENA_AST_H
#define PARENA_AST_H

#include "arena.h"
#include <stddef.h>

typedef enum {
    NODE_LIST,    /* (...) */
    NODE_VEC,     /* [...] */
    NODE_MAP,     /* {...} */
    NODE_SYMBOL,
    NODE_KEYWORD,
    NODE_STRING,
    NODE_NUMBER,
    NODE_COLON,   /* standalone ':' inside a form, e.g. (x : Type @ Region) --
                     kept as a real AST leaf rather than silently dropped so
                     the region analyzer can find it structurally later,
                     not by re-lexing */
    NODE_AT,      /* standalone '@', same reasoning as NODE_COLON */
} NodeType;

typedef struct Node {
    NodeType type;
    int line;
    /* Atom payload (SYMBOL/KEYWORD/STRING/NUMBER) */
    const char *text;
    size_t text_len;
    /* Compound payload (LIST/VEC/MAP) */
    struct Node **children;
    size_t child_count;
    size_t child_capacity;
} Node;

Node *node_new_atom(Arena *a, NodeType type, const char *text, size_t text_len, int line);
Node *node_new_compound(Arena *a, NodeType type, int line);
void node_push_child(Arena *a, Node *parent, Node *child);

#endif /* PARENA_AST_H */
