/* driver_boxed_binop.c -- real behavioral verification for selfhost/emit.prn's new boxed-I32
 * binding-kind unboxing (S501, 2026-09-18): compiling clean alone doesn't prove the unboxed
 * comparison is actually semantically correct at runtime, not just syntactically valid C -- this
 * links against the real generated C for
 *   (defn classify [(a : I32)] : I32 (let [c (* a 2)] (if (= c 10) 1 0)))
 * and calls it with real inputs, confirming the comparison genuinely reads the let-bound value's
 * real numeric content (not, e.g., a truncated/misinterpreted pointer bit pattern).
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

extern char *classify(int a);

int main(void) {
    /* c = a*2; classify returns 1 iff c==10, i.e. a==5 */
    intptr_t r5 = (intptr_t)classify(5);
    intptr_t r3 = (intptr_t)classify(3);
    intptr_t r0 = (intptr_t)classify(0);
    printf("classify(5)=%ld (expected 1), classify(3)=%ld (expected 0), "
           "classify(0)=%ld (expected 0)\n", (long)r5, (long)r3, (long)r0);
    assert(r5 == 1);
    assert(r3 == 0);
    assert(r0 == 0);
    printf("driver_boxed_binop: OK -- the boxed let-bound I32 correctly unboxes for comparison, "
           "real runtime values, not just compiled clean.\n");
    return 0;
}
