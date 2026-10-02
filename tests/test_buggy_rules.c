/* tests/test_buggy_rules.c -- stdlib/shankpit/buggy_rules.prn (SHANKPIT programmable buggy, card #468).
 * Every expected number is hand-derived from the .prn source (fixed-point permille / milli). */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>

#include "test_buggy_rules_gen.c"

int main(void) {
    assert(buggy_top_speed_milli() == 6240);
    assert(buggy_reverse_top_speed_milli() == 1800);

    /* drive force curve: band 1 (0..220) 1300 -> 920 */
    assert(buggy_drive_force_permille(-50) == 1300);
    assert(buggy_drive_force_permille(0) == 1300);
    assert(buggy_drive_force_permille(110) == 1110);          /* 1300 + (-380*110)/220 */
    assert(buggy_drive_force_permille(220) == 920);           /* start of band 2 */
    assert(buggy_drive_force_permille(370) == 770);           /* 920 + (-300*150)/300 */
    assert(buggy_drive_force_permille(520) == 620);           /* start of band 3 */
    assert(buggy_drive_force_permille(650) == 730);           /* 620 + (220*130)/260 */
    assert(buggy_drive_force_permille(780) == 840);           /* start of the taper */
    assert(buggy_drive_force_permille(890) == 450);           /* t = 0.5 -> smoothstep 0.5 -> 840 - 390 */
    assert(buggy_drive_force_permille(999) == 61);            /* t = 995 -> s = 999 -> 840 - 779 */
    assert(buggy_drive_force_permille(1000) == 60);           /* floor at top speed */
    assert(buggy_drive_force_permille(5000) == 60);

    /* the curve never goes negative and, past the first band, never rises except 520..780 (second wind) */
    int prev = buggy_drive_force_permille(0);
    for (int s = 1; s <= 1000; s++) {
        int f = buggy_drive_force_permille(s);
        assert(f > 0);
        if (s <= 520 || s >= 780) assert(f <= prev + 1);      /* +1: integer rounding at a band seam */
        prev = f;
    }

    assert(buggy_turn_rate_milli(0) == 3100);
    assert(buggy_turn_rate_milli(500) == 2000);
    assert(buggy_turn_rate_milli(1000) == 900);
    assert(buggy_turn_rate_milli(1500) == 900);               /* clamped */
    assert(buggy_turn_rate_milli(-5) == 3100);

    assert(buggy_steer_authority_permille(0) == 380);
    assert(buggy_steer_authority_permille(500) == 690);
    assert(buggy_steer_authority_permille(1000) == 1000);

    assert(buggy_lateral_grip_permille(0) == 74);             /* 120 * 620 / 1000 = 74.4 */
    assert(buggy_lateral_grip_permille(500) == 115);          /* 120 * 960 / 1000 */
    assert(buggy_lateral_grip_permille(1000) == 156);         /* 120 * 1300 / 1000 */

    printf("test_buggy_rules: all checks passed\n");
    return 0;
}
