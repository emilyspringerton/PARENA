/* parser.h — recursive-descent reader turning a token stream into the
 * generic S-expression AST (ast.h). DoD domain 1's other half: "Reads
 * valid .prn files into an AST... Unit tests on balanced and imbalanced
 * S-expressions."
 */
#ifndef PARENA_PARSER_H
#define PARENA_PARSER_H

#include "arena.h"
#include "ast.h"
#include <stddef.h>

/* parse_program: parses `src` (length `len`) as a top-level sequence of
 * forms, returning a synthetic NODE_LIST wrapping all of them (an empty
 * file parses to an empty list, not an error). All AST nodes and token
 * text are allocated from `arena`.
 *
 * On success, returns non-NULL and sets *out_error to NULL.
 * On a lex or parse error (unterminated string, unbalanced brackets,
 * mismatched bracket kind, unexpected EOF mid-form), returns NULL and
 * sets *out_error to an arena-owned message including the line number --
 * this is the "imbalanced S-expressions" failure path the DoD's unit
 * tests exercise. */
Node *parse_program(Arena *arena, const char *src, size_t len, const char **out_error);

#endif /* PARENA_PARSER_H */
