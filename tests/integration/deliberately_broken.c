/* deliberately_broken.c — a real, hand-written fixture proving domain
 * 4's own verification actually has teeth: if it didn't catch this
 * program's real leak and real use-after-free, it wouldn't be trusted
 * to catch a regression in emit.c's own output either. Not something
 * the compiler emits -- this is a negative-case test of the *checking
 * methodology*, the same "prove the check can fail" discipline
 * region_analyze's own test suite (test_region.c) already applies to
 * itself.
 */
#include "parena_runtime.h"
#include <stdio.h>

int main(void) {
    /* Real use-after-free: free the arena, then keep using memory it
     * owned. */
    Arena a;
    arena_init(&a);
    char *s = arena_strdup(&a, "will be freed out from under this pointer", 42);
    arena_free_all(&a);
    printf("%s\n", s); /* real UAF: s's backing block was just freed */

    /* Real leak: an arena that's never freed at all. */
    Arena leaked;
    arena_init(&leaked);
    arena_strdup(&leaked, "never freed", 11);
    /* deliberately no arena_free_all(&leaked) here */

    return 0;
}
