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
    printf(failures ? "FAILED\n" : "ALL PASS\n");
    return failures != 0;
}
