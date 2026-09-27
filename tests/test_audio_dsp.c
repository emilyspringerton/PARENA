/* tests/test_audio_dsp.c -- real end-to-end verification of stdlib/audio/dsp.prn (founder
 * real-time 2026-09-27: "nock and shankpit engine need sound engineering primatives"). Checks
 * the PARENA-compiled C against numbers that come from OUTSIDE this repo, not from re-deriving
 * the same formulas:
 *   - ITU-R BS.1770-4 Table 1/2: the published 48 kHz K-weighting coefficients.
 *   - Real frequency response: sines pushed sample-by-sample through the TDF-II step functions,
 *     steady-state RMS measured, compared with the filter's design intent (-3 dB at Butterworth
 *     cutoff, passband ~0 dB, notch/peak/shelf gains).
 *   - Dynamics curves: hard/soft knee compressor and expander values worked by hand.
 */
#include "parena_runtime.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846 /* strict -std=c99 hides it */
#endif

#include "test_audio_dsp_gen.c"

static int failures = 0;

static void check(const char *what, double got, double want, double tol) {
    if (fabs(got - want) > tol) {
        printf("FAIL: %s: got %.12f want %.12f (tol %g)\n", what, got, want, tol);
        failures++;
    } else {
        printf("PASS: %s (%.6f)\n", what, got);
    }
}

/* Steady-state gain (dB) of one biquad at `freq` Hz, measured by actually running a sine through
 * the PARENA step functions -- the same per-sample loop SHANKPIT/NOCK hosts run. */
static double measure_gain_db(int kind, double fc, double q, double gain, double sr, double freq) {
    double b0 = biquad_b0(kind, fc, q, gain, sr), b1 = biquad_b1(kind, fc, q, gain, sr);
    double b2 = biquad_b2(kind, fc, q, gain, sr), a1 = biquad_a1(kind, fc, q, gain, sr);
    double a2 = biquad_a2(kind, fc, q, gain, sr);
    double s1 = 0, s2 = 0, in_sq = 0, out_sq = 0;
    int n = (int)sr * 2, settle = (int)sr;
    for (int i = 0; i < n; i++) {
        double x = sin(2.0 * M_PI * freq * i / sr);
        double y = biquad_tdf2_y(b0, x, s1);
        double ns1 = biquad_tdf2_s1(b1, a1, x, y, s2);
        s2 = biquad_tdf2_s2(b2, a2, x, y);
        s1 = ns1;
        if (i >= settle) { in_sq += x * x; out_sq += y * y; }
    }
    return 10.0 * log10(out_sq / in_sq);
}

