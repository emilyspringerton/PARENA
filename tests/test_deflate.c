/* tests/test_deflate.c -- stdlib/compress/deflate.prn: zlib streams that this stdlib's own inflate (and, through
 * --dump, Python's zlib) decode back to the input: empty, tiny, text, runs, a long repeat, every byte value, a
 * screenshot-like image (flat colour with rows repeating), and pseudo-random bytes that cannot compress. */
#include "parena_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_deflate_gen.c"
static int failures = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); failures++; } else printf("PASS: %s\n", m); } while (0)

static FILE *dump = NULL;

/* Compress, strip the zlib wrapper, inflate, compare. Returns the compressed size (-1 on a mismatch). */
static int roundtrip(Arena *a, const unsigned char *src, int n, const char *name) {
    Bytes in = bytes_alloc_impl(a, n);
    for (int i = 0; i < n; i++) bytes_set_impl(in, i, src[i]);
    Bytes out = bytes_alloc_impl(a, zlib_bound(n));
    int z = zlib_compress(in, n, out, a);
    if (dump) { fprintf(dump, "%s %d %d\n", name, n, z); fwrite(out.data, 1, (size_t)z, dump); fputc('\n', dump); }
    Bytes payload = bytes_alloc_impl(a, z - 6);
    for (int i = 0; i < z - 6; i++) bytes_set_impl(payload, i, bytes_get_impl(out, i + 2));
    Bytes back = bytes_alloc_impl(a, n > 0 ? n : 1);
    int got = inflate(payload, back, a);
    if (got != n) { printf("  %s: inflated %d bytes, want %d\n", name, got, n); return -1; }
    for (int i = 0; i < n; i++) if (bytes_get_impl(back, i) != src[i]) { printf("  %s: byte %d differs\n", name, i); return -1; }
    unsigned int ad = (unsigned int)adler32(in, n);
    unsigned int tr = ((unsigned)bytes_get_impl(out, z - 4) << 24) | ((unsigned)bytes_get_impl(out, z - 3) << 16) |
                      ((unsigned)bytes_get_impl(out, z - 2) << 8) | (unsigned)bytes_get_impl(out, z - 1);
    if (ad != tr) { printf("  %s: adler trailer wrong\n", name); return -1; }
    if (z > zlib_bound(n)) { printf("  %s: wrote past the bound\n", name); return -1; }
    return z;
}

int main(int argc, char **argv) {
    if (argc > 1) dump = fopen(argv[1], "wb");
    Arena a; arena_init(&a);
    static unsigned char buf[2000000];
    int z;

    CHECK(roundtrip(&a, buf, 0, "empty") >= 0, "empty input round-trips");
    buf[0] = 'x';
    CHECK(roundtrip(&a, buf, 1, "one") >= 0, "one byte round-trips");
    const char *text = "the quick brown fox jumps over the lazy dog; the quick brown fox jumps over the lazy dog again";
    CHECK(roundtrip(&a, (const unsigned char *)text, (int)strlen(text), "text") >= 0, "text with a repeat round-trips");
    memset(buf, 0, 100000);
    z = roundtrip(&a, buf, 100000, "zeros");
    CHECK(z > 0 && z < 2000, "100000 zero bytes round-trip and shrink below 2000 bytes");
    for (int i = 0; i < 256 * 4; i++) buf[i] = (unsigned char)(i & 255);
    CHECK(roundtrip(&a, buf, 256 * 4, "bytes") >= 0, "every byte value round-trips (literal codes in all four bands)");
    for (int i = 0; i < 70000; i++) buf[i] = (unsigned char)("abcdefghij"[i % 10]);
    CHECK(roundtrip(&a, buf, 70000, "period10") > 0, "a 10-byte period over 70000 bytes round-trips (long matches)");
    /* screenshot-like: 400x300 RGB, a grey desktop with a white window and a few coloured rows, PNG-style row bytes */
    int w = 400, h = 300, stride = w * 3 + 1, sn = stride * h;
    for (int y = 0; y < h; y++) {
        buf[y * stride] = 0;
        for (int x = 0; x < w; x++) {
            unsigned char *p = &buf[y * stride + 1 + x * 3];
            int inwin = x > 50 && x < 350 && y > 40 && y < 260;
            p[0] = inwin ? 255 : 58; p[1] = inwin ? 255 : 60; p[2] = inwin ? 255 : 66;
            if (inwin && y % 20 == 0 && x < 300) { p[0] = 30; p[1] = 120; p[2] = 200; }
        }
    }
    z = roundtrip(&a, buf, sn, "screen");
    CHECK(z > 0 && z * 20 < sn, "a screenshot-like image round-trips and shrinks over 20x");
    unsigned int s = 12345;
    for (int i = 0; i < 50000; i++) { s = s * 1103515245u + 12345u; buf[i] = (unsigned char)(s >> 16); }
    z = roundtrip(&a, buf, 50000, "random");
    CHECK(z > 0, "50000 pseudo-random bytes round-trip (and stay within the bound)");
    /* distances up to 32 KiB: a 1000-byte block repeated 33 KB later */
    for (int i = 0; i < 40000; i++) { s = s * 1103515245u + 12345u; buf[i] = (unsigned char)(s >> 16); }
    memcpy(buf + 33000, buf + 1000, 1000);
    CHECK(roundtrip(&a, buf, 40000, "far") > 0, "a repeat 32000 bytes back round-trips");

    if (dump) fclose(dump);
    printf("%s\n", failures ? "deflate: FAILED" : "deflate: all passed");
    return failures ? 1 : 0;
}
