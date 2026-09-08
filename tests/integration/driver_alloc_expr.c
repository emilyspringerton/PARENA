/* tests/integration/driver_alloc_expr.c -- real end-to-end proof that the self-hosted emitter's
 * own new `alloc` NON-LITERAL value-argument support (2026-09-08, closing the "necessary
 * companion fix" NORTHSTAR.md's own Self-hosting section named right after mid-body #target
 * shipped) produces a real, correctly-SIZED allocation -- not the old silent
 * `arena_strdup(dest, "", 0)` zero-byte allocation that the following strcpy/strcat would
 * overflow.
 *
 * Uses stdlib/string.prn's own REAL `length`/`concat` source VERBATIM (see the test harness's
 * own embedded snippet) -- not a synthetic stand-in -- to prove the exact real, motivating case
 * (`(alloc dest String (+ (length a) (length b)))`) genuinely works end to end.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>

extern int length(char *s);
extern char *concat(char *a, char *b, Arena *dest);

int main(void) {
    Arena arena;
    arena_init(&arena);
    assert(length("hello") == 5);
    char *result = concat("hello, ", "world!", &arena);
    assert(result != NULL);
    assert(strcmp(result, "hello, world!") == 0);
    arena_free_all(&arena);
    return 0;
}
