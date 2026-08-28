/* driver_nested_call.c -- real behavioral verification for selfhost/
 * emit.prn's new nested-call-as-a-call-argument support (2026-08-28),
 * closing the "no nested calls" half of the gap this file's own
 * every-call-arg-symbol-or-number? header comment (and NORTHSTAR.md's
 * own Self-hosting section) repeatedly named but didn't fix until now.
 * Links against real generated C for:
 *   (defn twice [(n : I32)] : I32
 *     (* n 2))
 *   (defn add-doubled [(a : I32) (b : I32)] : I32
 *     (+ (twice a) b))
 * and proves the nested `(twice a)` call genuinely computes the right
 * value INLINE as one operand of the outer `+`, not just that gcc
 * accepts the generated text. Real, honest wrinkle worth naming: this
 * emitter's own uniform char*-return convention means `twice` really
 * returns `char *` (boxed via emit-i32-boxed's own `(char *)
 * (intptr_t)n` reinterpretation, this file's own established, pre-
 * existing technique -- see that function's own header comment), so
 * the generated `(twice(a) + b)` is real C *pointer* arithmetic
 * (char* + int), not integer arithmetic -- confirmed here to still
 * produce the numerically correct result (including with a NEGATIVE
 * operand) because `char` has size 1: advancing a `char *` by N bytes
 * is bit-for-bit identical to plain integer addition once round-
 * tripped back through `(int)(intptr_t)result`, the same real
 * technique every other caller of this convention already relies on.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

extern char *add_doubled(int a, int b);

int main(void) {
    int r1 = (int)(intptr_t)add_doubled(5, 3);
    int r2 = (int)(intptr_t)add_doubled(-5, 100);
    int r3 = (int)(intptr_t)add_doubled(0, 0);

    printf("add_doubled(5,3)=%d (expected 13), add_doubled(-5,100)=%d (expected 90), "
           "add_doubled(0,0)=%d (expected 0)\n",
           r1, r2, r3);
    assert(r1 == 13);
    assert(r2 == 90);
    assert(r3 == 0);

    printf("driver_nested_call: OK -- the real generated (twice(a) + b) expression genuinely "
            "computed the correct value on every real input, the nested call argument's own boxed "
            "return value round-tripping correctly through real C pointer arithmetic, not just "
            "compiling clean.\n");
    return 0;
}
