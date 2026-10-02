/* tests/test_camera_rules.c -- stdlib/shankpit/camera_rules.prn (SHANKPIT broadcast cameras, cards #456/#457/#458).
 * Every expected number is hand-derived from the .prn source (fixed-point: milli-units, permille, ms). C integer
 * division truncates toward zero, and so does the generated code, so negatives below are derived the same way. */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>

#include "test_camera_rules_gen.c"

int main(void) {
    /* operator reaction: rookie 700 ms .. veteran 120 ms, clamped */
    assert(cam_reaction_ms(0) == 700);
    assert(cam_reaction_ms(500) == 410);          /* 700 - 580*500/1000 */
    assert(cam_reaction_ms(1000) == 120);
    assert(cam_reaction_ms(-5) == 700);
    assert(cam_reaction_ms(2000) == 120);

    /* lagged tracking: moves dt/(tau+dt) of the gap, never overshoots */
    assert(cam_smooth_step(0, 1000, 16, 100) == 137);       /* 1000*16/116 = 137.9 -> 137 */
    assert(cam_smooth_step(1000, 0, 16, 100) == 863);       /* -137.9 -> -137 */
    assert(cam_smooth_step(500, 500, 16, 100) == 500);      /* already there */
    assert(cam_smooth_step(0, 1000, 0, 100) == 0);          /* no time, no motion */
    assert(cam_smooth_step(0, 1000, 16, 0) == 1000);        /* zero lag: snaps to the target */
    {   /* converges monotonically toward the target and never passes it */
        int pos = 0;
        for (int i = 0; i < 200; i++) { int n = cam_smooth_step(pos, 1000, 16, 100); assert(n >= pos && n <= 1000); pos = n; }
        assert(pos > 990);
    }

    assert(cam_lead_milli(6000, 500) == 3000);               /* 6 u/s, 0.5 s ahead = 3 units */
    assert(cam_lead_milli(-4000, 250) == -1000);
    assert(cam_lead_milli(0, 500) == 0);

    /* director: how interesting is this player */
    assert(cam_action_score(10000, 3000, 2000, 1, 50) == 1575);   /* 800 + 150 + 300 + 250 + 75 */
    assert(cam_action_score(80000, 0, 100000, 0, 100) == 0);      /* far, idle, no kills, healthy */
    assert(cam_action_score(50000, 10000, 4000, 0, 0) == 450);    /* boundaries: dist 50000 and since 4000 score 0; speed caps at 300; health 0 = +150 */
    assert(cam_action_score(60000, 0, 0, 0, 100) == 600);         /* a kill this very instant */

    /* cuts: hold time, hysteresis, and the fast-cut for a much better shot */
    assert(cam_should_cut(500, 800, 1000, 3000, 200) == 0);       /* held only 1 s of 3 */
    assert(cam_should_cut(500, 800, 3000, 3000, 200) == 1);       /* 800000 > 500*1200 */
    assert(cam_should_cut(500, 650, 3000, 3000, 200) == 1);       /* 650000 > 600000 */
    assert(cam_should_cut(500, 600, 3000, 3000, 200) == 0);       /* 600000 > 600000 is false: no ping-pong */
    assert(cam_should_cut(500, 1000, 1000, 3000, 200) == 1);      /* twice as good: hold shrinks to 1000 ms */
    assert(cam_should_cut(500, 1000, 999, 3000, 200) == 0);
    assert(cam_should_cut(0, 1, 5000, 3000, 200) == 1);           /* anything beats nothing */

    /* triangle wave (period 100): -1000 at 0, 0 at 25, +1000 at 50, 0 at 75, wraps */
    assert(cam_tri(0, 100) == -1000);
    assert(cam_tri(25, 100) == 0);
    assert(cam_tri(50, 100) == 1000);
    assert(cam_tri(75, 100) == 0);
    assert(cam_tri(100, 100) == -1000);
    assert(cam_tri(125, 100) == 0);

    /* handheld shake: two incommensurate waves; 0 intensity is a tripod */
    assert(cam_shake_milli(0, 0) == 0);
    assert(cam_shake_milli(0, 1000) == -463);       /* 2*(-1000) + 610 = -1390; 1000*-1390/3000 */
    assert(cam_shake_milli(0, 500) == -231);
    assert(cam_shake_milli(65, 1000) == 662);       /* 2*1000 + (-13) = 1987; 1987000/3000 */
    for (int t = 0; t < 2000; t += 7) { int v = cam_shake_milli(t, 1000); assert(v >= -1000 && v <= 1000); }

    /* orbit: 0.1-degree resolution */
    assert(cam_orbit_angle_milli(0, 10000) == 0);
    assert(cam_orbit_angle_milli(2500, 10000) == 90000);
    assert(cam_orbit_angle_milli(5000, 10000) == 180000);
    assert(cam_orbit_angle_milli(10000, 10000) == 0);       /* wraps */
    assert(cam_orbit_angle_milli(12500, 10000) == 90000);
    assert(cam_orbit_angle_milli(589999, 590000) == 359900);  /* a 9.8-minute lap does not overflow I32 */

    /* zoom operator: wide near, tight far */
    assert(cam_fov_milli(0) == 70000);
    assert(cam_fov_milli(15000) == 70000);
    assert(cam_fov_milli(47500) == 55000);
    assert(cam_fov_milli(80000) == 40000);
    assert(cam_fov_milli(200000) == 40000);

    printf("test_camera_rules: all checks passed\n");
    return 0;
}
