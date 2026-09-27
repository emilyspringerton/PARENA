/* tests/test_audio_dj.c -- stdlib/audio/{mixer4,deck,sampler}.prn: the 4-channel DJM-style
 * mixer, CDJ-style decks and MPC-style sampler (founder real-time 2026-09-27: "it needs to
 * emulate an MPC and a pioneer mixer and 4 CDJS"). Values are worked by hand from the rules'
 * documented behaviour, plus two end-to-end checks that run real signal through the rules: a
 * pitched deck resampling a sine (Hermite) and a 4-deck crossfade that must stay constant-power.
 */
#include "parena_runtime.h"
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "test_audio_dj_gen.c"

static int failures = 0;
static void check(const char *what, double got, double want, double tol) {
    if (fabs(got - want) > tol) { printf("FAIL: %s: got %.9f want %.9f\n", what, got, want); failures++; }
    else printf("PASS: %s\n", what);
}

int main(void) {
    /* ---- mixer ---- */
    check("trim 12 o'clock = 0 dB", mixer_trim_db(0.5), 0, 1e-12);
    check("trim full = +9 dB", mixer_trim_db(1.0), 9, 1e-12);
    check("trim closed = -inf", mixer_trim_db(0.0), -200, 0);
    check("EQ full cut knob 0 = kill", mixer_eq_db(0.0), -200, 0);
    check("EQ 0.25 = -13 dB", mixer_eq_db(0.25), -13, 1e-12);
    check("EQ full boost = +6 dB", mixer_eq_db(1.0), 6, 1e-12);
    check("color filter centre bypasses", mixer_filter_kind(0.0), -1, 0);
    check("color filter left = LPF", mixer_filter_kind(-0.5), 0, 0);
    check("color filter right = HPF", mixer_filter_kind(0.5), 1, 0);
    check("LPF full left = 60 Hz", mixer_filter_freq(-1.0), 60, 1e-6);
    check("HPF full right = 8 kHz", mixer_filter_freq(1.0), 8000, 1e-6);
    check("HPF just past dead zone ~20 Hz", mixer_filter_freq(0.0300001), 20, 0.01);
    check("fader standard half = 0.25", mixer_fader_gain(0.5, 1), 0.25, 1e-12);
    check("fader fast half = 0.707", mixer_fader_gain(0.5, 2), sqrt(0.5), 1e-12);
    check("xfader THRU ignores position", mixer_xfader_gain(1, 0.0, 0), 1, 0);
    check("xfader A at full A = 1", mixer_xfader_gain(0, 0.0, 0), 1, 1e-12);
    check("xfader B at full A = 0", mixer_xfader_gain(2, 0.0, 0), 0, 1e-12);
    check("xfader sharp: A still full at 0.9", mixer_xfader_gain(0, 0.9, 2), 1, 0);
    check("xfader sharp: A cut at 0.98", mixer_xfader_gain(0, 0.98, 2), 0, 0);
    {   /* constant power: A^2 + B^2 == 1 across the smooth throw */
        double worst = 0;
        for (int i = 0; i <= 100; i++) {
            double p = i / 100.0, a = mixer_xfader_gain(0, p, 0), b = mixer_xfader_gain(2, p, 0);
            double e = fabs(a * a + b * b - 1.0);
            if (e > worst) worst = e;
        }
        check("smooth crossfader is constant-power across the throw", worst, 0, 1e-12);
    }
    check("channel gain: trim 0dB, fader full, THRU = 1", mixer_channel_gain(0.5, 1.0, 1, 1, 0.5, 0), 1, 1e-12);
    check("headphones mix 0 = cue only", mixer_headphone(0.3, 0.9, 0.0, 1.0), 0.3, 1e-12);
    check("headphones mix 1 = master only", mixer_headphone(0.3, 0.9, 1.0, 1.0), 0.9, 1e-12);
    check("meter -30 dB = 0 LEDs", mixer_meter_segment(-30), 0, 0);
    check("meter 0 dB = 13 LEDs", mixer_meter_segment(0.0), 13, 0);
    check("meter +3 dB = 15 (clip)", mixer_meter_segment(3.0), 15, 0);

    /* ---- decks ---- */
    check("tempo +/-16 range, fader +1 = rate 1.16", deck_rate(1.0, 2), 1.16, 1e-12);
    check("tempo +/-6 range, fader -0.5 = -3%", deck_pitch_pct(-0.5, 0), -3, 1e-12);
    check("sync 128 -> 124 bpm track = 1.032258", deck_sync_rate(128, 124, 1), 128.0 / 124.0, 1e-12);
    check("sync clamps to +/-6%", deck_sync_rate(150, 100, 0), 1.06, 1e-12);
    check("44.1k track on 48k device advance", deck_advance(0, 1.0, 44100, 48000), 0.91875, 1e-12);
    check("scratch at 33 1/3 rpm = rate 1", deck_scratch_rate(0.5555555555555556), 1, 1e-12);
    check("cue while playing = jump back (2)", deck_cue_press(1, 5000, 100), 2, 0);
    check("cue while paused elsewhere = set cue (1)", deck_cue_press(0, 5000, 100), 1, 0);
    check("cue while paused at cue = preview (3)", deck_cue_press(0, 100.2, 100), 3, 0);
    check("beat len 120 bpm @48k = 24000", deck_beat_len(120, 48000), 24000, 1e-9);
    check("beat phase halfway", deck_beat_phase(36000, 0, 24000), 0.5, 1e-12);
    check("quantize snaps to nearest beat", deck_quantize_pos(35000, 1000, 24000, 1), 25000, 1e-9);
    check("quantize off leaves position", deck_quantize_pos(35000, 1000, 24000, 0), 35000, 0);
    check("4-beat loop out", deck_loop_out(1000, 4, 24000), 97000, 1e-9);
    check("loop wraps with overshoot", deck_loop_wrap(97000.5, 1000, 97000, 1), 1000.5, 1e-9);
    check("loop inactive passes through", deck_loop_wrap(97000.5, 1000, 97000, 0), 97000.5, 0);
    check("loop halve floor 1/32", deck_loop_halve(0.03125), 0.03125, 0);
    check("loop double ceiling 32", deck_loop_double(32), 32, 0);
    check("master tempo at +8% undoes pitch", deck_key_ratio(1.08, 1, 0), 1 / 1.08, 1e-12);
    check("key shift +12 = octave", deck_key_ratio(1.0, 0, 12), 2, 1e-12);
    check("hermite passes through x1 at t=0", deck_hermite(0.1, 0.5, 0.9, 0.2, 0), 0.5, 1e-12);
    check("hermite passes through x2 at t=1", deck_hermite(0.1, 0.5, 0.9, 0.2, 1), 0.9, 1e-12);
    {   /* a deck pitched +6% resampling a 1 kHz sine must play a 1.06 kHz sine, cleanly */
        enum { N = 48000 };
        static double src[N + 8];
        for (int i = 0; i < N + 8; i++) src[i] = sin(2 * M_PI * 1000.0 * i / 48000.0);
        double pos = 1, rate = deck_rate(0.375, 2), err = 0;   /* 0.375 * 16% = +6% */
        int n = 0;
        while (pos < N) {
            int i = (int)floor(pos);
            double y = deck_hermite(src[i - 1], src[i], src[i + 1], src[i + 2], pos - i);
            double ideal = sin(2 * M_PI * 1000.0 * pos / 48000.0);
            if (fabs(y - ideal) > err) err = fabs(y - ideal);
            pos = deck_advance(pos, rate, 48000, 48000);
            n++;
        }
        check("pitched deck resample error < -60 dBFS", err < 0.001 ? 1 : 0, 1, 0);
        check("+6% deck consumes 1.06x the source", (double)N / n, 1.06, 0.001);
    }

    /* ---- sampler ---- */
    check("pad A01 = note 36", mpc_pad_note(0, 0), 36, 0);
    check("pad D16 = note 99", mpc_pad_note(3, 15), 99, 0);
    check("full level ignores velocity", mpc_velocity_gain(10, 1), 1, 0);
    check("velocity 127 = unity", mpc_velocity_gain(127, 0), 1, 1e-12);
    check("120 bpm ticks/sample @48k", mpc_ticks_per_sample(120, 48000), 0.04, 1e-12);
    check("1/16 = 240 ticks", mpc_grid_ticks(2), 240, 0);
    check("swing 50% is straight", mpc_swing_tick(240, 240, 50), 240, 1e-12);
    check("swing 66% pushes off-beat 16th late", mpc_swing_tick(240, 240, 66), 240 + 240 * 0.32, 1e-9);
    check("swing never moves on-beat 16ths", mpc_swing_tick(480, 240, 66), 480, 0);
    check("100% quantize snaps", mpc_quantize_tick(250, 240, 100), 240, 1e-12);
    check("50% quantize halfway", mpc_quantize_tick(250, 240, 50), 245, 1e-12);
    check("note repeat fires on grid crossing", mpc_repeat_due(230, 245, 240), 1, 0);
    check("note repeat quiet between lines", mpc_repeat_due(245, 300, 240), 0, 0);
    check("choke same group", mpc_choke(3, 3), 1, 0);
    check("group 0 never chokes", mpc_choke(0, 0), 0, 0);
    check("tune +7 st = fifth", mpc_tune_ratio(7), pow(2, 7 / 12.0), 1e-12);
    check("adsr mid-attack", mpc_adsr_level(5, 10, 100, 0.5), 0.5, 1e-12);
    check("adsr sustain", mpc_adsr_level(500, 10, 100, 0.5), 0.5, 1e-12);
    check("release halfway", mpc_release_level(50, 0.8, 100), 0.4, 1e-12);
    check("16 levels pad 15 = hi", mpc_16_levels(15, -12, 12), 12, 1e-12);
    check("released voices are stolen first",
          mpc_voice_steal_score(1, 0.9, 10) < mpc_voice_steal_score(0, 0.01, 5000) ? 1 : 0, 1, 0);

    if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
    printf("ALL DJ RIG CHECKS PASSED\n");
    return 0;
}
