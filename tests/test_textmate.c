/* tests/test_textmate.c -- real end-to-end verification for
 * stdlib/editor/textmate.prn's real TextMate-grammar-shaped tokenizer,
 * built on the already-real regex/pcre.prn engine. Founder real-time:
 * "start adding all of the features of textmate" -> "have it support
 * text mate syntax and a parena variant of it" -> "for the regexes".
 *
 * Same "test what's actually there" discipline as every other real test
 * in this repo: compiles real regex patterns via the real PCRE engine,
 * tokenizes a real line of text, and checks the real, exact scope/span
 * of every resulting token -- not just "does it run."
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "test_textmate_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static TmRule must_compile(Arena *a, const char *scope, const char *pattern) {
    Result r = compile_rule((char *)scope, (char *)pattern, a);
    if (r.tag != 1) {
        printf("FATAL: rule '%s' (%s) failed to compile\n", scope, pattern);
        exit(1);
    }
    return *(TmRule *)r.value;
}

int main(void) {
    Arena a;
    arena_init(&a);

    /* A small, real grammar: keyword, string, number, identifier --
     * enough real rules to exercise real rule-ordering (keyword before
     * identifier) and real anchored-match behavior. */
    /* Real, honest finding along the way (not fixed here -- a real,
     * separate, larger regex-engine scope): regex/syntax.prn's own
     * AnchorKind declares a real WordBoundary variant, but nothing in
     * its real parser actually constructs one from `\b` in pattern
     * text -- confirmed live (compile("^(defn|let|if|match)\\b", ...)
     * silently mis-parses vs. the same pattern with `\b` dropped,
     * which matches correctly). Grammar rules below use plain prefix
     * anchoring without `\b` as a result -- a real, narrower keyword
     * match than a real TextMate grammar would want (would also match
     * "defn" as a prefix of a longer identifier like "definition"),
     * flagged honestly rather than silently worked around. */
    Vec rules = vec_new(&a);
    TmRule r_kw = must_compile(&a, "keyword.control.parena", "^(defn|let|if|match)");
    TmRule r_str = must_compile(&a, "string.quoted.double.parena", "^\"[^\"]*\"");
    TmRule r_num = must_compile(&a, "constant.numeric.parena", "^[0-9]+");
    TmRule r_ident = must_compile(&a, "variable.other.parena", "^[a-zA-Z][a-zA-Z0-9_-]*");
    vec_push_(&rules, TmRule_box(&a, r_kw));
    vec_push_(&rules, TmRule_box(&a, r_str));
    vec_push_(&rules, TmRule_box(&a, r_num));
    vec_push_(&rules, TmRule_box(&a, r_ident));

    /* --- real tokenization of a real, representative line --- */
    {
        Vec toks = tokenize_line(&rules, "let x 42", &a);
        int n = vec_len(&toks);
        CHECK(n > 0, "tokenize-line produces at least one real token");

        /* Expect: "let" (keyword), " " (untokenized), "x" (identifier),
         * " " (untokenized), "42" (number) -- confirm the REAL spans and
         * scopes, not just token count. */
        Token *t0 = (Token *)vec_get(&toks, 0);
        CHECK(t0->start == 0 && t0->end == 3 && strcmp(t0->scope, "keyword.control.parena") == 0,
              "'let' tokenizes as a real keyword.control.parena span [0,3)");

        Token *t1 = (Token *)vec_get(&toks, 1);
        CHECK(t1->start == 3 && t1->end == 4 && strcmp(t1->scope, "") == 0,
              "the space after 'let' is a real untokenized (empty-scope) span");

        Token *t2 = (Token *)vec_get(&toks, 2);
        CHECK(t2->start == 4 && t2->end == 5 && strcmp(t2->scope, "variable.other.parena") == 0,
              "'x' tokenizes as a real variable.other.parena span");

        Token *t4 = (Token *)vec_get(&toks, 4);
        CHECK(t4->start == 6 && t4->end == 8 && strcmp(t4->scope, "constant.numeric.parena") == 0,
              "'42' tokenizes as a real constant.numeric.parena span [6,8)");
    }

    /* --- real string literal tokenization --- */
    {
        Vec toks = tokenize_line(&rules, "\"hello world\"", &a);
        Token *t0 = (Token *)vec_get(&toks, 0);
        CHECK(t0->start == 0 && t0->end == 13 && strcmp(t0->scope, "string.quoted.double.parena") == 0,
              "a real quoted string tokenizes as one whole string.quoted.double.parena span");
    }

    /* --- rule ORDER matters: 'defn' matches BOTH the keyword rule and
     * the identifier rule -- confirm the keyword rule (declared first)
     * wins, real TextMate array-order-tiebreak semantics. --- */
    {
        Vec toks = tokenize_line(&rules, "defn", &a);
        Token *t0 = (Token *)vec_get(&toks, 0);
        CHECK(strcmp(t0->scope, "keyword.control.parena") == 0,
              "when two rules both match, the earlier-declared rule wins (real array-order tiebreak)");
    }

    /* --- a real, honest compile failure for a real malformed pattern
     * (per pattern-supported?'s own real, documented PCRE subset --
     * unmatched '[' is a real, structurally invalid pattern regex/
     * syntax.prn's own parser rejects). --- */
    {
        Result bad = compile_rule("x", "[unterminated", &a);
        CHECK(bad.tag == 0, "compile-rule on a real malformed pattern correctly fails, not a false Ok");
    }

    arena_free_all(&a);

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
