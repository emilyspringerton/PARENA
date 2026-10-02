/* tests/test_heli_rules.c -- stdlib/shankpit/heli_rules.prn (SHANKPIT helicopter flight model, card #542).
 * Every expected number is hand-derived from the .prn source. */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>

#include "test_heli_rules_gen.c"

int main(void) {
    assert(heli_hover_collective_permille() == 500);

    /* collective lever: raise/lower 9/tick, clamped; released it relaxes 3/tick to hover and stops there */
    assert(heli_collective_step_permille(500, 1, 0) == 509);
    assert(heli_collective_step_permille(995, 1, 0) == 1000);
    assert(heli_collective_step_permille(500, 0, 1) == 491);
    assert(heli_collective_step_permille(4, 0, 1) == 0);
    assert(heli_collective_step_permille(800, 0, 0) == 797);
    assert(heli_collective_step_permille(502, 0, 0) == 500);
    assert(heli_collective_step_permille(200, 0, 0) == 203);
    assert(heli_collective_step_permille(499, 0, 0) == 500);
    assert(heli_collective_step_permille(500, 0, 0) == 500);
    assert(heli_collective_step_permille(500, 1, 1) == 509);          /* up wins */
    int c = 0;                                                         /* lever released from full down relaxes all the way */
    for (int i = 0; i < 200; i++) c = heli_collective_step_permille(c, 0, 0);
    assert(c == 500);

    /* thrust: hover lever = exactly 1 g, full = 2 g, empty = none */
    assert(heli_lift_permille(500) == 1000);
    assert(heli_lift_permille(1000) == 2000);
    assert(heli_lift_permille(0) == 0);

    /* cyclic + attitude lag */
    assert(heli_cyclic_target_milli(1000) == 24000);
    assert(heli_cyclic_target_milli(-500) == -12000);
    assert(heli_attitude_step_milli(0, 24000) == 2400);
    assert(heli_attitude_step_milli(2400, 24000) == 4560);
    assert(heli_attitude_step_milli(24000, 24000) == 24000);
    assert(heli_attitude_step_milli(24000, 0) == 21600);
    int a = 0;                                                         /* converges to within the integer-division residue */
    for (int i = 0; i < 100; i++) a = heli_attitude_step_milli(a, 24000);
    assert(a >= 23990 && a <= 24000);

    /* pedals -> yaw rate lag */
    assert(heli_yaw_target_milli(1000) == 2000);
    assert(heli_yaw_target_milli(-1000) == -2000);
    assert(heli_yaw_step_milli(0, 2000) == 285);
    assert(heli_yaw_step_milli(2000, 2000) == 2000);

    /* drag grows with speed */
    assert(heli_drag_permille(0) == 3);
    assert(heli_drag_permille(2000) == 14);
    assert(heli_drag_permille(1000) == 8);
    assert(heli_vertical_damp_permille() == 25);

    assert(heli_max_hspeed_milli() == 2400);
    assert(heli_max_vspeed_up_milli() == 550);
    assert(heli_max_vspeed_down_milli() == 700);

    printf("test_heli_rules: all checks passed\n");
    return 0;
}
