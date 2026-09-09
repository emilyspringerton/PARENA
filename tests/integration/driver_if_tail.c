/* tests/integration/driver_if_tail.c -- real end-to-end proof that the
 * self-hosted emitter's own new tail-position `if` support (2026-09-09,
 * closing the real, named NORTHSTAR.md Self-hosting gap: "`if` used
 * directly as a defn's own whole body doesn't emit") produces real,
 * correct, running C -- not just gcc-clean text.
 *
 * `sign` is real, ordinary PARENA (see the test harness's own embedded
 * snippet) -- a nested `if` as a defn's ENTIRE body, each branch a bare
 * number literal, the exact real shape this feature exists to support.
 */
#include "parena_runtime.h"
#include <assert.h>

extern int sign(int n);

int main(void) {
    assert(sign(5) == 1);
    assert(sign(-5) == -1);
    assert(sign(0) == 0);
    return 0;
}
