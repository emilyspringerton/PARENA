/* driver_bool_arg.c -- real behavioral verification for selfhost/
 * emit.prn's new or/and/not-as-a-call-argument support (2026-08-28),
 * closing the gap the previous same-day round's own crash-regression
 * fixture proved was still unsupported, AND proving the real fix for
 * a real, live bug found along the way: `(f (and a b))` used to
 * silently emit `f(and(a, b))`, a call to a NEVER-DEFINED C function
 * named `and`, since plain-call-shaped? never excluded `or`/`and`/
 * `not` from its own name checks. Links against real generated C for:
 *   (defn report [(v : I32)] : I32
 *     (+ v 0))
 *   (defn check-pair [(x : I32) (y : I32)] : I32
 *     (report (and (> x 0) (> y 0))))
 * and proves the real `(and (> x 0) (> y 0))` boolean composition
 * genuinely computes the correct value on every real input.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

extern char *check_pair(int x, int y);

int main(void) {
    int r1 = (int)(intptr_t)check_pair(5, 3);
    int r2 = (int)(intptr_t)check_pair(-5, 3);
    int r3 = (int)(intptr_t)check_pair(5, -3);
    int r4 = (int)(intptr_t)check_pair(0, 0);

    printf("check_pair(5,3)=%d (expected 1), check_pair(-5,3)=%d (expected 0), "
           "check_pair(5,-3)=%d (expected 0), check_pair(0,0)=%d (expected 0)\n",
           r1, r2, r3, r4);
    assert(r1 == 1);
    assert(r2 == 0);
    assert(r3 == 0);
    assert(r4 == 0);

    printf("driver_bool_arg: OK -- the real generated (and (> x 0) (> y 0)) call argument "
           "genuinely computed the correct value on every real input, not just compiled "
           "clean.\n");
    return 0;
}
