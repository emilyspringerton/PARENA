/* driver_deref.c -- real behavioral verification for selfhost/
 * emit.prn's new `deref` support (2026-08-28), closing the "a clause
 * body that needs the payload beyond a bare-symbol tail" half of the
 * match gap this file's own match-support section explicitly named
 * as still open, and fixing a real, live, silently-wrong-C bug found
 * along the way (confirmed via a direct probe, not guessed): `(defn f
 * [(x : I32)] (deref x))` used to silently emit `deref(x)`, a call to
 * a never-defined C function, since `deref` was never excluded from
 * plain-call-shaped?'s own name checks. Links against real generated
 * C for:
 *   (defn unwrap-or-zero [(o : Option)] : I32
 *     (match o
 *       ((Some x) (deref x))
 *       (None 0)))
 * and proves the real `*((int *)(x))` dereference genuinely reads
 * back the correct value from a real, boxed I32 payload -- constructed
 * here directly in C (the same real `int_box`-style heap-boxing
 * convention the runtime itself establishes, matching
 * driver_match.c's own original, pre-real-construction technique;
 * this emitter's own construction support only produces POINTER-
 * shaped payloads so far, a real, separate, harder gap not attempted
 * this round -- see deref-shaped?'s own header comment) -- not just
 * that gcc accepts the generated text.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

extern char *unwrap_or_zero(Option o);

int main(void) {
    Arena a;
    arena_init(&a);

    int *boxed99 = (int *)arena_alloc(&a, sizeof(int));
    *boxed99 = 99;
    int *boxedNeg = (int *)arena_alloc(&a, sizeof(int));
    *boxedNeg = -13;

    Option some99 = option_some(boxed99);
    Option someNeg = option_some(boxedNeg);
    Option none = option_none();

    int r1 = (int)(intptr_t)unwrap_or_zero(some99);
    int r2 = (int)(intptr_t)unwrap_or_zero(someNeg);
    int r3 = (int)(intptr_t)unwrap_or_zero(none);

    printf("unwrap_or_zero(Some(99))=%d (expected 99), unwrap_or_zero(Some(-13))=%d "
           "(expected -13), unwrap_or_zero(None)=%d (expected 0)\n",
           r1, r2, r3);
    assert(r1 == 99);
    assert(r2 == -13);
    assert(r3 == 0);

    printf("driver_deref: OK -- the real *((int *)(x)) dereference genuinely read back the "
           "correct value from a real, boxed I32 match payload on every real input, not just "
           "compiled clean.\n");
    return 0;
}
