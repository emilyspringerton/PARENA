/* test_mixforge_dsp.c -- C-target verification for stdlib/mixforge/mixer.prn + sampler.prn, the
 * MIXFORGE 4-track mixer / MIDI sampler DSP kernels. The same two files are compiled to WASM for
 * the browser (MIXFORGE/scripts/build_dsp_wasm.sh, checked there by web/dsp_test.mjs); this proves
 * the native C target computes the same numbers, against libm references. */
#include "parena_runtime.h"
#include <math.h>
#include <stdio.h>

#include "test_mixforge_dsp_gen.c"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) { g_pass++; printf("PASS: %s\n", msg); } \
                              else { g_fail++; printf("FAIL: %s\n", msg); } } while (0)

int main(void) {
    double max_err = 0.0;
    for (int i = 0; i <= 1000; i++) {
        double t = i / 1000.0;
        double e = fabs(quarter_cos(t) - cos(M_PI * t / 2.0));
        if (e > max_err) max_err = e;
    }
    CHECK(max_err < 3e-5, "quarter-cos within 3e-5 of cos(pi x/2)");
    CHECK(fabs(pan_left(0.0) - M_SQRT1_2) < 3e-5 && fabs(pan_right(0.0) - M_SQRT1_2) < 3e-5, "pan centre is equal power");
    CHECK(fader_gain(0.5) == 0.125, "fader taper 0.5 -> 0.125");
    CHECK(!channel_audible(1, 1, 1) && channel_audible(0, 1, 1) && !channel_audible(0, 0, 1), "mute beats solo; solo isolates");
    CHECK(xfade_gain(1, 0.0) == 1.0 && fabs(xfade_gain(0, 1.0)) < 3e-5, "crossfader THRU / A-side");
    CHECK(soft_clip(10.0) == 1.0 && soft_clip(0.0) == 0.0, "soft clip saturates");
    CHECK(midi_kind(0x93) == 9 && midi_channel(0x93) == 3, "MIDI status decode");
    CHECK(is_note_off(0x90, 0) && is_note_on(0x90, 1), "velocity-0 note-on is a note-off");
    CHECK(pitch_bend_value(127, 127) == 8191 && pitch_bend_value(0, 0) == -8192, "14-bit pitch bend");
    CHECK(pad_for_note(51, 36) == 15 && pad_for_note(52, 36) == -1, "16-pad map");
    double rel = 0.0;
    for (int n = 0; n <= 127; n++) {
        double e = fabs(note_rate(n, 60) / pow(2.0, (n - 60) / 12.0) - 1.0);
        if (e > rel) rel = e;
    }
    CHECK(rel < 1e-14, "note-rate exact 12-TET over 128 notes");
    CHECK(capture_frames(1.0, 120.0, 48000.0) == 24000.0, "capture-frames 1 beat @120 @48k");
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
