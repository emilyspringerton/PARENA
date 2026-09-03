#include "lexer.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

void lexer_init(Lexer *lx, const char *src, size_t len, Arena *arena) {
    lx->src = src;
    lx->pos = 0;
    lx->len = len;
    lx->line = 1;
    lx->arena = arena;
}

static int at_end(Lexer *lx) { return lx->pos >= lx->len; }
static char peek(Lexer *lx) { return at_end(lx) ? '\0' : lx->src[lx->pos]; }
static char peek2(Lexer *lx) { return (lx->pos + 1 >= lx->len) ? '\0' : lx->src[lx->pos + 1]; }

static char advance(Lexer *lx) {
    char c = lx->src[lx->pos++];
    if (c == '\n') lx->line++;
    return c;
}

/* is_symbol_char: everything that can appear in a symbol, including the
 * leading character. Deliberately permissive, matching this language's own
 * Lisp-family idiom: !file, &var, &mut, set!, defn, string/parse-i32,
 * king_music_carrier-style-names, etc. are all valid symbols. Excludes
 * only characters with real structural meaning elsewhere in the grammar:
 * whitespace, the six bracket characters, the reader macros : @ " and the
 * comment character ;. */
static int is_symbol_char(char c) {
    if (isspace((unsigned char)c)) return 0;
    switch (c) {
        case '(': case ')': case '[': case ']': case '{': case '}':
        case ':': case '@': case '"': case ';':
            return 0;
        default:
            return 1;
    }
}

static void skip_whitespace_and_comments(Lexer *lx) {
    for (;;) {
        char c = peek(lx);
        if (c == '\0') return;
        if (isspace((unsigned char)c)) {
            advance(lx);
            continue;
        }
        if (c == ';') {
            /* Line comment -- ';' or ';;', both run to end of line, same
               convention as every Lisp-family reader. */
            while (!at_end(lx) && peek(lx) != '\n') advance(lx);
            continue;
        }
        return;
    }
}

static Token make_punct(TokenType type, int line) {
    Token t;
    t.type = type;
    t.text = NULL;
    t.text_len = 0;
    t.line = line;
    return t;
}

static Token lex_string(Lexer *lx, const char **out_error) {
    int start_line = lx->line;
    advance(lx); /* opening quote */
    char buf[4096];
    size_t n = 0;
    for (;;) {
        if (at_end(lx)) {
            /* No "at line N" suffix here -- the parser's fail() wrapper
               appends the current line itself for every lexer-reported
               error, so including it here too would double it up
               ("...at line 1 at line 1"). start_line (where the string
               opened) isn't lost: it's folded into the message text
               itself instead of the trailing position. */
            if (out_error) {
                char msg[128];
                snprintf(msg, sizeof(msg), "unterminated string literal (opened at line %d)", start_line);
                *out_error = arena_strdup(lx->arena, msg, strlen(msg));
            }
            Token t = make_punct(TOK_EOF, lx->line);
            return t;
        }
        char c = advance(lx);
        if (c == '"') break;
        if (c == '\\' && !at_end(lx)) {
            char e = advance(lx);
            switch (e) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                /* \r -- real, genuine gap found live (kanban priority-queue card 3124213,
                   stdlib/sip/message.prn's own real "\r\n" line terminators, SIP's -- and
                   HTTP's -- real, standard wire-format line ending): completely unhandled here
                   before this fix, so every "\r" in a real .prn string literal silently decoded
                   to a bare 'r' (this switch's own `default: c = e` fallback), not a real
                   carriage return -- confirmed live via a real gcc-compiled, run test asserting
                   round-trip re-parsing of a real built SIP request, which failed until this fix
                   landed. No real .prn stdlib file had ever needed a literal CR byte before this
                   one; net/http.prn's own real request/response line-building helpers were
                   themselves never designed (STDLIB.md's own honest note on that file), so this
                   is a genuinely new, first-ever real trigger, not a regression. */
                case 'r': c = '\r'; break;
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                default: c = e; break;
            }
        }
        if (n < sizeof(buf) - 1) buf[n++] = c;
    }
    Token t;
    t.type = TOK_STRING;
    t.text = arena_strdup(lx->arena, buf, n);
    t.text_len = n;
    t.line = start_line;
    return t;
}

