/* driver_none_paren_pattern.c -- real behavioral verification for a real
 * segfault found and fixed 2026-09-02, discovered via a genuine
 * self-compilation attempt (`parena-selfhost build selfhost/region.prn`
 * crashed for real; gdb backtrace pointed at match-pattern-payload-name's
 * own `(get-field payload :text)`). match-pattern-has-payload? used to
 * only check a pattern's KIND (call-shaped), not whether it actually has
 * a payload child -- a real, zero-payload variant pattern written
 * parenthesized (`((None) body...)`, real and common throughout
 * selfhost/region.prn's own match clauses) is call-shaped too, with only
 * ONE child, so match-pattern-payload-name's own unconditional
 * children[1] read was a real out-of-bounds Vec access. Links against
 * real generated C for:
 *   (defn unwrap-or-zero [(o : Option)] : I32
 *     (match o
 *       ((Some x) (deref x))
 *       ((None) 0)))
 * -- the exact previously-crashing parenthesized-None shape, this time
 * as the match's SECOND clause (not the first, matching the real,
 * confirmed-live crash shape in selfhost/region.prn) -- and proves it
 * now runs correctly end to end, not just that gcc accepts the text.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

extern char *unwrap_or_zero(Option o);

int main(void) {
    Arena a;
    arena_init(&a);

    int *boxed42 = (int *)arena_alloc(&a, sizeof(int));
    *boxed42 = 42;

    Option some42 = option_some(boxed42);
    Option none = option_none();

    int r1 = (int)(intptr_t)unwrap_or_zero(some42);
    int r2 = (int)(intptr_t)unwrap_or_zero(none);

    printf("unwrap_or_zero(Some(42))=%d (expected 42), unwrap_or_zero(None)=%d (expected 0)\n",
           r1, r2);
    assert(r1 == 42);
    assert(r2 == 0);

    printf("driver_none_paren_pattern: OK -- a real, parenthesized zero-payload `((None) ...)` "
           "match pattern no longer segfaults the emitter and runs correctly end to end.\n");
    return 0;
}
