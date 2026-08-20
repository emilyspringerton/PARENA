#include "parser.h"
#include "lexer.h"
#include <stdio.h>
#include <string.h>
#include <setjmp.h>

typedef struct {
    Lexer lx;
    Arena *arena;
    Token cur;
    jmp_buf error_jmp;
    const char *error_msg; /* set just before longjmp */
} Parser;

/* fail: records an arena-owned error message and unwinds the whole parse
 * via longjmp -- every call site below that can fail participates without
 * each one needing to thread an error return through every recursive
 * call, which would otherwise make parse_form/parse_compound's mutual
 * recursion considerably messier for no real benefit (a parse error means
 * "stop, the whole file is rejected," never "skip this one form and keep
 * going" for VS0). */
static void fail(Parser *p, const char *fmt_prefix, const char *detail, int line) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s%s at line %d", fmt_prefix, detail, line);
    p->error_msg = arena_strdup(p->arena, buf, strlen(buf));
    longjmp(p->error_jmp, 1);
}

static void advance(Parser *p) {
    const char *lex_err = NULL;
    p->cur = lexer_next(&p->lx, &lex_err);
    if (lex_err) fail(p, "", lex_err, p->cur.line);
}

static const char *close_name(TokenType t) {
    switch (t) {
        case TOK_RPAREN: return ")";
        case TOK_RBRACKET: return "]";
        case TOK_RBRACE: return "}";
        default: return "?";
    }
}

static Node *parse_form(Parser *p);

static Node *parse_compound(Parser *p, NodeType node_type, TokenType open_tok, TokenType close_tok) {
    (void)open_tok;
    int open_line = p->cur.line;
    advance(p); /* consume the opening bracket */
    Node *node = node_new_compound(p->arena, node_type, open_line);
    for (;;) {
        if (p->cur.type == close_tok) {
            advance(p);
            return node;
        }
        if (p->cur.type == TOK_EOF) {
            char detail[96];
            snprintf(detail, sizeof(detail), "unterminated form: expected '%s' to close the form opened",
                     close_name(close_tok));
            fail(p, "", detail, open_line);
        }
        if (p->cur.type == TOK_RPAREN || p->cur.type == TOK_RBRACKET || p->cur.type == TOK_RBRACE) {
            /* A close token, but the WRONG one -- mismatched bracket kind,
               e.g. "(foo]" or "[bar)". */
            char detail[128];
            snprintf(detail, sizeof(detail), "mismatched bracket: expected '%s' but found '%s'",
                     close_name(close_tok), close_name(p->cur.type));
            fail(p, "", detail, p->cur.line);
        }
        node_push_child(p->arena, node, parse_form(p));
    }
}

static Node *parse_form(Parser *p) {
    switch (p->cur.type) {
        case TOK_LPAREN:
            return parse_compound(p, NODE_LIST, TOK_LPAREN, TOK_RPAREN);
        case TOK_LBRACKET:
            return parse_compound(p, NODE_VEC, TOK_LBRACKET, TOK_RBRACKET);
        case TOK_LBRACE:
            return parse_compound(p, NODE_MAP, TOK_LBRACE, TOK_RBRACE);
        case TOK_SYMBOL: {
            Node *n = node_new_atom(p->arena, NODE_SYMBOL, p->cur.text, p->cur.text_len, p->cur.line);
            advance(p);
            return n;
        }
        case TOK_KEYWORD: {
            Node *n = node_new_atom(p->arena, NODE_KEYWORD, p->cur.text, p->cur.text_len, p->cur.line);
            advance(p);
            return n;
        }
        case TOK_STRING: {
            Node *n = node_new_atom(p->arena, NODE_STRING, p->cur.text, p->cur.text_len, p->cur.line);
            advance(p);
            return n;
        }
        case TOK_NUMBER: {
            Node *n = node_new_atom(p->arena, NODE_NUMBER, p->cur.text, p->cur.text_len, p->cur.line);
            advance(p);
            return n;
        }
        case TOK_COLON: {
            Node *n = node_new_atom(p->arena, NODE_COLON, NULL, 0, p->cur.line);
            advance(p);
            return n;
        }
        case TOK_AT: {
            Node *n = node_new_atom(p->arena, NODE_AT, NULL, 0, p->cur.line);
            advance(p);
            return n;
        }
        case TOK_RPAREN:
        case TOK_RBRACKET:
        case TOK_RBRACE: {
            char detail[64];
            snprintf(detail, sizeof(detail), "unexpected '%s' with no matching open bracket",
                     close_name(p->cur.type));
            fail(p, "", detail, p->cur.line);
            return NULL; /* unreachable -- fail() never returns */
        }
        case TOK_EOF:
        default:
            fail(p, "", "unexpected end of file, expected a form", p->cur.line);
            return NULL; /* unreachable */
    }
}

Node *parse_program(Arena *arena, const char *src, size_t len, const char **out_error) {
    Parser p;
    p.arena = arena;
    p.error_msg = NULL;
    lexer_init(&p.lx, src, len, arena);

    if (setjmp(p.error_jmp)) {
        *out_error = p.error_msg;
        return NULL;
    }

    advance(&p); /* prime the first token */

    Node *program = node_new_compound(arena, NODE_LIST, 1);
    while (p.cur.type != TOK_EOF) {
        if (p.cur.type == TOK_RPAREN || p.cur.type == TOK_RBRACKET || p.cur.type == TOK_RBRACE) {
            char detail[64];
            snprintf(detail, sizeof(detail), "unexpected '%s' with no matching open bracket",
                     close_name(p.cur.type));
            fail(&p, "", detail, p.cur.line);
        }
        node_push_child(arena, program, parse_form(&p));
    }

    *out_error = NULL;
    return program;
}
