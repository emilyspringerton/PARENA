/* tests/test_camera_unify.c -- round-trip test of the unified camera mods (BACKLOG SECTION 636, UNIFY-0/4):
 *   stdlib/camera/ease.prn, zoom_cam_mod.prn, click_move_mod.prn, camera_follow_mod.prn
 * compiled by the real parena compiler to C and called through its exported symbols.
 *
 * Every expected number is derived by hand in the comment beside it (e^-1 = 0.63212, 1.15x notch, etc.), not
 * copied from program output. The SAME file is copied verbatim into REDGARDEN/tests/test_camera_unify.c and
 * SHANKPIT/packages/render/camera_unify_test.c, each linked against that repo's own checked-in generated C, so
 * a drift between the three copies of the generated code is a test failure in the repo that drifted.
 * Build with -DPARENA_NO_GRAPHICS. PARENA: -DCAMERA_UNIFY_INCLUDE_GEN=\"<gen.c>\" includes the freshly generated C.
 * REDGARDEN/SHANKPIT: link their checked-in camera_unify_mod.c and put camera_unify_mod_host.h on the -I path. */
#ifdef CAMERA_UNIFY_INCLUDE_GEN
#include "parena_runtime.h"
#include CAMERA_UNIFY_INCLUDE_GEN
#else
#include "camera_unify_mod_host.h"
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int g_fail = 0, g_checks = 0;
#define CHECK(cond, msg) do { g_checks++; if (!(cond)) { g_fail++; printf("FAIL  %s  (line %d)\n", msg, __LINE__); } } while (0)
#define NEAR(a, b, tol, msg) do { g_checks++; long _a = (a), _b = (b); if (labs(_a - _b) > (tol)) { g_fail++; \
    printf("FAIL  %s: got %ld want %ld +-%d  (line %d)\n", msg, _a, _b, (int)(tol), __LINE__); } } while (0)

static void test_ease(void) {
    /* alpha(dt=tau) = 1 - e^-1 = 0.63212 -> Q15 20713 ; alpha(2 tau) = 1 - e^-2 = 0.86466 -> 28333 */
    NEAR(cam_ease_alpha_q15(140, 140), 20713, 40, "alpha at 1 tau");
    NEAR(cam_ease_alpha_q15(280, 140), 28333, 40, "alpha at 2 tau");
    /* half a tau: 1 - e^-0.5 = 0.39347 -> 12893 */
    NEAR(cam_ease_alpha_q15(70, 140), 12893, 40, "alpha at 0.5 tau");
    CHECK(cam_ease_alpha_q15(0, 140) == 0, "dt 0 -> no movement");
    CHECK(cam_ease_alpha_q15(-5, 140) == 0, "negative dt -> no movement");
    CHECK(cam_ease_alpha_q15(16, 0) == 32768, "tau 0 -> instant");
    CHECK(cam_ease_alpha_q15(16, -3) == 32768, "negative tau -> instant");
    CHECK(cam_ease_alpha_q15(100000, 10) == cam_ease_alpha_q15(1000, 10), "dt clamped at 1000 ms");
    CHECK(cam_ease_alpha_q15(1000, 10) > 32600, "huge dt/tau ~ arrived");
    /* lerp: 1000 * 0.5 = 500; sign symmetric; never stalls on a tiny gap; never past the gap */
    CHECK(cam_ease_lerp(1000, 16384) == 500, "lerp half");
    CHECK(cam_ease_lerp(-1000, 16384) == -500, "lerp half negative");
    CHECK(cam_ease_lerp(3, 10) == 1, "tiny gap still moves 1");
    CHECK(cam_ease_lerp(-3, 10) == -1, "tiny negative gap still moves 1");
    CHECK(cam_ease_lerp(3, 0) == 0, "alpha 0 moves nothing");
    CHECK(cam_ease_lerp(5, 32768) == 5, "alpha 1 lands exactly");
    CHECK(cam_ease_lerp(100000000, 32768) == 100000000, "big gap alpha 1 no overflow");
    /* wrap: 190000 -> -170000, 180000 stays, -180000 -> 180000, 720000 -> 0 */
    CHECK(cam_ease_wrap_mdeg(190000) == -170000, "wrap 190");
    CHECK(cam_ease_wrap_mdeg(180000) == 180000, "wrap 180 stays");
    CHECK(cam_ease_wrap_mdeg(-180000) == 180000, "wrap -180");
    CHECK(cam_ease_wrap_mdeg(720000) == 0, "wrap 720");
    CHECK(cam_ease_wrap_mdeg(-10000) == -10000, "wrap small negative");
}

