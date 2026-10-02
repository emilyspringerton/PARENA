/* tests/test_ambient_rules.c -- stdlib/shankpit/ambient_rules.prn (SHANKPIT ambient slider, card #460).
 * Every expected number is hand-derived from the .prn source. */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>

#include "test_ambient_rules_gen.c"

int main(void) {
    assert(ambient_level_max() == 10);
    assert(ambient_level_default() == 3);

    assert(ambient_clamp(-5) == 0);
    assert(ambient_clamp(0) == 0);
    assert(ambient_clamp(7) == 7);
    assert(ambient_clamp(10) == 10);
    assert(ambient_clamp(99) == 10);

    assert(ambient_step(3, 1) == 4);
    assert(ambient_step(3, -1) == 2);
    assert(ambient_step(0, -1) == 0);     /* stops at the bottom, no wrap */
    assert(ambient_step(10, 1) == 10);    /* stops at the top, no wrap */

    assert(ambient_boost_permille(0) == 0);       /* level 0 = original look, exactly */
    assert(ambient_boost_permille(3) == 120);     /* default: +0.12 */
    assert(ambient_boost_permille(10) == 400);
    assert(ambient_boost_permille(-4) == 0);      /* out-of-range input is clamped, never negative */
    assert(ambient_boost_permille(50) == 400);

    printf("test_ambient_rules: all checks passed\n");
    return 0;
}
