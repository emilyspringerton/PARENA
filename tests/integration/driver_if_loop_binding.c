/* tests/integration/driver_if_loop_binding.c -- real end-to-end proof that the self-hosted
 * emitter's own new if-as-loop-binding-value support (2026-09-10, closing the real,
 * previously-named gap: `is-valid-i32-text?`'s own real `i (if (starts-with-sign? s) 1 0)` loop
 * init) produces real, correct, running C, not just gcc-clean text.
 *
 * `count-from`'s own `i` loop binding is `(if (> start-flag 0) 1 0)` -- a real ternary the
 * generated code must actually EVALUATE at runtime, not just compile: starting `i` at 1 vs. 0
 * changes how many times the loop body runs before `i > n`, so the two real call sites below
 * genuinely exercise both branches and prove the chosen start value took effect (not just that
 * the code happens to compile with an unused ternary sitting in it).
 */
#include "parena_runtime.h"
#include <assert.h>

extern int count_from(int start_flag, int n);

int main(void) {
    /* start-flag > 0: i starts at 1 -> i=1,2,3 before i(4) > n(3) -- 3 iterations. */
    assert(count_from(1, 3) == 3);
    /* start-flag == 0: i starts at 0 -> i=0,1,2,3 before i(4) > n(3) -- 4 iterations. */
    assert(count_from(0, 3) == 4);
    return 0;
}
