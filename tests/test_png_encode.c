/* tests/test_png_encode.c -- stdlib/image/png_encode.prn: images written as PNG decode back to the same pixels with
 * this stdlib's own image/png.prn decoder (and, through --dump, with Pillow): BGRA screen pixels to RGB and to RGBA,
 * plain RGB input, a 1x1, every filter exercised, and a 1920x1080 screen-sized capture. */
#include "parena_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "test_png_encode_gen.c"
static int failures = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); failures++; } else printf("PASS: %s\n", m); } while (0)

static const char *dumpdir = NULL;
static void save(Bytes png, const char *name) {
    if (!dumpdir) return;
    char path[512]; snprintf(path, sizeof path, "%s/%s", dumpdir, name);
    FILE *f = fopen(path, "wb"); fwrite(png.data, 1, (size_t)png.len, f); fclose(f);
}

/* a test pattern: gradients, a flat block, noise in one corner -- something for every filter to win on */
static unsigned char pat(int x, int y, int c) {
    if (x < 8 && y < 8) return (unsigned char)((x * 37 + y * 91 + c * 53) * 2654435761u >> 24);
    if (y > 20) return (unsigned char)(c == 0 ? 200 : c == 1 ? 40 : 90);
    return (unsigned char)(c == 0 ? x * 9 : c == 1 ? y * 11 : (x + y) * 5);
}

/* encode w x h from a source with sc channels (bgr order if bgr), decode, compare RGB(A) */
static int roundtrip(Arena *a, int w, int h, int sc, int bgr, int alpha, const char *name) {
    Bytes px = bytes_alloc_impl(a, w * h * sc);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) for (int c = 0; c < sc; c++) {
        int logical = (bgr && c < 3) ? 2 - c : c;              /* the channel this byte holds */
        bytes_set_impl(px, (y * w + x) * sc + c, logical == 3 ? (x * 7 + y) & 255 : pat(x, y, logical));
    }
    Bytes png = png_encode(px, w, h, sc, bgr, alpha, a);
    save(png, name);
    PngImage img = png_decode(png, a);
    if (!img.ok || img.width != w || img.height != h) { printf("  %s: did not decode\n", name); return 0; }
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) for (int c = 0; c < 4; c++) {
        int want = c == 3 ? (alpha ? ((x * 7 + y) & 255) : 255) : pat(x, y, c);
        if (bytes_get_impl(img.pixels, (y * w + x) * 4 + c) != want) { printf("  %s: (%d,%d) channel %d differs\n", name, x, y, c); return 0; }
    }
    return png.len;
}

int main(int argc, char **argv) {
    if (argc > 1) dumpdir = argv[1];
    Arena a; arena_init(&a);
    CHECK(roundtrip(&a, 1, 1, 4, 1, 0, "one.png"), "a 1x1 BGRA pixel round-trips");
    CHECK(roundtrip(&a, 40, 30, 4, 1, 0, "bgra_to_rgb.png"), "BGRA screen pixels -> RGB PNG round-trip");
    CHECK(roundtrip(&a, 40, 30, 4, 1, 1, "bgra_to_rgba.png"), "BGRA -> RGBA PNG keeps alpha");
    CHECK(roundtrip(&a, 37, 23, 3, 0, 0, "rgb.png"), "plain RGB input (odd size) round-trips");
    unsigned char crc_in[] = { 'I', 'E', 'N', 'D' };
    Bytes cb = bytes_alloc_impl(&a, 4); for (int i = 0; i < 4; i++) bytes_set_impl(cb, i, crc_in[i]);
    CHECK((unsigned)crc32(cb, 0, 4) == 0xAE426082u, "CRC-32 of \"IEND\" is the well-known AE426082");
    /* screen-sized: a desktop with a window, title bar and text-like strokes */
    int w = 1920, h = 1080;
    Bytes scr = bytes_alloc_impl(&a, w * h * 4);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int inwin = x > 300 && x < 1500 && y > 150 && y < 900, title = inwin && y < 180;
        int text = inwin && !title && (y % 18) < 12 && ((x / 6 + y / 18) % 7) < 5 && ((x * 13 + y * 7) % 5) < 2;
        unsigned char r = title ? 45 : inwin ? (text ? 20 : 250) : (unsigned char)(30 + y / 20);
        unsigned char g = title ? 45 : inwin ? (text ? 20 : 250) : (unsigned char)(60 + y / 30);
        unsigned char b = title ? 48 : inwin ? (text ? 24 : 252) : (unsigned char)(110 + y / 25);
        int i = (y * w + x) * 4;
        bytes_set_impl(scr, i, b); bytes_set_impl(scr, i + 1, g); bytes_set_impl(scr, i + 2, r); bytes_set_impl(scr, i + 3, 255);
    }
    clock_t t0 = clock();
    Bytes png = png_encode(scr, w, h, 4, 1, 0, &a);
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    save(png, "screen.png");
    PngImage img = png_decode(png, &a);
    int same = img.ok && img.width == w && img.height == h;
    for (int i = 0; same && i < w * h; i++)
        if (bytes_get_impl(img.pixels, i * 4) != bytes_get_impl(scr, i * 4 + 2) || bytes_get_impl(img.pixels, i * 4 + 2) != bytes_get_impl(scr, i * 4)) same = 0;
    printf("  1920x1080: %d bytes (raw %d), %.2f s\n", png.len, w * h * 3, secs);
    CHECK(same, "a 1920x1080 screen capture round-trips");
    CHECK(png.len < w * h * 3 / 10, "and compresses below a tenth of its raw size");
    printf("%s\n", failures ? "png_encode: FAILED" : "png_encode: all passed");
    return failures ? 1 : 0;
}
