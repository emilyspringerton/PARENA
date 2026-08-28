/* driver_predicate_cond.c -- real behavioral verification for selfhost/
 * emit.prn's new plain-call-shaped predicate cond test support
 * (2026-08-28): compiling-clean alone doesn't prove `classify`'s own
 * real dispatch to `is_zero(n)` actually branches correctly, only that
 * gcc accepts the types -- this links against the real generated C for
 *   (defn is-zero [(n : I32)] : Bool
 *     (= n 0))
 *   (defn classify [(n : I32)] : I32
 *     (cond
 *       ((is-zero n) 1)
 *       (true 0)))
 * and actually calls classify with both a zero and a non-zero input,
 * proving the callee's own real, boxed Bool return (0/1 reinterpreted
 * as NULL/non-NULL via emit-i32-boxed on the callee side) is correctly
 * truthy-tested at the call site with zero extra unboxing needed.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

extern char *classify(int n);

int main(void) {
    int r0 = (int)(intptr_t)classify(0);
    int r5 = (int)(intptr_t)classify(5);
    printf("classify(0) = %d (expected 1), classify(5) = %d (expected 0)\n", r0, r5);
    assert(r0 == 1);
    assert(r5 == 0);
    printf("driver_predicate_cond: OK -- classify's own real dispatch to a plain-call-shaped "
           "predicate (is_zero) branched correctly on both real inputs, not just compiled clean.\n");
    return 0;
}
