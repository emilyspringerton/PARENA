/* tests/integration/driver_target_scalar_return.c -- real end-to-end proof that the self-hosted
 * emitter's own new #target-bodied scalar return-type support (2026-09-08, closing the "no real
 * I32/F64 scalar return-type support" gap named in NORTHSTAR.md's own "Self-hosting" section)
 * produces a real, correctly-TYPED, running function -- a real `int`, not the pre-existing
 * `char *` default that would fail to link/compile against this extern declaration at all.
 */
#include "parena_runtime.h"
#include <assert.h>

extern int magic_number(void);

int main(void) {
    int n = magic_number();
    assert(n == 42);
    return 0;
}
