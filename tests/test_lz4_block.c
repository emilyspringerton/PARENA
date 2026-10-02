/* tests/test_lz4_block.c -- compress/lz4_block.prn (real LZ4 block format) vs liblz4.
 * kat.h holds liblz4-compressed vectors (tests/lz4_block/gen_kat.py): we must DECODE the reference's
 * output. Our own compressed output is dumped to $LZ4B_OUT/ours_<i>.bin and tests/lz4_block/
 * verify_ours.py has liblz4 decode it (the other direction). Also a truncation/bit-flip sweep under
 * ASan+UBSan: malformed input must yield zero-length or exactly out-len bytes, never crash. */
#include "parena_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_lz4_block_gen.c"
#include "lz4_block/kat.h"

static int failures = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); failures++; } else printf("PASS: %s\n", m); } while (0)

static Bytes mk(Arena *a, const unsigned char *p, int n) {
    Bytes b = bytes_alloc_impl(a, n);
    if (n) memcpy(b.data, p, (size_t)n);
    return b;
}

int main(void) {
    const char *outdir = getenv("LZ4B_OUT");
    char name[160], msg[160];
    int i, total_ours = 0, total_raw = 0;
    for (i = 0; i < N_KATS; i++) {
        Arena a; arena_init(&a);
        Bytes raw = mk(&a, kats[i].raw, kats[i].raw_len);
        Bytes ref = mk(&a, kats[i].lz4, kats[i].lz4_len);
        Bytes dec = lz4_decompress_block(ref, raw.len, &a);
        snprintf(msg, sizeof(msg), "kat %d: decodes liblz4 output (%d -> %d bytes)", i, ref.len, raw.len);
        CHECK(dec.len == raw.len && memcmp(dec.data, raw.data, (size_t)raw.len) == 0, msg);

        Bytes ours = lz4_compress_block(raw, &a);
        Bytes back = lz4_decompress_block(ours, raw.len, &a);
        snprintf(msg, sizeof(msg), "kat %d: own roundtrip (%d -> %d -> %d)", i, raw.len, ours.len, back.len);
        CHECK(ours.len > 0 && back.len == raw.len && memcmp(back.data, raw.data, (size_t)raw.len) == 0, msg);
        total_ours += ours.len; total_raw += raw.len;
        if (outdir) {
            FILE *f;
            snprintf(name, sizeof(name), "%s/ours_%d.bin", outdir, i);
            f = fopen(name, "wb");
            if (f) { fwrite(ours.data, 1, (size_t)ours.len, f); fclose(f); }
        }
        /* malformed sweep over the reference stream: every truncation + every single-byte flip */
        {
            int bad = 0, j;
            for (j = 0; j < ref.len && j < 400; j++) {
                Bytes t = mk(&a, kats[i].lz4, j);
                Bytes r = lz4_decompress_block(t, raw.len, &a);
                if (r.len != 0 && r.len != raw.len) bad++;
                t = mk(&a, kats[i].lz4, ref.len);
                t.data[j] ^= 0xA5;
                r = lz4_decompress_block(t, raw.len, &a);
                if (r.len != 0 && r.len != raw.len) bad++;
            }
            snprintf(msg, sizeof(msg), "kat %d: truncation+bitflip sweep never yields a wrong-sized result", i);
            CHECK(bad == 0, msg);
        }
        arena_free_all(&a);
    }
    {
        Arena a; arena_init(&a);
        unsigned char big[70000]; memset(big, 'q', sizeof(big));
        CHECK(lz4_compress_block(mk(&a, big, 70000), &a).len == 0, "input > 65535 -> zero-length");
        CHECK(lz4_compress_block(mk(&a, big, 0), &a).len == 0, "empty input -> zero-length");
        CHECK(lz4_decompress_block(mk(&a, kats[1].lz4, kats[1].lz4_len), 5, &a).len == 0, "out-len too small -> zero-length");
        CHECK(lz4_decompress_block(mk(&a, kats[1].lz4, kats[1].lz4_len), kats[1].raw_len + 7, &a).len == 0, "out-len too large -> zero-length");
        printf("INFO: ours %d bytes vs raw %d over all vectors\n", total_ours, total_raw);
        arena_free_all(&a);
    }
    printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
