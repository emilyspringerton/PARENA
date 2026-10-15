/* tests/test_h3_wire.c -- net/h3_wire.prn. QPACK field sections in h3_vectors.h come from pylsqpack
 * (independent implementation, Huffman-coded); Huffman KAT is RFC 7541 C.4.1/C.6.1. */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "test_h3_wire_gen.c"
#include "h3_vectors.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } else { printf("PASS: %s\n", msg); } } while (0)

static Bytes mk(Arena *a, const void *p, int n) { Bytes b = bytes_alloc_impl(a, n); if (n) memcpy(b.data, p, (size_t)n); return b; }
static Bytes S(Arena *a, const char *s) { return mk(a, s, (int)strlen(s)); }

int main(void) {
    Arena a; arena_init(&a);

    /* prefix integers: RFC 7541 C.1 */
    CHECK(qpack_int_read(mk(&a, "\x0a", 1), 0, 5) == 10, "C.1.1: 10 in 5-bit prefix");
    CHECK(qpack_int_read(mk(&a, "\x1f\x9a\x0a", 3), 0, 5) == 1337, "C.1.2: 1337 in 5-bit prefix");
    CHECK(qpack_int_len(mk(&a, "\x1f\x9a\x0a", 3), 0, 5) == 3, "C.1.2 consumed 3 bytes");
    CHECK(qpack_int_read(mk(&a, "\x2a", 1), 0, 8) == 42, "C.1.3: 42 in 8-bit prefix");
    CHECK(qpack_int_read(mk(&a, "\x1f\x9a", 2), 0, 5) == -1, "truncated continuation -> -1");
    { Bytes o = bytes_alloc_impl(&a, 8);
      int e = qpack_put_int_(o, 0, 0xe0, 5, 1337);
      CHECK(e == 3 && o.data[0] == 0xff && o.data[1] == 0x9a && o.data[2] == 0x0a, "encode 1337 w/ flags matches RFC bytes");
      CHECK(qpack_put_int_(o, 0, 0, 5, 10) == 1 && o.data[0] == 10, "encode 10"); }

    /* Huffman: RFC 7541 C.4.1 "www.example.com" */
    { const unsigned char h[] = {0xf1,0xe3,0xc2,0xe5,0xf2,0x3a,0x6b,0xa0,0xab,0x90,0xf4,0xff};
      Bytes in = mk(&a, h, sizeof h), out = bytes_alloc_impl(&a, 64);
      int e = qpack_huffman_decode(in, 0, (int)sizeof h, out, 0);
      CHECK(e == 15 && memcmp(out.data, "www.example.com", 15) == 0, "Huffman C.4.1 www.example.com");
      const unsigned char h2[] = {0xa8,0xeb,0x10,0x64,0x9c,0xbf};
      e = qpack_huffman_decode(mk(&a, h2, sizeof h2), 0, (int)sizeof h2, out, 0);
      CHECK(e == 8 && memcmp(out.data, "no-cache", 8) == 0, "Huffman C.4.2 no-cache");
      /* padding must be all ones: flip last bit */
      unsigned char bad[sizeof h]; memcpy(bad, h, sizeof h); bad[sizeof h - 1] ^= 0x01;
      CHECK(qpack_huffman_decode(mk(&a, bad, sizeof bad), 0, (int)sizeof bad, out, 0) == -1, "bad padding rejected");
      const unsigned char eos[] = {0xff,0xff,0xff,0xff};
      CHECK(qpack_huffman_decode(mk(&a, eos, 4), 0, 4, out, 0) == -1, "EOS / long-pad rejected"); }

    /* pylsqpack-produced sections decode to the expected "name: value\n" text */
    for (unsigned i = 0; i < sizeof H3V / sizeof *H3V; i++) {
        Bytes blk = mk(&a, H3V[i].blk, H3V[i].n), out = bytes_alloc_impl(&a, 4096);
        int e = qpack_decode_section(blk, 0, H3V[i].n, out);
        char m[64]; snprintf(m, sizeof m, "pylsqpack section %u decodes identically", i);
        CHECK(e == (int)strlen(H3V[i].expect) && memcmp(out.data, H3V[i].expect, (size_t)e) == 0, m);
    }

    /* our request encoder: dump bytes for pylsqpack to decode independently + structural checks */
    { Bytes out = bytes_alloc_impl(&a, 512);
      int e = qpack_put_get_request_(out, 0, S(&a, "example.com"), S(&a, "/index.html"), S(&a, "user-agent"), S(&a, "pellegrino"));
      CHECK(e > 0, "request encoded");
      FILE *f = fopen("/tmp/claude-1000/h3_req.bin", "wb"); if (f) { fwrite(out.data, 1, (size_t)e, f); fclose(f); }
      Bytes dec = bytes_alloc_impl(&a, 1024);
      int d = qpack_decode_section(out, 0, e, dec);
      const char *want = ":method: GET\n:scheme: https\n:authority: example.com\n:path: /index.html\nuser-agent: pellegrino\n";
      CHECK(d == (int)strlen(want) && memcmp(dec.data, want, (size_t)d) == 0, "own encoder round-trips through own decoder"); }

    /* errors: dynamic refs / nonzero RIC / truncation / overflow */
    { Bytes dec = bytes_alloc_impl(&a, 256);
      CHECK(qpack_decode_section(mk(&a, "\x01\x00\xd1", 3), 0, 3, dec) == -1, "non-zero Required Insert Count -> -1");
      CHECK(qpack_decode_section(mk(&a, "\x00\x00\x81", 3), 0, 3, dec) == -1, "dynamic indexed ref -> -1");
      CHECK(qpack_decode_section(mk(&a, "\x00\x00\x10", 3), 0, 3, dec) == -1, "post-base indexed -> -1");
      CHECK(qpack_decode_section(mk(&a, "\x00\x00\xff\x7f", 4), 0, 4, dec) == -1, "static index out of range (>98) -> -1");
      CHECK(qpack_decode_section(mk(&a, "\x00\x00\x51\x0a" "abc", 7), 0, 7, dec) == -1, "literal value length past end -> -1");
      Bytes tiny = bytes_alloc_impl(&a, 4);
      CHECK(qpack_decode_section(mk(&a, "\x00\x00\xd1", 3), 0, 3, tiny) == -1, "output overflow -> -1"); }

    /* static table spot checks via decode of indexed lines */
    { Bytes dec = bytes_alloc_impl(&a, 256);
      int e = qpack_decode_section(mk(&a, "\x00\x00\xd1\xd7\xff\x23", 6), 0, 6, dec);  /* 17, 23, 98 */
      const char *want = ":method: GET\n:scheme: https\nx-frame-options: sameorigin\n";
      CHECK(e == (int)strlen(want) && memcmp(dec.data, want, (size_t)e) == 0, "static 17/23/98 = RFC 9204 Appendix A"); }
    CHECK(qpack_static_name_len(0) == 10 && qpack_static_name_len(95) == 10 && qpack_static_name_len(99) == -1, "static name lengths");

    /* HTTP/3 frames */
    { Bytes o = bytes_alloc_impl(&a, 64);
      int p = h3_put_settings_(o, 0);
      CHECK(p == 6 && h3_frame_type(o, 0) == 4 && h3_frame_len(o, 0) == 4 && h3_frame_payload_off(o, 0) == 2 && h3_frame_end(o, 0) == 6, "SETTINGS frame");
      Bytes d = S(&a, "payload");
      int q = h3_put_data_frame_(o, p, d);
      CHECK(q == p + 2 + 7 && h3_frame_type(o, p) == 0 && h3_frame_len(o, p) == 7 && h3_frame_end(o, p) == q, "DATA frame");
      Bytes t = bytes_slice(o, 0, q - 3, &a);
      CHECK(h3_frame_end(t, p) == -1, "truncated DATA frame -> -1");
      Bytes big = bytes_alloc_impl(&a, 8);
      CHECK(h3_put_frame_header_(big, 0, 1, 20000) == 5, "HEADERS len 20000 -> 4-byte varint");
      CHECK(h3_frame_len(big, 0) == 20000, "reads it back"); }

    /* fuzz: garbage sections never crash */
    srand(777);
    for (int it = 0; it < 4000; it++) {
        int n = rand() % 48;
        Bytes g = bytes_alloc_impl(&a, n);
        for (int i = 0; i < n; i++) g.data[i] = (unsigned char)rand();
        if (n > 2 && it % 2) { g.data[0] = 0; g.data[1] = 0; }
        Bytes out = bytes_alloc_impl(&a, 512);
        (void)qpack_decode_section(g, 0, n, out);
        (void)qpack_huffman_decode(g, 0, n, out, 0);
        (void)h3_frame_end(g, 0);
    }
    CHECK(1, "4000 random buffers: no crash/OOB");

    arena_free_all(&a);
    printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
