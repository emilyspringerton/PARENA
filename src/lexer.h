/* lexer.h — tokenizes .prn source text into the stream the parser reads.
 * DoD domain 1 ("Lexer & Parser"): reads valid .prn files into an AST with
 * no heap allocation outside the compiler's bump arena — token text is
 * copied into the Arena (arena_strdup), never malloc'd separately.
 */
#ifndef PARENA_LEXER_H
#define PARENA_LEXER_H

#include "arena.h"
#include <stddef.h>

typedef enum {
    TOK_LPAREN,   /* ( */
    TOK_RPAREN,   /* ) */
    TOK_LBRACKET, /* [ */
    TOK_RBRACKET, /* ] */
    TOK_LBRACE,   /* { */
    TOK_RBRACE,   /* } */
    TOK_COLON,    /* standalone ':' in a type/region signature, e.g. (x : Type @ Region) --
                     distinct from a KEYWORD token, which is ':' immediately followed by
                     an identifier with no space (:region/scratch) */
    TOK_AT,       /* '@' in a region signature: (x : Type @ Region) */
    TOK_SYMBOL,   /* identifier, including !-prefixed (linear) and &/&mut-prefixed
                     (borrow) forms -- ! and & are valid leading/constituent characters,
                     not separate operator tokens, matching this language's own
                     Lisp-family reader conventions */
    TOK_KEYWORD,  /* :foo, :region/scratch -- colon with no preceding/following space
                     before the identifier */
    TOK_STRING,   /* "..." with \n \t \" \\ escapes */
    TOK_NUMBER,   /* integer or decimal literal */
    TOK_EOF,
} TokenType;

typedef struct {
    TokenType type;
    const char *text; /* arena-owned, NUL-terminated; NULL for punctuation tokens
                          (parens/brackets/braces/colon/at) which need no payload */
    size_t text_len;
    int line; /* 1-based; DoD's own required error format is "...at line X" */
} Token;

typedef struct {
    const char *src;
    size_t pos;
    size_t len;
    int line;
    Arena *arena;
} Lexer;

void lexer_init(Lexer *lx, const char *src, size_t len, Arena *arena);

/* lexer_next: returns the next token, advancing the lexer. Returns a
 * TOK_EOF token (repeatedly, if called again) at end of input. On a
 * malformed token (unterminated string, stray character), returns a
 * token with type set to TOK_EOF and *out_error set to a non-NULL,
 * arena-owned message describing the problem and line — callers that
 * care about lex errors distinct from clean EOF must pass a non-NULL
 * out_error and check it. */
Token lexer_next(Lexer *lx, const char **out_error);

#endif /* PARENA_LEXER_H */
