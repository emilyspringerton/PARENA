/* tests/test_editor_spotlight.c -- real end-to-end verification of
 * stdlib/editor/spotlight.prn, the PARENA editor's own Ctrl+T/Cmd+T
 * command palette (2026-08-27, founder real-time: "start building out
 * the textmate features? like quick open via ctrl+t windows and linux
 * or cmd+t for mac" -> "thats going to be a magic spotlight feature"
 * -> "so make sure the plugin api has very much access to that as a
 * feature" -> "for example adding a calculator to the ctrl t should
 * be just a plugin" -> "the backends for searching should be hot
 * swappable").
 *
 * Real, honest scope: pure logic only, no SDL2/Xvfb needed here --
 * file-search-provider/calculator-provider/run-providers are all real
 * (query, dest) -> Vec functions with no rendering dependency. The
 * overlay UI itself (examples/editor_main.c's own Ctrl+T/Cmd+T
 * handling, text-input routing, rendering) is exercised separately by
 * examples/editor_main.c's own manual/screenshot verification, the
 * same split test_editor_widget.c's own header comment already
 * documents between pure-function and real-SDL2-integration checks.
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "test_editor_spotlight_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static int any_label_contains(Vec *results, const char *needle) {
    for (int i = 0; i < vec_len(results); i++) {
        SpotlightResult *r = (SpotlightResult *)vec_get(results, i);
        if (strstr(r->label, needle)) return 1;
    }
    return 0;
}

int main(void) {
    Arena a;
    arena_init(&a);

    /* --- real fixture directory tree, built fresh under /tmp --- */
    char root[256];
    snprintf(root, sizeof root, "/tmp/parena_spotlight_test_%d", (int)getpid());
    char sub[300];
    snprintf(sub, sizeof sub, "%s/subdir", root);
    char hidden[300];
    snprintf(hidden, sizeof hidden, "%s/.hidden", root);
    mkdir(root, 0755);
    mkdir(sub, 0755);
    mkdir(hidden, 0755);

    char f1[350], f2[350], f3[350], f4[350];
    snprintf(f1, sizeof f1, "%s/lexer.prn", root);
    snprintf(f2, sizeof f2, "%s/parser.prn", root);
    snprintf(f3, sizeof f3, "%s/emit.prn", sub);
    snprintf(f4, sizeof f4, "%s/secret.txt", hidden);
    FILE *f;
    f = fopen(f1, "w"); fputs("lexer", f); fclose(f);
    f = fopen(f2, "w"); fputs("parser", f); fclose(f);
    f = fopen(f3, "w"); fputs("emit", f); fclose(f);
    f = fopen(f4, "w"); fputs("secret", f); fclose(f);

    /* --- real file-search-provider coverage --- */
    Vec r1 = file_search_provider((char *)"lex", root, &a);
    CHECK(any_label_contains(&r1, "lexer.prn"), "file-search-provider finds a real file by a real case-matching substring");

    Vec r2 = file_search_provider((char *)"LEX", root, &a);
    CHECK(any_label_contains(&r2, "lexer.prn"), "file-search-provider matches case-insensitively");

    Vec r3 = file_search_provider((char *)"emit", root, &a);
    CHECK(any_label_contains(&r3, "emit.prn"), "file-search-provider finds a real file in a real nested subdirectory");

    Vec r4 = file_search_provider((char *)"secret", root, &a);
    CHECK(!any_label_contains(&r4, "secret.txt"), "file-search-provider skips a real hidden (dot-prefixed) directory");

    Vec r5 = file_search_provider((char *)"", root, &a);
    CHECK(vec_len(&r5) == 0, "file-search-provider returns no results for a real empty query");

    Vec r6 = file_search_provider((char *)"nonexistent-xyz", root, &a);
    CHECK(vec_len(&r6) == 0, "file-search-provider returns no results for a real query matching nothing");

    /* --- real calculator-provider coverage (the founder's own explicit
     * "just a plugin" proof case) --- */
    Vec c1 = calculator_provider((char *)"2 + 2", &a);
    CHECK(vec_len(&c1) == 1, "calculator-provider produces exactly one real result for a real arithmetic query");
    CHECK(any_label_contains(&c1, "4"), "calculator-provider's own real result shows the real computed value (2 + 2 = 4)");

    Vec c2 = calculator_provider((char *)"10 * 5", &a);
    CHECK(any_label_contains(&c2, "50"), "calculator-provider correctly evaluates a real multiplication");

    Vec c3 = calculator_provider((char *)"lexer.prn", &a);
    CHECK(vec_len(&c3) == 0, "calculator-provider does not misfire on a real ordinary file-search query");

    Vec c4 = calculator_provider((char *)"", &a);
    CHECK(vec_len(&c4) == 0, "calculator-provider produces no result for a real empty query");

    /* --- real run-providers coverage: both providers fire together,
     * matching the real overlay's own combined result list. --- */
    Vec run1 = run_providers((char *)"3 + 4", root, &a);
    CHECK(any_label_contains(&run1, "7"), "run-providers includes the real calculator result for an arithmetic query");

    Vec run2 = run_providers((char *)"parser", root, &a);
    CHECK(any_label_contains(&run2, "parser.prn"), "run-providers includes the real file-search result for a filename query");

    arena_free_all(&a);
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
