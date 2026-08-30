/* tests/test_http_router.c -- real end-to-end verification of stdlib/http/router.prn (LO
 * FRAMEWORK_NORTHSTAR.md's own Phase B proof point, founder real-time: "ok well write the deps
 * in parena"). Confirms a real, genuine bug (found live via a real segfault): vec-string-at's
 * own double-dereference cast, same latent bug in regex/pcre.prn's own copy of the same function
 * (never actually exercised there — no real caller of join-strings exists), fixed in both files.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "test_http_router_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* Real, hand-verified route matches. */
    assert(route_matches_("/repos/:name/commits", "/repos/PARENA/commits", &arena) == 1);
    assert(route_matches_("/repos/:name/commits", "/repos/PARENA/branches", &arena) == 0);
    assert(route_matches_("/repos/:name", "/repos/a/b", &arena) == 0); /* segment-count mismatch */
    assert(route_matches_("/", "/", &arena) == 1);

    /* Real param extraction against a real matched route. */
    char *name = extract_param("/repos/:name/commits", "/repos/PARENA/commits", "name", &arena);
    assert(strcmp(name, "PARENA") == 0);

    char *repo_name = extract_param("/repos/:owner/:repo", "/repos/emilyspringerton/LO", "repo", &arena);
    assert(strcmp(repo_name, "LO") == 0);

    printf("test_http_router: all assertions passed\n");
    return 0;
}