/* Simulate `total_ms` of arm easing at a fixed dt. */
static int sim_arm(int arm, int target, int dt, int total_ms) {
    for (int t = 0; t < total_ms; t += dt) arm = zoom_cam_ease_arm_milli(arm, target, dt, zoom_cam_tau_ms());
    return arm;
}

static void test_zoom(void) {
    CHECK(zoom_cam_tau_ms() == 140 && zoom_cam_fp_enter_milli() == 600 && zoom_cam_fp_exit_milli() == 800, "consts");
    /* up = zoom IN: 10000 / 1.15 = 8695.65 -> 8695 (integer 10000*1000/1150) */
    CHECK(zoom_cam_wheel_target_milli(10000, 1, 1150, 0, 30000) == 8695, "wheel up zooms in");
    /* down = zoom OUT: 10000 * 1.15 = 11500 */
    CHECK(zoom_cam_wheel_target_milli(10000, -1, 1150, 0, 30000) == 11500, "wheel down zooms out");
    CHECK(zoom_cam_wheel_target_milli(10000, 1, 0, 0, 30000) == 8695, "step 0 -> default 1150");
    CHECK(zoom_cam_wheel_target_milli(10000, 0, 1150, 0, 30000) == 10000, "no clicks no change");
    CHECK(zoom_cam_wheel_target_milli(10000, 1, 5, 0, 30000) == 9900, "step clamped up to 1010: 10000000/1010 = 9900");
    CHECK(zoom_cam_wheel_target_milli(10000, 1, 99999, 0, 30000) == 5000, "step clamped to 2000: 10000/2");
    /* clamps: out of range inputs and results */
    CHECK(zoom_cam_wheel_target_milli(29000, -1, 1150, 0, 30000) == 30000, "max clamp (33350 -> 30000)");
    CHECK(zoom_cam_wheel_target_milli(99000, 0, 1150, 0, 30000) == 30000, "over-max target clamped even with 0 clicks");
    CHECK(zoom_cam_wheel_target_milli(4500, 1, 1150, 4000, 30000) == 4000, "min 4000 (no first person): 3913 -> 4000");
    CHECK(zoom_cam_wheel_target_milli(0, 1, 1150, -50, 30000) == 0, "negative min treated as 0 (stays first person, never negative)");
    CHECK(zoom_cam_wheel_target_milli(0, -1, 1150, 0, 100) == 100, "hi below floor 2000 clamps to hi");
    /* 8 notches in: 30000 / 1.15^8 (3.0590) = 9807, snap floor not reached; +-60 for integer truncation */
    NEAR(zoom_cam_wheel_target_milli(30000, 8, 1150, 0, 30000), 9807, 60, "8 notches in");
    CHECK(zoom_cam_wheel_target_milli(30000, 100, 1150, 0, 30000) == zoom_cam_wheel_target_milli(30000, 8, 1150, 0, 30000), "clicks clamped to +8");
    CHECK(zoom_cam_wheel_target_milli(1000, -100, 1150, 0, 30000) == zoom_cam_wheel_target_milli(1000, -8, 1150, 0, 30000), "clicks clamped to -8");
    /* wheel sign: monotone */
    CHECK(zoom_cam_wheel_target_milli(10000, 3, 1150, 0, 30000) < 10000 && zoom_cam_wheel_target_milli(10000, -3, 1150, 0, 30000) > 10000, "sign");
    /* snap to first person: 2000 -> 1739 -> 1512 -> (1314 < 1500) -> 0 */
    int t = 2000;
    t = zoom_cam_wheel_target_milli(t, 1, 1150, 0, 30000); CHECK(t == 1739, "2000/1.15 = 1739");
    t = zoom_cam_wheel_target_milli(t, 1, 1150, 0, 30000); CHECK(t == 1512, "1739/1.15 = 1512");
    t = zoom_cam_wheel_target_milli(t, 1, 1150, 0, 30000); CHECK(t == 0, "1512/1.15 = 1314 < snap 1500 -> first person");
    CHECK(zoom_cam_wheel_target_milli(0, 1, 1150, 0, 30000) == 0, "zoom in at first person stays");
    CHECK(zoom_cam_wheel_target_milli(0, -1, 1150, 0, 30000) == 2000, "first notch out jumps to floor 2000 (clear of exit 800)");
    CHECK(zoom_cam_wheel_target_milli(2000, -1, 1150, 0, 30000) == 2300, "then geometric: 2000*1.15");
    CHECK(zoom_cam_wheel_target_milli(0, -3, 1150, 0, 30000) == 2645, "0 -> 2000 -> 2300 -> 2645");

    /* easing: dt<=0 holds, tau 0 jumps, non-negative, snap */
    CHECK(zoom_cam_ease_arm_milli(5000, 0, 0, 140) == 5000, "dt 0 holds");
    CHECK(zoom_cam_ease_arm_milli(5000, 0, -16, 140) == 5000, "negative dt holds");
    CHECK(zoom_cam_ease_arm_milli(5000, 12000, 16, 0) == 12000, "tau 0 jumps");
    CHECK(zoom_cam_ease_arm_milli(2, 0, 16, 140) == 0, "within 2 milli snaps");
    CHECK(zoom_cam_ease_arm_milli(0, -500, 16, 140) == 0, "never negative");
    /* one tau: 10000 -> 10000 * e^-1 = 3679 */
    NEAR(zoom_cam_ease_arm_milli(10000, 0, 140, 140), 3679, 15, "one tau leaves 36.8%");
    /* frame-rate independence: 280 ms (2 tau) from 10000 to 0 leaves 10000 e^-2 = 1353 at ANY dt */
    int a250 = sim_arm(10000, 0, 4, 280), a140 = sim_arm(10000, 0, 2, 280), a60 = sim_arm(10000, 0, 7, 280);
    int a30 = sim_arm(10000, 0, 14, 280), a20 = sim_arm(10000, 0, 20, 280), a8 = sim_arm(10000, 0, 28, 280);
    NEAR(a250, 1353, 40, "dt 4ms (250fps)"); NEAR(a140, 1353, 40, "dt 2ms (500fps)"); NEAR(a60, 1353, 40, "dt 7ms (143fps)");
    NEAR(a30, 1353, 40, "dt 14ms (71fps)"); NEAR(a20, 1353, 40, "dt 20ms (50fps)"); NEAR(a8, 1353, 40, "dt 28ms (36fps)");
    /* real 60 fps (16,17,17 ms pattern, 16.67 avg) vs real 144 fps (7,7,6 ms, 6.67 avg): the same 300 ms ends together.
       Expected 10000 * e^(-300/140) = 1173 for both. */
    { const int p60[3] = {16, 17, 17}, p144[3] = {7, 7, 6};
      int e60 = 10000, e144 = 10000;
      for (int i = 0; i < 18; i++) e60 = zoom_cam_ease_arm_milli(e60, 0, p60[i % 3], 140);         /* 18 x 16.67 = 300 ms */
      for (int i = 0; i < 45; i++) e144 = zoom_cam_ease_arm_milli(e144, 0, p144[i % 3], 140);      /* 45 x 6.67 = 300 ms */
      NEAR(e60, 1173, 40, "real 60 fps, 300 ms"); NEAR(e144, 1173, 40, "real 144 fps, 300 ms"); NEAR(e60, e144, 30, "60 vs 144 fps agree");
    }
    /* upward ease also matches: 0 -> 8000 in 140 ms leaves 8000 * 0.632 = 5057 */
    NEAR(sim_arm(0, 8000, 7, 140), 5057, 40, "ease out 7ms steps");
    NEAR(sim_arm(0, 8000, 20, 140), 5057, 40, "ease out 20ms steps");
    CHECK(sim_arm(10000, 0, 16, 3000) == 0, "converges all the way to first person (0)");
    CHECK(sim_arm(3000, 12000, 6, 3000) == 12000, "converges up to target");
    /* a hitch (dt 1 s) is stable: no overshoot */
    CHECK(zoom_cam_ease_arm_milli(10000, 0, 5000, 140) >= 0, "huge dt no overshoot below 0");
    CHECK(zoom_cam_ease_arm_milli(10000, 4000, 5000, 140) >= 4000, "huge dt never overshoots target");

    /* hysteresis: enter < 600, exit >= 800 */
    CHECK(zoom_cam_is_first_person(600, 0) == 0 && zoom_cam_is_first_person(599, 0) == 1, "enter threshold 600");
    CHECK(zoom_cam_is_first_person(799, 1) == 1 && zoom_cam_is_first_person(800, 1) == 0, "exit threshold 800");
    CHECK(zoom_cam_is_first_person(0, 0) == 1 && zoom_cam_is_first_person(20000, 1) == 0, "extremes");
    /* sweep in and out with the flag fed back: exactly two transitions, at 599 going in and 800 coming out */
    int fp = 0, trans = 0, enter_at = -1, exit_at = -1;
    for (int arm = 1500; arm >= 0; arm--) { int n = zoom_cam_is_first_person(arm, fp); if (n != fp) { trans++; enter_at = arm; } fp = n; }
    for (int arm = 0; arm <= 1500; arm++) { int n = zoom_cam_is_first_person(arm, fp); if (n != fp) { trans++; exit_at = arm; } fp = n; }
    CHECK(trans == 2 && enter_at == 599 && exit_at == 800, "sweep: one enter at 599, one exit at 800");
    /* flicker: an arm jittering across the 600 line (590..610) while NOT in fp flips once on entry, then holds */
    fp = 0; trans = 0;
    for (int i = 0; i < 400; i++) { int arm = 590 + ((i * 7) % 21); int n = zoom_cam_is_first_person(arm, fp); if (n != fp) trans++; fp = n; }
    CHECK(trans == 1 && fp == 1, "jitter around 600 enters once, never flickers back");
    /* jitter around the 800 exit line while in fp: stays in until >= 800, then out and stays out for 790..810?? */
    fp = 1; trans = 0;
    for (int i = 0; i < 400; i++) { int arm = 700 + ((i * 13) % 99); int n = zoom_cam_is_first_person(arm, fp); if (n != fp) trans++; fp = n; }
    CHECK(trans == 0 && fp == 1, "jitter 700..798 while first person: no flicker");
    fp = 0; trans = 0;
    for (int i = 0; i < 400; i++) { int arm = 600 + ((i * 13) % 199); int n = zoom_cam_is_first_person(arm, fp); if (n != fp) trans++; fp = n; }
    CHECK(trans == 0 && fp == 0, "jitter 600..798 while third person: no flicker");
}

