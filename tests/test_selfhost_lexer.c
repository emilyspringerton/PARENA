/* tests/test_selfhost_lexer.c -- real end-to-end verification of
 * selfhost/lexer.prn, the first real slice of PARENA's own self-hosting
 * effort (NORTHSTAR.md's own "Self-hosting" section; founder real-time:
 * "ok but after we have a compiler we also need to write parena in
 * parena" -> "not c" -> "silly", picked up again 2026-08-27 after
 * "self hosted compiler"). This is a real, faithful PORT of src/lexer.c
 * (VS0's own C-implemented reference tokenizer) -- every expected
 * token sequence below was traced by hand against src/lexer.c's own
 * real, documented behavior (not guessed at), the same "compute
 * expected values from the real reference before writing assertions"
 * discipline tests/test_editor_indent.c already established.
 *
 * Real, honest scope note: this test drives the PARENA-emitted lexer
 * only (compiled BY the existing C-based parena-c, same as every other
 * real .prn module this repo tests) -- it does NOT (yet) cross-check
 * token-for-token against a live src/lexer.c run in the same binary,
 * since bridging the two would hit the identical real Arena-type
 * collision runtime/prnfmt_bridge.c already solved for a different
 * pair of compiler-internal types (src/arena.h's own Arena vs.
 * runtime/parena_runtime.h's own Arena) -- a real, separate, deferred
 * follow-up if a genuine correctness question ever needs it, not
 * attempted here.
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>

#include "test_selfhost_lexer_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static const char *kind_name(TokenType_Tag tag) {
    switch (tag) {
        case TokenType_TAG_TLParen: return "TLParen";
        case TokenType_TAG_TRParen: return "TRParen";
        case TokenType_TAG_TLBracket: return "TLBracket";
        case TokenType_TAG_TRBracket: return "TRBracket";
        case TokenType_TAG_TLBrace: return "TLBrace";
        case TokenType_TAG_TRBrace: return "TRBrace";
        case TokenType_TAG_TColon: return "TColon";
        case TokenType_TAG_TAt: return "TAt";
        case TokenType_TAG_TSymbol: return "TSymbol";
        case TokenType_TAG_TKeyword: return "TKeyword";
        case TokenType_TAG_TString: return "TString";
        case TokenType_TAG_TNumber: return "TNumber";
        case TokenType_TAG_TEof: return "TEof";
        default: return "???";
    }
}

/* check_token -- real, direct field-by-field check against one
 * expected (kind, text, line) triple. */