static Token lex_keyword(Lexer *lx) {
    int start_line = lx->line;
    size_t start = lx->pos; /* includes leading ':' */
    advance(lx); /* consume ':' */
    while (!at_end(lx) && (is_symbol_char(peek(lx)) || peek(lx) == '/')) advance(lx);
    size_t len = lx->pos - start;
    Token t;
    t.type = TOK_KEYWORD;
    t.text = arena_strdup(lx->arena, lx->src + start, len);
    t.text_len = len;
    t.line = start_line;
    return t;
}

static int looks_like_number_start(Lexer *lx) {
    char c = peek(lx);
    if (isdigit((unsigned char)c)) return 1;
    if ((c == '-' || c == '+') && isdigit((unsigned char)peek2(lx))) return 1;
    return 0;
}

static Token lex_number(Lexer *lx) {
    int start_line = lx->line;
    size_t start = lx->pos;
    if (peek(lx) == '-' || peek(lx) == '+') advance(lx);
    while (!at_end(lx) && isdigit((unsigned char)peek(lx))) advance(lx);
    if (peek(lx) == '.' && isdigit((unsigned char)peek2(lx))) {
        advance(lx);
        while (!at_end(lx) && isdigit((unsigned char)peek(lx))) advance(lx);
    }
    size_t len = lx->pos - start;
    Token t;
    t.type = TOK_NUMBER;
    t.text = arena_strdup(lx->arena, lx->src + start, len);
    t.text_len = len;
    t.line = start_line;
    return t;
}

static Token lex_symbol(Lexer *lx) {
    int start_line = lx->line;
    size_t start = lx->pos;
    while (!at_end(lx) && is_symbol_char(peek(lx))) advance(lx);
    size_t len = lx->pos - start;
    Token t;
    t.type = TOK_SYMBOL;
    t.text = arena_strdup(lx->arena, lx->src + start, len);
    t.text_len = len;
    t.line = start_line;
    return t;
}

Token lexer_next(Lexer *lx, const char **out_error) {
    skip_whitespace_and_comments(lx);
    if (at_end(lx)) return make_punct(TOK_EOF, lx->line);

    int line = lx->line;
    char c = peek(lx);

    switch (c) {
        case '(': advance(lx); return make_punct(TOK_LPAREN, line);
        case ')': advance(lx); return make_punct(TOK_RPAREN, line);
        case '[': advance(lx); return make_punct(TOK_LBRACKET, line);
        case ']': advance(lx); return make_punct(TOK_RBRACKET, line);
        case '{': advance(lx); return make_punct(TOK_LBRACE, line);
        case '}': advance(lx); return make_punct(TOK_RBRACE, line);
        case '"': return lex_string(lx, out_error);
        case '@': advance(lx); return make_punct(TOK_AT, line);
        case ':': {
            /* A colon immediately followed by an identifier char (no space)
               is a keyword: :region/scratch. A colon on its own (followed
               by whitespace, EOF, or another delimiter) is the standalone
               type-signature separator: (x : Type @ Region). */
            char n = peek2(lx);
            if (is_symbol_char(n)) return lex_keyword(lx);
            advance(lx);
            return make_punct(TOK_COLON, line);
        }
        default:
            if (looks_like_number_start(lx)) return lex_number(lx);
            if (is_symbol_char(c)) return lex_symbol(lx);
            /* Genuinely unrecognized character (e.g. a stray '#' outside a
               #target block, or a non-ASCII byte) -- report and skip it so
               a single bad byte doesn't cascade into a wall of spurious
               errors, matching this session's own "fail loudly, don't
               guess" convention while still letting the caller see every
               real problem in one pass rather than just the first. */
            if (out_error) {
                char msg[96];
                snprintf(msg, sizeof(msg), "unexpected character '%c' at line %d", c, line);
                *out_error = arena_strdup(lx->arena, msg, strlen(msg));
            }
            advance(lx);
            return make_punct(TOK_EOF, line);
    }
}