static void test_follow(void) {
    CHECK(cam_follow_manual_pause_ms() == 800 && cam_follow_moving_speed_milli() == 500, "follow consts");
    /* recenter-active table: style 0/1/2 x speed x idle x fp */
    CHECK(cam_follow_recenter_active(0, 9000, 60000, 0) == 0, "Free never recenters");
    CHECK(cam_follow_recenter_active(1, 600, 1000, 0) == 1, "Behind-when-moving: running -> yes");
    CHECK(cam_follow_recenter_active(1, 500, 1000, 0) == 0, "speed exactly 0.5 is not > 0.5");
    CHECK(cam_follow_recenter_active(1, 0, 60000, 0) == 0, "Behind-when-moving: standing -> no");
    CHECK(cam_follow_recenter_active(2, 0, 60000, 0) == 1, "Always: standing -> yes");
    CHECK(cam_follow_recenter_active(1, 600, 799, 0) == 0 && cam_follow_recenter_active(1, 600, 800, 0) == 1, "manual pause 800 ms");
    CHECK(cam_follow_recenter_active(2, 0, 0, 0) == 0, "Always paused right after manual orbit");
    CHECK(cam_follow_recenter_active(2, 9000, 60000, 1) == 0, "first person ignores");
    CHECK(cam_follow_recenter_active(-3, 9000, 60000, 0) == 0, "bad style -> Free");
    /* manual idle timer */
    CHECK(cam_follow_manual_idle_ms(500, 16, 1) == 0, "manual input resets");
    CHECK(cam_follow_manual_idle_ms(500, 16, 0) == 516, "grows by dt");
    CHECK(cam_follow_manual_idle_ms(500, 0, 0) == 500 && cam_follow_manual_idle_ms(500, -9, 0) == 500, "dt<=0 holds");
    CHECK(cam_follow_manual_idle_ms(59990, 100, 0) == 60000, "capped at a minute");
    int idle = 0; int manual_frames = 0, rec = 0;
    for (int ms = 0; ms < 1600; ms += 16) { idle = cam_follow_manual_idle_ms(idle, 16, ms < 160 ? 1 : 0); if (ms < 160) manual_frames++; rec = cam_follow_recenter_active(2, 0, idle, 0); if (ms < 160 + 800 - 16) CHECK(rec == 0, "paused during orbit + 800 ms"); }
    CHECK(rec == 1 && manual_frames == 10, "recenter resumes after the pause");

    /* yaw: normal tau 450, dt 450 -> alpha 0.632. Shortest arc across 0/360 */
    NEAR(cam_follow_yaw_step_mdeg(350000, 10000, 450, 1, 1), 2642, 40, "wrap forward: 350 -> 10 closes 63% of +20: 12642 -> 362642 mod 360000 = 2642");
    NEAR(cam_follow_yaw_step_mdeg(10000, 350000, 450, 1, 1), 357358, 40, "wrap backward");
    CHECK(cam_follow_yaw_step_mdeg(12345, 200000, 450, 1, 0) == 12345, "inactive holds");
    CHECK(cam_follow_yaw_step_mdeg(12345, 200000, 0, 1, 1) == 12345 && cam_follow_yaw_step_mdeg(12345, 200000, -4, 1, 1) == 12345, "dt<=0 holds");
    CHECK(cam_follow_recenter_tau_ms(0) == 900 && cam_follow_recenter_tau_ms(1) == 450 && cam_follow_recenter_tau_ms(2) == 180 && cam_follow_recenter_tau_ms(9) == 450, "tau table");
    /* slow < normal < fast per-frame movement (diff 40000 -> moves 1-e^-(16/tau): 1.76% / 3.49% / 8.50%: 706 / 1397 / 3400 */
    int s0 = cam_follow_yaw_step_mdeg(0, 40000, 16, 0, 1), s1 = cam_follow_yaw_step_mdeg(0, 40000, 16, 1, 1), s2 = cam_follow_yaw_step_mdeg(0, 40000, 16, 2, 1);
    NEAR(s0, 706, 20, "slow"); NEAR(s1, 1397, 20, "normal"); NEAR(s2, 3400, 30, "fast"); CHECK(s0 < s1 && s1 < s2, "ordering");
    /* 180 degrees exactly is a defined direction (+) and does not jitter */
    int y = cam_follow_yaw_step_mdeg(0, 180000, 16, 1, 1); CHECK(y > 0 && y < 180000, "exact opposite picks +");
    /* frame-rate independence over 900 ms (2 tau normal): 90000 * (1 - e^-2) = 77819 */
    int d1 = 0, d2 = 0;
    for (int t = 0; t < 900; t += 6) d1 = cam_follow_yaw_step_mdeg(d1, 90000, 6, 1, 1);
    for (int t = 0; t < 900; t += 30) d2 = cam_follow_yaw_step_mdeg(d2, 90000, 30, 1, 1);
    NEAR(d1, 77819, 400, "yaw 6ms steps"); NEAR(d2, 77819, 400, "yaw 30ms steps");
    for (int t = 0; t < 6000; t += 8) d1 = cam_follow_yaw_step_mdeg(d1, 90000, 8, 1, 1);
    NEAR(d1, 90000, 40, "yaw converges");

    /* slope -> pitch target: off 0; dead zone 60; 100 permille -> (100-60)*22 = 880 (uphill ahead = negative) */
    CHECK(cam_follow_slope_pitch_target_mdeg(0, 500) == 0, "terrain Off");
    CHECK(cam_follow_slope_pitch_target_mdeg(1, 60) == 0 && cam_follow_slope_pitch_target_mdeg(1, -60) == 0, "dead zone");
    CHECK(cam_follow_slope_pitch_target_mdeg(1, 100) == -880, "uphill 10%");
    CHECK(cam_follow_slope_pitch_target_mdeg(1, -100) == 880, "downhill 10%");
    CHECK(cam_follow_slope_pitch_target_mdeg(1, 1000) == -15000 && cam_follow_slope_pitch_target_mdeg(1, -9000) == 15000, "clamped to +-15 degrees");
    /* damping: 350 ms constant -> one tau gets 63.2% of -880 = -556 */
    NEAR(cam_follow_pitch_ease_mdeg(0, -880, 350), -556, 6, "pitch ease one tau");
    CHECK(cam_follow_pitch_ease_mdeg(100, -880, 0) == 100, "pitch ease dt 0 holds");
    int p = 0, maxstep = 0;   /* a step change in slope eases: no frame moves more than 6% of the gap at 16 ms (1-e^-(16/350) = 4.5%) */
    for (int i = 0; i < 200; i++) { int n = cam_follow_pitch_ease_mdeg(p, -15000, 16); int st = abs(n - p); if (st > maxstep) maxstep = st; p = n; }
    CHECK(maxstep <= 15000 * 5 / 100 + 2, "damped: first frame moves <= 4.6% (690)"); NEAR(p, -15000, 30, "pitch converges");
    /* wall damp: never fights pull-in */
    CHECK(cam_follow_wall_damp_mdeg(10000, 1000) == 10000, "arm fully out: full offset");
    CHECK(cam_follow_wall_damp_mdeg(10000, 500) == 5000, "half pulled in: half offset");
    CHECK(cam_follow_wall_damp_mdeg(10000, 0) == 0, "fully pulled in: no offset");
    CHECK(cam_follow_wall_damp_mdeg(-10000, 500) == -5000, "negative symmetric");
    CHECK(cam_follow_wall_damp_mdeg(10000, 1500) == 10000 && cam_follow_wall_damp_mdeg(10000, -5) == 0, "fraction clamped 0..1000");
    /* focus height: Off rigid; Slope eases a curb; fast on big gap */
    CHECK(cam_follow_focus_y_step_milli(0, 0, 777, 16) == 777, "terrain Off: focus == player");
    NEAR(cam_follow_focus_y_step_milli(1, 0, 500, 220), 316, 3, "curb 0.5: one tau 220 -> 63.2%");
    NEAR(cam_follow_focus_y_step_milli(1, 0, 10000, 80), 6321, 8, "fall 10: tau 80 -> 63.2%");
    CHECK(cam_follow_focus_y_step_milli(1, 123, 500, 0) == 123, "dt 0 holds");
    int fy = 0; for (int i = 0; i < 400; i++) fy = cam_follow_focus_y_step_milli(1, fy, 2000, 16); NEAR(fy, 2000, 3, "focus converges");
    /* smoothing */
    CHECK(cam_follow_smooth_tau_ms(0) == 0 && cam_follow_smooth_tau_ms(1) == 60 && cam_follow_smooth_tau_ms(2) == 160 && cam_follow_smooth_tau_ms(7) == 0, "smooth taus");
    CHECK(cam_follow_smooth_step_milli(0, 5, 1000, 16) == 1000, "off: exactly the target");
    NEAR(cam_follow_smooth_step_milli(1, 0, 1000, 60), 632, 3, "low one tau");
    NEAR(cam_follow_smooth_step_milli(2, 0, 1000, 160), 632, 3, "high one tau");
    NEAR(cam_follow_smooth_step_milli(1, 0, 1000, 16), 234, 3, "low 16ms: 1-e^-0.2667 = 23.4%");
    NEAR(cam_follow_smooth_step_milli(2, 0, 1000, 16), 95, 3, "high 16ms: 1-e^-0.1 = 9.5%");
    CHECK(cam_follow_smooth_step_milli(2, 40, 1000, 16) < cam_follow_smooth_step_milli(1, 40, 1000, 16), "high lags more than low");
    CHECK(cam_follow_smooth_step_milli(2, 40, 1000, 0) == 40, "dt 0 holds");
}