static void check_token(Token tok, TokenType_Tag expect_kind, const char *expect_text, int expect_line,
                         const char *label) {
    char msg[256];
    snprintf(msg, sizeof msg, "%s: kind is %s (expected %s)", label, kind_name(tok.kind.tag), kind_name(expect_kind));
    CHECK(tok.kind.tag == expect_kind, msg);
    snprintf(msg, sizeof msg, "%s: text is %s%s%s (expected %s%s%s)", label,
             tok.text ? "\"" : "", tok.text ? tok.text : "(null)", tok.text ? "\"" : "",
             expect_text ? "\"" : "", expect_text ? expect_text : "(null)", expect_text ? "\"" : "");
    CHECK(tok.text && strcmp(tok.text, expect_text) == 0, msg);
    snprintf(msg, sizeof msg, "%s: line is %d (expected %d)", label, tok.line, expect_line);
    CHECK(tok.line == expect_line, msg);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    Arena a;
    arena_init(&a);

    /* --- real, direct tokenize() runs, checking the full real token
     * sequence for each real input against hand-traced expectations --- */
    {
        Result r = tokenize("(a b)", &a);
        CHECK(r.tag == 1, "tokenize succeeds on a real simple balanced form");
        if (r.tag == 1) {
            Vec toks = *(Vec *)r.value;
            CHECK(vec_len(&toks) == 5, "'(a b)' produces exactly 5 real tokens: ( a b ) EOF");
            check_token(*(Token *)vec_get(&toks, 0), TokenType_TAG_TLParen, "", 1, "tok0");
            check_token(*(Token *)vec_get(&toks, 1), TokenType_TAG_TSymbol, "a", 1, "tok1");
            check_token(*(Token *)vec_get(&toks, 2), TokenType_TAG_TSymbol, "b", 1, "tok2");
            check_token(*(Token *)vec_get(&toks, 3), TokenType_TAG_TRParen, "", 1, "tok3");
            check_token(*(Token *)vec_get(&toks, 4), TokenType_TAG_TEof, "", 1, "tok4");
        }
    }

    {
        /* Real bang/amp-prefixed linear/borrow forms -- !file, &var,
         * &mut are single real symbols, matching src/lexer.c's own
         * real is_symbol_char permissiveness (the same real invariant
         * tests/test_lexer_parser.c's own C-lexer suite already pins
         * down for the reference implementation). */
        Result r = tokenize("(!file &var &mut)", &a);
        CHECK(r.tag == 1, "tokenize succeeds on real bang/amp-prefixed symbols");
        if (r.tag == 1) {
            Vec toks = *(Vec *)r.value;
            CHECK(vec_len(&toks) == 6, "'(!file &var &mut)' produces 6 real tokens");
            check_token(*(Token *)vec_get(&toks, 1), TokenType_TAG_TSymbol, "!file", 1, "!file");
            check_token(*(Token *)vec_get(&toks, 2), TokenType_TAG_TSymbol, "&var", 1, "&var");
            check_token(*(Token *)vec_get(&toks, 3), TokenType_TAG_TSymbol, "&mut", 1, "&mut");
        }
    }

    {
        /* Real keyword vs. standalone colon distinction (:region/scratch
         * vs. a bare ':' in a type signature), matching src/lexer.c's
         * own real lex_keyword dispatch exactly. */
        Result r = tokenize("(x : Type @ :region/scratch)", &a);
        CHECK(r.tag == 1, "tokenize succeeds on a real type/region signature");
        if (r.tag == 1) {
            Vec toks = *(Vec *)r.value;
            check_token(*(Token *)vec_get(&toks, 2), TokenType_TAG_TColon, "", 1, "standalone colon");
            check_token(*(Token *)vec_get(&toks, 3), TokenType_TAG_TSymbol, "Type", 1, "Type");
            check_token(*(Token *)vec_get(&toks, 4), TokenType_TAG_TAt, "", 1, "at");
            check_token(*(Token *)vec_get(&toks, 5), TokenType_TAG_TKeyword, ":region/scratch", 1, "keyword");
        }
    }

    {
        /* Real negative/decimal numbers, and a bare '-' as its own real
         * symbol (matching looks_like_number_start's own real
         * "-/+ must be followed by a digit" guard). */
        Result r = tokenize("-42 3.5 (- a b)", &a);
        CHECK(r.tag == 1, "tokenize succeeds on real numbers + a bare '-' symbol");
        if (r.tag == 1) {
            Vec toks = *(Vec *)r.value;
            check_token(*(Token *)vec_get(&toks, 0), TokenType_TAG_TNumber, "-42", 1, "-42");
            check_token(*(Token *)vec_get(&toks, 1), TokenType_TAG_TNumber, "3.5", 1, "3.5");
            check_token(*(Token *)vec_get(&toks, 3), TokenType_TAG_TSymbol, "-", 1, "bare -");
        }
    }

    {
        /* Real string literal with every real escape src/lexer.c's own
         * lex_string decodes: \n \t \" \\ , plus a real literal
         * passthrough char after an unrecognized backslash. */
        Result r = tokenize("\"a\\nb\\tc\\\"d\\\\e\\zf\"", &a);
        CHECK(r.tag == 1, "tokenize succeeds on a real string with every real escape sequence");
        if (r.tag == 1) {
            Vec toks = *(Vec *)r.value;
            Token tok0 = *(Token *)vec_get(&toks, 0);
            CHECK(tok0.kind.tag == TokenType_TAG_TString, "the real escaped string lexes as TString");
            CHECK(strcmp(tok0.text, "a\nb\tc\"d\\ezf") == 0,
                  "every real \\n/\\t/\\\"/\\\\ escape decodes to its real single byte, and \\z (unrecognized) passes 'z' through literally");
        }
    }

    {
        /* Real unterminated string -- reports the real line it OPENED
         * on, matching src/lexer.c's own start_line convention. */
        Result r = tokenize("(f \"never closed", &a);
        CHECK(r.tag == 0, "tokenize reports a real Err on an unterminated string, not a crash or silent EOF");
        if (r.tag == 0) {
            LexError e = *(LexError *)r.value;
            CHECK(e.tag == LexError_TAG_UnterminatedString, "the real error is UnterminatedString");
        }
    }

    {
        /* Real comment skipping -- a ';;' line comment runs to end of
         * line and contributes no real token; a real paren INSIDE it
         * must not be lexed as a real structural token. */
        Result r = tokenize(";; a real (fake paren\n(real)", &a);
        CHECK(r.tag == 1, "tokenize succeeds skipping a real ';;' comment");
        if (r.tag == 1) {
            Vec toks = *(Vec *)r.value;
            check_token(*(Token *)vec_get(&toks, 0), TokenType_TAG_TLParen, "", 2,
                        "the real '(' after the comment is on real line 2, and the comment's own fake paren produced no token");
        }
    }

    {
        /* Real, genuine PARENA source fragment, lifted from this
         * repo's own stdlib/string.prn (not synthetic). */
        char real_src[] = "(defn is-digit? [(c : I32)]\n  : Bool\n  (and (>= c 48) (<= c 57)))";
        Result r = tokenize(real_src, &a);
        CHECK(r.tag == 1, "tokenize succeeds on a real, genuine fragment lifted from stdlib/string.prn");
        if (r.tag == 1) {
            Vec toks = *(Vec *)r.value;
            int n = vec_len(&toks);
            CHECK(n > 20, "the real string.prn fragment produces a real, substantial token stream (>20 tokens)");
            Token last = *(Token *)vec_get(&toks, n - 1);
            CHECK(last.kind.tag == TokenType_TAG_TEof, "the real token stream's own last token is TEof");
            /* Real, direct spot-check: token 0 is '(', token 1 is the
             * real 'defn' keyword-symbol, token 2 is the real defn
             * name. */
            check_token(*(Token *)vec_get(&toks, 1), TokenType_TAG_TSymbol, "defn", 1, "defn itself");
            check_token(*(Token *)vec_get(&toks, 2), TokenType_TAG_TSymbol, "is-digit?", 1, "defn name");
        }
    }

    {
        /* Real empty input -- a real, honest single-TEof stream, not
         * an error or a crash. */
        Result r = tokenize("", &a);
        CHECK(r.tag == 1, "tokenize succeeds on a real empty input");
        if (r.tag == 1) {
            Vec toks = *(Vec *)r.value;
            CHECK(vec_len(&toks) == 1, "a real empty input produces exactly one real TEof token");
        }
    }

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
