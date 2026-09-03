/* tests/test_v16_lexer.c -- real end-to-end verification of stdlib/v16/lexer.prn (kanban
 * priority-queue card 34134124, "parena v16 iteratejs engine"). Confirms real number/string/
 * ident/punct tokenization, whitespace skipping, an unterminated string's own honest degenerate
 * handling, and the real TokEof sentinel always being the last token -- not just "did it
 * compile".
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "test_v16_lexer_gen.c"

static JsToken tok_at(Vec *v, int i) {
    return *(JsToken *)vec_get(v, i);
}

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* Real mix of every real v0 token kind, plus whitespace between them (all skipped) and a
       source that does NOT end in whitespace, so TokEof's own real index math is exercised
       right at the end of the buffer too. */
    Vec toks = lex("let x = 42 + foo; \"hi\"", &arena);

    int n = vec_len(&toks);
    assert(n == 9); /* let, x, =, 42, +, foo, ;, "hi", TokEof */

    int i = 0;
    JsToken t;

    t = tok_at(&toks, i++); assert(t.kind == 2 && strcmp(t.text, "let") == 0); /* TokIdent */
    t = tok_at(&toks, i++); assert(t.kind == 2 && strcmp(t.text, "x") == 0);
    t = tok_at(&toks, i++); assert(t.kind == 3 && strcmp(t.text, "=") == 0);   /* TokPunct */
    t = tok_at(&toks, i++); assert(t.kind == 0 && strcmp(t.text, "42") == 0); /* TokNumber */
    t = tok_at(&toks, i++); assert(t.kind == 3 && strcmp(t.text, "+") == 0);
    t = tok_at(&toks, i++); assert(t.kind == 2 && strcmp(t.text, "foo") == 0);
    t = tok_at(&toks, i++); assert(t.kind == 3 && strcmp(t.text, ";") == 0);
    t = tok_at(&toks, i++); assert(t.kind == 1 && strcmp(t.text, "hi") == 0); /* TokString, unquoted text */
    t = tok_at(&toks, i++); assert(t.kind == 4); /* TokEof */

    assert(i == n);
    printf("PASS: real mixed-token source lexes correctly (%d tokens incl. TokEof)\n", n);

    /* Real empty source: exactly one token, TokEof. */
    Vec empty_toks = lex("", &arena);
    assert(vec_len(&empty_toks) == 1);
    assert(tok_at(&empty_toks, 0).kind == 4);
    printf("PASS: an empty source lexes to exactly one TokEof, not zero tokens or a crash\n");

    /* Real, honest degenerate case: an unterminated string doesn't crash or hang -- it takes the
       rest of the source as the string's own text, and TokEof still follows. */
    Vec unterminated = lex("\"never closes", &arena);
    assert(vec_len(&unterminated) == 2);
    t = tok_at(&unterminated, 0);
    assert(t.kind == 1 && strcmp(t.text, "never closes") == 0);
    assert(tok_at(&unterminated, 1).kind == 4);
    printf("PASS: an unterminated string is a real, honest degenerate case, not a crash or hang\n");

    /* Real, deliberate v0 boundary: an unrecognized byte is silently skipped, not a crash --
       '#' isn't in the real v0 punct/ident/digit/string/space set. */
    Vec skips_unknown = lex("a # b", &arena);
    assert(vec_len(&skips_unknown) == 3); /* a, b, TokEof -- '#' silently skipped */
    assert(strcmp(tok_at(&skips_unknown, 0).text, "a") == 0);
    assert(strcmp(tok_at(&skips_unknown, 1).text, "b") == 0);
    printf("PASS: an unrecognized byte is silently skipped (real, honest v0 boundary), not a crash\n");

    printf("\nALL PASS\n");
    return 0;
}