static void test_click_move(void) {
    /* accept: enabled, zoomed out, not on UI, ray hit, walkable */
    CHECK(click_move_accept(1, 0, 0, 1, 1000) == 1, "accept flat ground");
    CHECK(click_move_accept(0, 0, 0, 1, 1000) == 0, "option off");
    CHECK(click_move_accept(1, 1, 0, 1, 1000) == 0, "first person");
    CHECK(click_move_accept(1, 0, 1, 1, 1000) == 0, "UI captured the click");
    CHECK(click_move_accept(1, 0, 0, 0, 1000) == 0, "no ray hit");
    CHECK(click_move_accept(1, 0, 0, 1, 299) == 0 && click_move_accept(1, 0, 0, 1, 300) == 1, "wall vs ground at normal.y 0.3");
    /* arrived: radius 400 -> r^2 = 160000 */
    CHECK(click_move_arrived(300, 0, 400) == 1 && click_move_arrived(-300, 0, 400) == 1, "inside");
    CHECK(click_move_arrived(400, 0, 400) == 1 && click_move_arrived(401, 0, 400) == 0, "edge: 160000 <= 160000, 401 out");
    CHECK(click_move_arrived(300, 300, 400) == 0, "diagonal 424 > 400 (180000 > 160000)");
    CHECK(click_move_arrived(282, -282, 400) == 1, "282,282 = 159048 <= 160000");
    CHECK(click_move_arrived(0, 0, 400) == 1 && click_move_arrived(0, 0, 0) == 1, "on the spot");
    CHECK(click_move_arrived(2000000, 2000000, 400) == 0 && click_move_arrived(1, 1, -5) == 0, "huge delta / negative radius, no overflow");
    CHECK(click_move_arrived(40000, 40000, 2000000) == 0 && click_move_arrived(20000, 20000, 2000000) == 1, "radius clamped to 30000: 28284 in, 56568 out");
    /* cancel */
    CHECK(click_move_should_cancel(1, 0, 0, 0) == 0, "nothing cancels");
    CHECK(click_move_should_cancel(0, 0, 0, 0) == 1, "option turned off");
    CHECK(click_move_should_cancel(1, 1, 0, 0) == 1, "entered first person");
    CHECK(click_move_should_cancel(1, 0, 1, 0) == 1, "WASD");
    CHECK(click_move_should_cancel(1, 0, 0, 1499) == 0 && click_move_should_cancel(1, 0, 0, 1500) == 1, "stuck timeout 1500");
    /* stuck timer: under 200 milli/s counts: 3 milli in 16 ms = 187/s stuck; 4 milli = 250/s resets */
    CHECK(click_move_stuck_update(100, 16, 3) == 116 && click_move_stuck_update(100, 16, 4) == 0, "stuck threshold 0.2 u/s");
    CHECK(click_move_stuck_update(100, 16, 0) == 116, "no movement accumulates");
    CHECK(click_move_stuck_update(100, 0, 0) == 100 && click_move_stuck_update(100, -3, 0) == 100, "dt<=0 holds");
    CHECK(click_move_stuck_update(59990, 100, 0) == 60000, "capped");
    { int st = 0, n = 0; while (!click_move_should_cancel(1, 0, 0, st)) { st = click_move_stuck_update(st, 500, 0); n++; } CHECK(n == 3 && st == 1500, "wall-stuck cancels after 3 x 500 ms"); }
    /* isqrt */
    CHECK(click_move_isqrt(0) == 0 && click_move_isqrt(-9) == 0 && click_move_isqrt(1) == 1 && click_move_isqrt(3) == 1, "isqrt small");
    CHECK(click_move_isqrt(4) == 2 && click_move_isqrt(99) == 9 && click_move_isqrt(100) == 10, "isqrt exact/floor");
    CHECK(click_move_isqrt(2147395600) == 46340 && click_move_isqrt(2147483647) == 46340, "isqrt top of range (46340^2 = 2147395600)");
    /* steering. scale: <=400 -> 0, >=1500 -> 1000, 900 -> 350 + 650*500/1100 = 645 */
    CHECK(click_move_steer_scale_permille(400) == 0 && click_move_steer_scale_permille(401) > 0, "scale arrive edge");
    CHECK(click_move_steer_scale_permille(1500) == 1000 && click_move_steer_scale_permille(99999) == 1000, "scale far");
    CHECK(click_move_steer_scale_permille(900) == 645, "scale mid 645");
    CHECK(click_move_steer_fwd_permille(5000, 0) == 1000 && click_move_steer_right_permille(5000, 0) == 0, "straight ahead");
    CHECK(click_move_steer_fwd_permille(-5000, 0) == -1000, "straight behind");
    CHECK(click_move_steer_right_permille(0, 5000) == 1000 && click_move_steer_fwd_permille(0, 5000) == 0, "right");
    CHECK(click_move_steer_right_permille(0, -5000) == -1000, "left");
    /* diagonal: a=b=1000, len=1414, 1000*1000/1414 = 707 */
    CHECK(click_move_steer_fwd_permille(5000, 5000) == 707 && click_move_steer_right_permille(5000, 5000) == 707, "diagonal 707 each");
    CHECK(click_move_steer_fwd_permille(900, 0) == 645, "decelerates near goal: 645");
    CHECK(click_move_steer_fwd_permille(300, 0) == 0 && click_move_steer_right_permille(0, 300) == 0, "inside arrive: no push");
    CHECK(click_move_steer_fwd_permille(0, 0) == 0 && click_move_steer_right_permille(0, 0) == 0, "zero vector");
    CHECK(click_move_steer_fwd_permille(1000000, 0) == 1000, "far: full stick, no overflow");
    CHECK(click_move_steer_fwd_permille(2000000000, 0) == 1000 && click_move_steer_fwd_permille(-2000000000, 7) == -1000, "clamped huge input");
    /* sweep: the stick vector never exceeds full deflection (+1%), and points at the goal when far */
    int worst = 0;
    for (int lf = -9000; lf <= 9000; lf += 600) for (int lr = -9000; lr <= 9000; lr += 600) {
        int f = click_move_steer_fwd_permille(lf, lr), r = click_move_steer_right_permille(lf, lr);
        int mag2 = f * f + r * r; if (mag2 > worst) worst = mag2;
        if (lf * lf + lr * lr > 4000 * 4000 && lf != 0) CHECK((f > 0) == (lf > 0) || f == 0, "stick points toward the goal (forward sign)");
    }
    CHECK(worst <= 1010 * 1010, "stick magnitude <= 1.01");
    /* ahead targets (REDGARDEN first person) */
    CHECK(click_move_ahead_active(0, 0) == 0 && click_move_ahead_active(100, 0) == 0 && click_move_ahead_active(101, 0) == 1, "ahead dead zone 0.1");
    CHECK(click_move_ahead_active(-700, 700) == 1, "ahead diagonal");
    CHECK(click_move_ahead_milli(10000, 1000) == 13000 && click_move_ahead_milli(10000, -500) == 8500 && click_move_ahead_milli(10000, 0) == 10000, "ahead = hero + dir * 3 units");
}

int main(void) {
    test_ease(); test_zoom(); test_follow(); test_click_move();
    printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "ok", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
