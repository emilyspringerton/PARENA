/* driver_i32_roundtrip.c -- real behavioral verification for
 * selfhost/emit.prn's new real, non-pointer-shaped (I32) Ok/Err/Some
 * payload CONSTRUCTION support (2026-08-28), closing the "non-
 * pointer-shaped payload" gap this file's own header comment history
 * named repeatedly as still open, and completing the FULL real round
 * trip alongside the same day's earlier `deref` support: a value can
 * now be both PRODUCED and CONSUMED, scalar payload included, entirely
 * by real selfhost-emitted C, never hand-constructed or hand-
 * dereferenced in this driver.
 *
 * Links against real generated C for:
 *   (defn unwrap-or-zero [(o : Option)] : I32
 *     (match o
 *       ((Some x) (deref x))
 *       (None 0)))
 *   (defn make-answer [(dest : Arena @ :region/buffer)] : Option
 *     (Some 42))
 *   (defn make-none-i32 [] : Option
 *     None)
 * plus the new, real, unconditionally-emitted `int_box` helper
 * (int-box-helper-decl's own header comment) -- proving a real I32
 * value genuinely round-trips through a real heap box, a real Option
 * construction, and a real match+deref consumption, not just that gcc
 * accepts the generated text.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

extern char *unwrap_or_zero(Option o);
extern Option make_answer(Arena *dest);
extern Option make_none_i32(void);

int main(void) {
    Arena a;
    arena_init(&a);

    Option answer = make_answer(&a);
    Option none = make_none_i32();

    int r1 = (int)(intptr_t)unwrap_or_zero(answer);
    int r2 = (int)(intptr_t)unwrap_or_zero(none);

    printf("unwrap_or_zero(make_answer())=%d (expected 42), "
           "unwrap_or_zero(make_none_i32())=%d (expected 0)\n",
           r1, r2);
    assert(r1 == 42);
    assert(r2 == 0);

    printf("driver_i32_roundtrip: OK -- a real I32 value genuinely round-tripped through a real "
           "heap box, a real Option construction, and a real match+deref consumption, entirely "
           "selfhost-emitted on both ends, not just compiled clean.\n");
    return 0;
}
