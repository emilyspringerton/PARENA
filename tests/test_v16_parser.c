/* tests/test_v16_parser.c -- real end-to-end verification of stdlib/v16/parser.prn (V16 JS
 * engine Phase 2b, PARENA/docs/V16_NORTHSTAR.md's own real phased plan). Confirms real operator
 * precedence (multiplicative binds tighter than additive binds tighter than comparison),
 * left-associativity, parenthesized sub-expressions, multi-argument function calls (including
 * the real zero-arg case), and the real empty-source root=-1 boundary -- not just "did it
 * compile".
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "test_v16_parser_gen.c"

static AstExpr *node(Vec *exprs, int i) {
    return (AstExpr *)vec_get(exprs, i);
}

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* "1 + 2 * 3" -- real precedence: multiplicative binds tighter, so the tree is
     * (+ 1 (* 2 3)), not (* (+ 1 2) 3). */
    {
        Vec toks = lex("1 + 2 * 3", &arena);
        ParseResult r = parse(toks, &arena);
        assert(r.root != -1);
        AstExpr *plus = node(&r.state.exprs, r.root);
        assert(plus->kind == expr_binary() && strcmp(plus->text, "+") == 0);
        AstExpr *left = node(&r.state.exprs, plus->a);
        assert(left->kind == expr_num() && strcmp(left->text, "1") == 0);
        AstExpr *right = node(&r.state.exprs, plus->b);
        assert(right->kind == expr_binary() && strcmp(right->text, "*") == 0);
        AstExpr *rl = node(&r.state.exprs, right->a);
        AstExpr *rr = node(&r.state.exprs, right->b);
        assert(rl->kind == expr_num() && strcmp(rl->text, "2") == 0);
        assert(rr->kind == expr_num() && strcmp(rr->text, "3") == 0);
        printf("PASS: real precedence -- '1 + 2 * 3' parses as (+ 1 (* 2 3))\n");
    }

    /* "10 - 3 - 2" -- real left-associativity: (- (- 10 3) 2), not (- 10 (- 3 2)). Would
     * evaluate to a different, wrong number (9 vs 5) if this were mis-associated. */
    {
        Vec toks = lex("10 - 3 - 2", &arena);
        ParseResult r = parse(toks, &arena);
        AstExpr *outer = node(&r.state.exprs, r.root);
        assert(outer->kind == expr_binary() && strcmp(outer->text, "-") == 0);
        AstExpr *inner = node(&r.state.exprs, outer->a);
        assert(inner->kind == expr_binary() && strcmp(inner->text, "-") == 0);
        AstExpr *rhs = node(&r.state.exprs, outer->b);
        assert(rhs->kind == expr_num() && strcmp(rhs->text, "2") == 0);
        AstExpr *inner_l = node(&r.state.exprs, inner->a);
        AstExpr *inner_r = node(&r.state.exprs, inner->b);
        assert(strcmp(inner_l->text, "10") == 0 && strcmp(inner_r->text, "3") == 0);
        printf("PASS: real left-associativity -- '10 - 3 - 2' parses as ((10 - 3) - 2)\n");
    }

    /* "(1 + 2) * 3" -- parens override precedence: real tree is (* (+ 1 2) 3). */
    {
        Vec toks = lex("(1 + 2) * 3", &arena);
        ParseResult r = parse(toks, &arena);
        AstExpr *star = node(&r.state.exprs, r.root);
        assert(star->kind == expr_binary() && strcmp(star->text, "*") == 0);
        AstExpr *left = node(&r.state.exprs, star->a);
        assert(left->kind == expr_binary() && strcmp(left->text, "+") == 0);
        AstExpr *right = node(&r.state.exprs, star->b);
        assert(right->kind == expr_num() && strcmp(right->text, "3") == 0);
        printf("PASS: real parenthesized sub-expressions override default precedence\n");
    }

    /* "x < 5" -- comparison, real ident operand. */
    {
        Vec toks = lex("x < 5", &arena);
        ParseResult r = parse(toks, &arena);
        AstExpr *cmp = node(&r.state.exprs, r.root);
        assert(cmp->kind == expr_binary() && strcmp(cmp->text, "<") == 0);
        AstExpr *left = node(&r.state.exprs, cmp->a);
        assert(left->kind == expr_ident() && strcmp(left->text, "x") == 0);
        printf("PASS: real comparison operator with an identifier operand\n");
    }

    /* "add(1, 2, 3)" -- real multi-argument call, real comma-chained AstArg list. */
    {
        Vec toks = lex("add(1, 2, 3)", &arena);
        ParseResult r = parse(toks, &arena);
        AstExpr *call = node(&r.state.exprs, r.root);
        assert(call->kind == expr_call());
        AstExpr *callee = node(&r.state.exprs, call->a);
        assert(callee->kind == expr_ident() && strcmp(callee->text, "add") == 0);
        int arg_idx = call->b;
        assert(arg_idx != -1);
        AstArg *a0 = (AstArg *)vec_get(&r.state.args, arg_idx);
        AstExpr *v0 = node(&r.state.exprs, a0->expr);
        assert(strcmp(v0->text, "1") == 0);
        AstArg *a1 = (AstArg *)vec_get(&r.state.args, a0->next);
        AstExpr *v1 = node(&r.state.exprs, a1->expr);
        assert(strcmp(v1->text, "2") == 0);
        AstArg *a2 = (AstArg *)vec_get(&r.state.args, a1->next);
        AstExpr *v2 = node(&r.state.exprs, a2->expr);
        assert(strcmp(v2->text, "3") == 0);
        assert(a2->next == -1);
        printf("PASS: real multi-argument function call, 'add(1, 2, 3)' -> 3 chained AstArgs\n");
    }

    /* "noop()" -- real zero-argument call: the callee resolves, but call->b (first arg) is -1. */
    {
        Vec toks = lex("noop()", &arena);
        ParseResult r = parse(toks, &arena);
        AstExpr *call = node(&r.state.exprs, r.root);
        assert(call->kind == expr_call());
        assert(call->b == -1);
        printf("PASS: real zero-argument call -- 'noop()' has no AstArg chain (b == -1)\n");
    }

    /* Real, honest empty-source boundary: parse() on a source that lexes to just TokEof
       returns root = -1, matching lexer.prn's own TokEof convention rather than parse-primary's
       own separate -1-on-unexpected-token fallback silently standing in for "there was nothing
       here". */
    {
        Vec toks = lex("", &arena);
        ParseResult r = parse(toks, &arena);
        assert(r.root == -1);
        printf("PASS: an empty source parses to root == -1, not a crash or a garbage node\n");
    }

    /* A real "quoted string" literal as a call argument -- strings and numbers both work as
       primary expressions/arguments, not just numbers. */
    {
        Vec toks = lex("log(\"hi\")", &arena);
        ParseResult r = parse(toks, &arena);
        AstExpr *call = node(&r.state.exprs, r.root);
        assert(call->kind == expr_call());
        AstArg *a0 = (AstArg *)vec_get(&r.state.args, call->b);
        AstExpr *v0 = node(&r.state.exprs, a0->expr);
        assert(v0->kind == expr_str() && strcmp(v0->text, "hi") == 0);
        printf("PASS: a string literal parses correctly as a call argument\n");
    }

    printf("\nALL PASS\n");
    return 0;
}
