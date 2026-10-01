/* tests/test_wav.c -- stdlib/media/wav.prn round trip + byte-exact header vs a real Piper WAV. */
#include "parena_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_wav_gen.c"
static int failures = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); failures++; } else printf("PASS: %s\n", m); } while (0)
int main(int argc, char **argv) {
    Arena a; arena_init(&a);
    Bytes w = wav_alloc(4, 22050, &a);
    CHECK(bytes_len(w) == 52, "44-byte header + 4*2 data bytes");
    CHECK(wav_valid_(w), "fresh WAV is structurally valid");
    CHECK(wav_sample_rate(w) == 22050 && wav_sample_count(w) == 4, "rate/count read back");
    int vals[4] = {0, 1, -1, 32767};
    for (int i = 0; i < 4; i++) wav_set_sample_(w, i, vals[i]);
    int ok = 1; for (int i = 0; i < 4; i++) ok &= (wav_get_sample(w, i) == vals[i]);
    CHECK(ok, "samples 0,1,-1,32767 round-trip");
    wav_set_sample_(w, 0, -40000); wav_set_sample_(w, 1, 40000);
    CHECK(wav_get_sample(w, 0) == -32768 && wav_get_sample(w, 1) == 32767, "out-of-range clamps");
    /* byte-exact header comparison against a real Piper-written WAV (same rate/len shape) */
    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb"); unsigned char h[44]; CHECK(f && fread(h, 1, 44, f) == 44, "read reference header");
        if (f) { fseek(f, 0, SEEK_END); long len = ftell(f); fclose(f);
            int n = (int)((len - 44) / 2); Bytes r = wav_alloc(n, (int)(h[24] | h[25] << 8 | h[26] << 16), &a);
            int same = 1; for (int i = 0; i < 44; i++) same &= (bytes_get(r, i) == h[i]);
            CHECK(same, "header byte-identical to Piper's own WAV header"); }
    }
    printf(failures ? "FAILED\n" : "ALL PASS\n");
    return failures != 0;
}
