/* driver_cond.c -- real behavioral verification for selfhost/emit.prn's
 * new tail-position `cond` support (2026-08-28): compiling-clean alone
 * doesn't prove the real if/else-if/else chain actually branches
 * correctly, only that gcc accepts the types -- this links against the
 * real generated C for
 *   (defn classify [(n : I32)] : I32
 *     (cond ((= n 1) 10) ((>= n 2) 20) (true 0)))
 * and actually calls it with all three real classes of input,
 * unboxing each result the same way this feature's own header comment
 * documents any real caller must (`(int)(intptr_t)result`).
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

extern char *classify(int n);

int main(void) {
    int r1 = (int)(intptr_t)classify(1);
    int r2 = (int)(intptr_t)classify(5);
    int r3 = (int)(intptr_t)classify(0);
    printf("classify(1)=%d classify(5)=%d classify(0)=%d (expected 10, 20, 0)\n", r1, r2, r3);
    assert(r1 == 10);
    assert(r2 == 20);
    assert(r3 == 0);
    printf("driver_cond: OK -- all three real cond branches produced the correct value, not "
           "just compiled clean.\n");
    return 0;
}
