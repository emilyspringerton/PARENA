/* tests/integration/driver_loop_recur.c -- real end-to-end proof that
 * the self-hosted emitter's own new `loop`/`recur` support (2026-09-09,
 * closing the "no loop/recur support at all" gap found while verifying
 * tail-position `if` support) produces real, correct, running C -- not
 * just gcc-clean text.
 *
 * `sum-to-n` is real, ordinary PARENA (see the test harness's own
 * embedded snippet) -- a `loop` with two I32 bindings, an `if`-tail
 * choosing between `recur` (the accumulating case) and a real terminal
 * value (the base case), the exact real shape this feature exists to
 * support. The base case is written `(+ acc 0)`, not a bare `acc`
 * symbol -- a deliberate, documented workaround for a real, SEPARATE,
 * already-known gap (a bare I32 param/loop-var symbol in tail position
 * isn't boxed correctly yet), not something this feature itself is
 * responsible for closing.
 */
#include "parena_runtime.h"
#include <assert.h>

extern int sum_to_n(int n);

int main(void) {
    assert(sum_to_n(0) == 0);
    assert(sum_to_n(3) == 6);
    assert(sum_to_n(5) == 15);
    return 0;
}
