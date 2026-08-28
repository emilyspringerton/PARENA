/* driver_i32_param_ctor.c -- real behavioral verification for
 * selfhost/emit.prn's new I32-typed BARE-SYMBOL Ok/Err/Some payload
 * support (2026-08-28), closing the gap result-option-payload-is-
 * i32?'s own header comment named explicitly as not-yet-attempted.
 * Real, confirmed-live problem found via a direct probe (not
 * guessed): `(defn wrap [(n : I32) (dest : Arena @ Region)] : Option
 * (Some n))` used to silently satisfy the EXISTING "pointer-shaped"
 * payload branch (a bare symbol, kind NSymbol, was already accepted
 * unconditionally), emitting `option_some(n)` with `n` a raw C `int`
 * passed where the runtime's own real `void *value` is expected -- a
 * genuine `-Werror=int-conversion` compile failure (an honest
 * failure, not a silent miscompile, but a real, fixable gap all the
 * same). Links against real generated C for:
 *   (defn unwrap-or-zero [(o : Option)] : I32
 *     (match o
 *       ((Some x) (deref x))
 *       (None 0)))
 *   (defn wrap [(n : I32) (dest : Arena @ Region)] : Option
 *     (Some n))
 * and proves the real I32 param genuinely round-trips through a real
 * heap box, a real Option construction from a BARE SYMBOL payload,
 * and a real match+deref consumption, on multiple real inputs
 * including a negative one -- not just that gcc accepts the generated
 * text.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

extern char *unwrap_or_zero(Option o);
extern Option wrap(int n, Arena *dest);

int main(void) {
    Arena a;
    arena_init(&a);

    Option wrapped = wrap(123, &a);
    Option wrapped_neg = wrap(-9, &a);

    int r1 = (int)(intptr_t)unwrap_or_zero(wrapped);
    int r2 = (int)(intptr_t)unwrap_or_zero(wrapped_neg);

    printf("unwrap_or_zero(wrap(123))=%d (expected 123), unwrap_or_zero(wrap(-9))=%d "
           "(expected -9)\n",
           r1, r2);
    assert(r1 == 123);
    assert(r2 == -9);

    printf("driver_i32_param_ctor: OK -- the real I32 param genuinely round-tripped through a "
           "real heap box, a real Option construction from a bare-symbol payload, and a real "
           "match+deref consumption, on every real input including a negative one, not just "
           "compiled clean.\n");
    return 0;
}
