/* tests/test_tyler_voice.c -- verification for stdlib/tyler/voice_mod.prn. Clip/hold numbers are
 * the REAL Piper renders of VH01 (TYLER/tts/render_vh01.py) vs tyler_coldopen.c's real holds. */
#include "parena_runtime.h"
#include <stdio.h>
#include "test_tyler_voice_gen.c"
static int failures = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); failures++; } else printf("PASS: %s\n", m); } while (0)
int main(void) {
    CHECK(on_tyler_voice_clip_fits(2316, 3000), "beat0 tyler 2316/3000 fits");
    CHECK(!on_tyler_voice_clip_fits(3518, 3000), "beat0 hana 3518/3000 overruns");
    CHECK(on_tyler_voice_speedup_permille(2316, 3000) == 1000, "fitting clip needs no speedup");
    CHECK(on_tyler_voice_speedup_permille(3518, 3000) == 1172, "hana beat0 needs 1.172x");
    CHECK(on_tyler_voice_plan(2316, 3000, 1250) == 0, "plan: fits -> as-is");
    CHECK(on_tyler_voice_plan(3518, 3000, 1250) == 1, "plan: 1.172x within 1.25x cap -> speed up");
    CHECK(on_tyler_voice_plan(4336, 3000, 1250) == 2, "plan: 1.445x over cap -> stretch hold");
    CHECK(on_tyler_voice_plan(5904, 3000, 1250) == 2, "plan: beat4 1.968x -> stretch hold");
    CHECK(on_tyler_voice_stretched_hold_ms(5904, 3000, 500) == 6404, "stretched hold = clip+pad");
    CHECK(on_tyler_voice_stretched_hold_ms(1358, 2500, 500) == 2500, "stretch never shortens hold");
    CHECK(on_tyler_voice_required_hold_ms(2000, 300, 400) == 2700, "required = lead+clip+tail");
    /* ---- playback decisions (MODE_TYLER voice, wired into SHANKPIT's coordinator + audio callback) ---- */
    /* line-state: 0 pending / 1 playing / 2 over, at the real beat-0 geometry (Tyler 0..2305, Hana 2605..6274) */
    CHECK(on_tyler_voice_line_state(0, 0, 2305) == 1, "line-state: elapsed == offset starts the line");
    CHECK(on_tyler_voice_line_state(2304, 0, 2305) == 1, "line-state: last ms of a clip is still playing");
    CHECK(on_tyler_voice_line_state(2305, 0, 2305) == 2, "line-state: elapsed == offset+dur is over");
    CHECK(on_tyler_voice_line_state(2400, 2605, 3669) == 0, "line-state: Hana's line pending during the 300 ms gap");
    CHECK(on_tyler_voice_line_state(2605, 2605, 3669) == 1, "line-state: Hana's line starts at its offset");
    CHECK(on_tyler_voice_line_state(6274, 2605, 3669) == 2, "line-state: Hana's line over at beat-0 clip_ms 6274");
    CHECK(on_tyler_voice_line_state(5, 0, 0) == 2, "line-state: a zero-length clip is over immediately (never started)");
    /* seek */
    CHECK(on_tyler_voice_seek_ms(0, 0) == 0, "seek: on-time start seeks 0");
    CHECK(on_tyler_voice_seek_ms(16, 0) == 16, "seek: 16 ms late seeks 16");
    CHECK(on_tyler_voice_seek_ms(3000, 2605) == 395, "seek: late joiner 3000 ms into beat 0 seeks 395 into Hana's clip");
    CHECK(on_tyler_voice_seek_ms(100, 2605) == 0, "seek: before the offset never seeks negative");
    /* duck target */
    CHECK(on_tyler_duck_target_permille(1, 350) == 350, "duck target: a voice active -> duck level");
    CHECK(on_tyler_duck_target_permille(2, 350) == 350, "duck target: two voices -> same duck level");
    CHECK(on_tyler_duck_target_permille(0, 350) == 1000, "duck target: no voice -> unity");
    /* duck step: attack 50 ms, release 400 ms, 23 ms callback (512 frames @ 22050 Hz) */
    CHECK(on_tyler_duck_step_permille(1000, 350, 23, 50, 400) == 540, "duck step: attack drops 460 permille per 23 ms");
    CHECK(on_tyler_duck_step_permille(540, 350, 23, 50, 400) == 350, "duck step: attack lands exactly on the target, no undershoot");
    CHECK(on_tyler_duck_step_permille(350, 350, 23, 50, 400) == 350, "duck step: at target stays put");
    CHECK(on_tyler_duck_step_permille(350, 1000, 23, 50, 400) == 407, "duck step: release climbs 57 permille per 23 ms");
    CHECK(on_tyler_duck_step_permille(990, 1000, 23, 50, 400) == 1000, "duck step: release lands exactly on unity, no overshoot");
    {   /* a full release from the duck level reaches unity inside ~400 ms * 650/1000 + one block */
        int g = 350, ms = 0;
        while (g < 1000 && ms < 2000) { g = on_tyler_duck_step_permille(g, 1000, 23, 50, 400); ms += 23; }
        CHECK(g == 1000 && ms <= 276, "duck step: 350 -> 1000 release completes in <= 276 ms (took ms)");
    }
    {   /* attack from unity to the duck level completes inside 50 ms * 650/1000 + one block */
        int g = 1000, ms = 0;
        while (g > 350 && ms < 2000) { g = on_tyler_duck_step_permille(g, 350, 23, 50, 400); ms += 23; }
        CHECK(g == 350 && ms <= 46, "duck step: 1000 -> 350 attack completes in <= 46 ms");
    }
    printf(failures ? "FAILED\n" : "ALL PASS\n");
    return failures != 0;
}