int main(void) {
    /* Units */
    check("db-to-linear(-6.0206) ~ 0.5", db_to_linear(-6.0205999), 0.5, 1e-6);
    check("db-to-linear(0) = 1", db_to_linear(0.0), 1.0, 1e-12);
    check("linear-to-db(0.1) = -20", linear_to_db(0.1), -20.0, 1e-9);
    check("linear-to-db(0) floors at -200 (no -Inf leak)", linear_to_db(0.0), -200.0, 0);
    check("linear-to-db(-0.5) uses magnitude", linear_to_db(-0.5), -6.0206, 1e-4);
    check("mix-dry-wet 0.8", mix_dry_wet(1.0, 0.0, 0.8), 0.2, 1e-12);
    check("time-coef(0 ms) is instant", time_coef(0.0, 48000.0), 1.0, 0);
    /* one-pole with coef from time-coef reaches 1-1/e of a step after exactly `ms` */
    {
        double c = time_coef(10.0, 48000.0), v = 0;
        for (int i = 0; i < 480; i++) v = one_pole_step(v, 1.0, c);
        check("time-coef(10ms): step reaches 63.2% after 10ms", v, 1.0 - exp(-1.0), 1e-3);
    }

    /* BS.1770-4 published 48 kHz K-weighting coefficients */
    check("K stage1 b0 @48k", kweight_shelf_b0(48000.0), 1.53512485958697, 1e-9);
    check("K stage1 b1 @48k", kweight_shelf_b1(48000.0), -2.69169618940638, 1e-9);
    check("K stage1 b2 @48k", kweight_shelf_b2(48000.0), 1.19839281085285, 1e-9);
    check("K stage1 a1 @48k", kweight_shelf_a1(48000.0), -1.69065929318241, 1e-9);
    check("K stage1 a2 @48k", kweight_shelf_a2(48000.0), 0.73248077421585, 1e-9);
    check("K stage2 a1 @48k", kweight_hp_a1(48000.0), -1.99004745483398, 1e-9);
    check("K stage2 a2 @48k", kweight_hp_a2(48000.0), 0.99007225036621, 1e-9);
    check("lufs-from-mean-square(1.0) = -0.691", lufs_from_mean_square(1.0), -0.691, 1e-12);
    if (lufs_absolute_gate(-70.5) != 0 || lufs_absolute_gate(-69.0) != 1) { printf("FAIL: absolute gate\n"); failures++; }
    if (lufs_relative_gate(-31.0, -20.0) != 0 || lufs_relative_gate(-29.0, -20.0) != 1) { printf("FAIL: relative gate\n"); failures++; }

    /* RBJ biquads, measured by running real sines through the TDF-II step */
    const double sr = 48000.0, bw = 0.7071067811865476;
    check("HPF 80Hz: -3dB at cutoff", measure_gain_db(1, 80, bw, 0, sr, 80), -3.0103, 0.05);
    check("HPF 80Hz: passband 1kHz ~0dB", measure_gain_db(1, 80, bw, 0, sr, 1000), 0.0, 0.05);
    check("HPF 80Hz: 20Hz rejected (~-24dB, 12dB/oct x2 oct)", measure_gain_db(1, 80, bw, 0, sr, 20), -24.1, 0.5);
    check("LPF 1kHz: -3dB at cutoff", measure_gain_db(0, 1000, bw, 0, sr, 1000), -3.0103, 0.05);
    check("LPF 1kHz: 100Hz passband", measure_gain_db(0, 1000, bw, 0, sr, 100), 0.0, 0.05);
    check("NOTCH 60Hz hum: 60Hz killed", measure_gain_db(3, 60, 10.0, 0, sr, 60) < -40 ? 1 : 0, 1, 0);
    check("NOTCH 60Hz: 1kHz untouched", measure_gain_db(3, 60, 10.0, 0, sr, 1000), 0.0, 0.05);
    check("PEAK +6dB @3kHz", measure_gain_db(4, 3000, 1.0, 6.0, sr, 3000), 6.0, 0.05);
    check("PEAK -9dB @3kHz", measure_gain_db(4, 3000, 1.0, -9.0, sr, 3000), -9.0, 0.05);
    check("HIGHSHELF +4dB: 16kHz gets ~+4dB", measure_gain_db(6, 2000, bw, 4.0, sr, 16000), 4.0, 0.2);
    check("HIGHSHELF +4dB: 100Hz untouched", measure_gain_db(6, 2000, bw, 4.0, sr, 100), 0.0, 0.05);
    check("LOWSHELF -6dB: 40Hz gets ~-6dB", measure_gain_db(5, 300, bw, -6.0, sr, 40), -6.0, 0.2);
    check("BANDPASS 0dB peak at centre", measure_gain_db(2, 5000, 2.0, 0, sr, 5000), 0.0, 0.05);

    /* Dynamics gain computers (worked by hand) */
    check("comp hard knee: below threshold = 0", comp_gain_db(-30, -20, 4, 0), 0.0, 1e-12);
    check("comp hard knee 4:1, 8dB over -> -6dB", comp_gain_db(-12, -20, 4, 0), -6.0, 1e-12);
    check("comp soft knee 6dB at threshold -> (1/4-1)*9/12", comp_gain_db(-20, -20, 4, 6), -0.5625, 1e-12);
    check("comp soft knee: above knee = hard", comp_gain_db(-12, -20, 4, 6), -6.0, 1e-12);
    check("expander 2:1, 10dB under -> -10dB", expander_gain_db(-50, -40, 2, 0, -27), -10.0, 1e-12);
    check("expander floors at range", expander_gain_db(-90, -40, 2, 0, -27), -27.0, 1e-12);
    check("expander above threshold = 0", expander_gain_db(-30, -40, 2, 0, -27), 0.0, 1e-12);
    check("limiter 3dB over -1 ceiling", limiter_gain_db(2.0, -1.0), -3.0, 1e-12);
    check("deess off at intensity 0", deess_gain_db(-10, -20, 0), 0.0, 0);
    check("deess 0.5 intensity, band 4dB over -> -2dB", deess_gain_db(-22, -20, 0.5), -2.0, 1e-12);
    check("loudnorm linear gain", loudnorm_gain_db(-18, -30, -10, -1.5), 8.5, 1e-12);
    check("loudnorm linear gain (unconstrained)", loudnorm_gain_db(-18, -24, -10, -1.5), 6.0, 1e-12);
    check("pcm16 roundtrip", pcm16_to_sample(16384), 0.5, 1e-12);
    check("pcm16 clamp", sample_to_pcm16_scale(2.0), 32767.0, 0);

    if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
    printf("ALL AUDIO DSP CHECKS PASSED\n");
    return 0;
}
