/* tests/test_png.c -- stdlib/image/png.prn: 8-bit RGB, RGBA, paletted (PLTE) and paletted+tRNS
 * PNGs (real ImageMagick-written fixtures, tests/test_png_fixtures.h) all decode to the same 3x2
 * RGBA image; unsupported shapes fail honestly instead of producing garbage. The paletted cases
 * are the 2026-10-01 addition (NOCK's ImageMagick pipeline palettizes <=256-colour textures). */
#include "parena_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_png_gen.c"
#include "test_png_fixtures.h"
static int failures = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); failures++; } else printf("PASS: %s\n", m); } while (0)

static Bytes load(Arena *a, const unsigned char *d, unsigned int n) {
    Bytes b = bytes_alloc_impl(a, (int)n);
    for (unsigned int i = 0; i < n; i++) bytes_set_impl(b, (int)i, d[i]);
    return b;
}
/* expected RGBA for the 3x2 fixture; pixel (0,1) is black, transparent only in the alpha fixtures */
static const unsigned char EXP[6][3] = {
    {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {0, 0, 0}, {128, 128, 128}, {255, 0, 0}};
static int matches(PngImage img, int alpha_fixture) {
    if (!img.ok || img.width != 3 || img.height != 2) return 0;
    for (int i = 0; i < 6; i++) {
        int a = (alpha_fixture && i == 3) ? 0 : 255;
        if (bytes_get_impl(img.pixels, i * 4) != EXP[i][0] || bytes_get_impl(img.pixels, i * 4 + 1) != EXP[i][1] ||
            bytes_get_impl(img.pixels, i * 4 + 2) != EXP[i][2] || bytes_get_impl(img.pixels, i * 4 + 3) != a) return 0;
    }
    return 1;
}
int main(void) {
    Arena a; arena_init(&a);
    CHECK(matches(png_decode(load(&a, fx_rgb, fx_rgb_len), &a), 0), "RGB (color type 2) decodes, alpha forced to 255");
    CHECK(matches(png_decode(load(&a, fx_rgba, fx_rgba_len), &a), 1), "RGBA (color type 6) decodes, alpha preserved");
    CHECK(matches(png_decode(load(&a, fx_pal, fx_pal_len), &a), 0), "paletted (color type 3, PLTE) decodes via palette lookup");
    CHECK(matches(png_decode(load(&a, fx_pal_a, fx_pal_a_len), &a), 1), "paletted + tRNS decodes with per-entry alpha");
    (void)png_decode(load(&a, fx_rgb, 20), &a); /* truncated: result unspecified, must just not crash */
    CHECK(1, "truncated input does not crash");
    unsigned char junk[16] = {0};
    CHECK(!png_decode(load(&a, junk, 16), &a).ok, "non-PNG bytes report ok=0");
    printf(failures ? "FAILED\n" : "ALL PASS\n");
    return failures != 0;
}
