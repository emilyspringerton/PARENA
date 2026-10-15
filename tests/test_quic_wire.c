/* tests/test_quic_wire.c -- net/quic_wire.prn. Varint vectors are RFC 9000 Appendix A.1; the long
 * header vectors are the unprotected Initial headers from RFC 9001 Appendix A.2 / A.3. */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "test_quic_wire_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } else { printf("PASS: %s\n", msg); } } while (0)

static Bytes mk(Arena *a, const unsigned char *p, int n) {
    Bytes b = bytes_alloc_impl(a, n);
    if (n) memcpy(b.data, p, (size_t)n);
    return b;
}
static Bytes hex(Arena *a, const char *h) {
    int n = (int)strlen(h) / 2;
    Bytes b = bytes_alloc_impl(a, n);
    for (int i = 0; i < n; i++) { unsigned v; sscanf(h + 2 * i, "%2x", &v); b.data[i] = (unsigned char)v; }
    return b;
}

int main(void) {
    Arena a; arena_init(&a);

    /* varints, RFC 9000 A.1 */
    CHECK(quic_varint_read(hex(&a, "25"), 0) == 37, "1-byte 0x25 = 37");
    CHECK(quic_varint_read(hex(&a, "4025"), 0) == 37, "non-minimal 2-byte 0x4025 = 37");
    CHECK(quic_varint_read(hex(&a, "7bbd"), 0) == 15293, "2-byte 0x7bbd = 15293");
    CHECK(quic_varint_read(hex(&a, "9d7f3e7d"), 0) == 494878333, "4-byte 0x9d7f3e7d = 494878333");
    CHECK(quic_varint_read(hex(&a, "c2197c5eff14e88c"), 0) == -1, "8-byte 151288809941952652 exceeds I32 -> -1");
    CHECK(quic_varint_read(hex(&a, "c00000007fffffff"), 0) == 2147483647, "8-byte 2^31-1 decodes");
    CHECK(quic_varint_read(hex(&a, "9d7f3e"), 0) == -1, "truncated 4-byte -> -1");
    CHECK(quic_varint_read(hex(&a, ""), 0) == -1, "empty -> -1");
    CHECK(quic_varint_read(hex(&a, "25"), 1) == -1, "pos past end -> -1");

    /* round trip + minimal encoding sizes */
    int vals[] = {0, 1, 63, 64, 16383, 16384, 1073741823, 1073741824, 2147483647};
    int sizes[] = {1, 1, 1, 2, 2, 4, 4, 8, 8};
    for (unsigned i = 0; i < sizeof vals / sizeof *vals; i++) {
        Bytes o = bytes_alloc_impl(&a, 8);
        int e = quic_varint_put_(o, 0, vals[i]);
        char m[80]; snprintf(m, sizeof m, "varint %d encodes in %d bytes and round-trips", vals[i], sizes[i]);
        CHECK(e == sizes[i] && quic_varint_size(vals[i]) == sizes[i] && quic_varint_read(o, 0) == vals[i], m);
    }
    { Bytes o = bytes_alloc_impl(&a, 1);
      CHECK(quic_varint_put_(o, 0, 64) == -1, "put that would overflow -> -1");
      CHECK(quic_varint_put_(o, 0, -5) == -1, "negative put -> -1"); }
    { Bytes o = bytes_alloc_impl(&a, 8); quic_varint_put_(o, 0, 494878333);
      CHECK(o.data[0] == 0x9d && o.data[1] == 0x7f && o.data[2] == 0x3e && o.data[3] == 0x7d, "put matches RFC bytes 9d7f3e7d"); }

    /* RFC 9001 A.2 client Initial header (unprotected) */
    Bytes ci = hex(&a, "c300000001088394c8f03e5157080000449e00000002");
    CHECK(quic_pkt_long_(ci), "A.2 is a long header");
    CHECK(quic_long_type(ci) == 0, "A.2 type Initial");
    CHECK(quic_long_version(ci) == 1, "A.2 version 1");
    CHECK(quic_long_dcid_len(ci) == 8, "A.2 dcid len 8");
    CHECK(memcmp(ci.data + quic_long_dcid_off(ci), "\x83\x94\xc8\xf0\x3e\x51\x57\x08", 8) == 0, "A.2 dcid bytes");
    CHECK(quic_long_scid_len(ci) == 0, "A.2 scid len 0");
    CHECK(quic_initial_token_len(ci) == 0, "A.2 token len 0");
    CHECK(quic_long_length(ci) == 1182, "A.2 length 1182 (0x449e)");
    CHECK(quic_long_pn_off(ci) == 18, "A.2 pn offset 18");
    CHECK(quic_pkt_pn_len(ci.data[0]) == 4, "A.2 pn length 4");

    /* RFC 9001 A.3 server Initial header */
    Bytes si = hex(&a, "c1000000010008f067a5502a4262b5004075" "0001");
    CHECK(quic_long_dcid_len(si) == 0, "A.3 dcid len 0");
    CHECK(quic_long_scid_len(si) == 8, "A.3 scid len 8");
    CHECK(quic_long_scid_off(si) == 7, "A.3 scid off 7");
    CHECK(quic_long_length(si) == 117, "A.3 length 117 (0x4075)");
    CHECK(quic_long_pn_off(si) == 18, "A.3 pn off 18");

    /* long-header builder reproduces A.2's leading bytes */
    { Bytes dc = hex(&a, "8394c8f03e515708"), sc = bytes_alloc_impl(&a, 0);
      Bytes o = bytes_alloc_impl(&a, 64);
      int p = quic_put_long_header_(o, 0, 0xc3, 1, dc, sc);
      p = quic_varint_put_(o, p, 0);       /* token length */
      p = quic_varint_put_(o, p, 1182);    /* length */
      CHECK(p == 18 && memcmp(o.data, ci.data, 18) == 0, "builder matches RFC 9001 A.2 header bytes"); }
    { Bytes big = bytes_alloc_impl(&a, 64);
      CHECK(quic_put_long_header_(big, 0, 0xc0, 1, bytes_alloc_impl(&a, 21), bytes_alloc_impl(&a, 0)) == -1, "dcid > 20 rejected"); }
    CHECK(!quic_pkt_long_(hex(&a, "40")), "short header is not long");
    CHECK(quic_long_type(hex(&a, "40")) == -1, "short header has no long type");
    CHECK(quic_short_pn_off(8) == 9, "short pn offset");
    CHECK(quic_long_length_off(hex(&a, "f000000001000000")) == -1, "Retry has no length");

    /* frames: build then walk */
    {
        Bytes pk = bytes_alloc_impl(&a, 256);
        Bytes data = mk(&a, (const unsigned char *)"hello", 5);
        int p = 0;
        p = quic_put_crypto_(pk, p, 0, data);
        int f_stream = p;
        p = quic_put_stream_(pk, p, 4, 1000, data, 1);
        int f_ack = p;
        p = quic_put_ack_(pk, p, 7, 3, 2);
        int f_md = p;
        p = quic_put_max_data_(pk, p, 65536);
        int f_msd = p;
        p = quic_put_max_stream_data_(pk, p, 4, 999999);
        int f_ms = p;
        p = quic_put_max_streams_(pk, p, 1, 100);
        int f_ping = p;
        p = quic_put_ping_(pk, p);
        p = quic_put_handshake_done_(pk, p);
        int f_pad = p;
        p = quic_put_padding_(pk, p, 3);
        int f_cc = p;
        p = quic_put_connection_close_(pk, p, 10, 6, mk(&a, (const unsigned char *)"bye", 3));
        CHECK(p > 0, "frames built");
        Bytes b = bytes_slice(pk, 0, p, &a);
        CHECK(quic_frame_type(b, 0) == 6, "crypto type");
        CHECK(quic_crypto_offset(b, 0) == 0 && quic_crypto_data_len(b, 0) == 5, "crypto offset/len");
        CHECK(memcmp(b.data + quic_crypto_data_off(b, 0), "hello", 5) == 0, "crypto data");
        CHECK(quic_frame_end(b, 0) == f_stream, "crypto end");
        CHECK(quic_frame_type(b, f_stream) == 0x0f, "stream type with FIN");
        CHECK(quic_stream_id(b, f_stream) == 4 && quic_stream_offset(b, f_stream) == 1000, "stream id/offset");
        CHECK(quic_stream_data_len(b, f_stream) == 5 && quic_stream_fin_(b, f_stream), "stream len/fin");
        CHECK(memcmp(b.data + quic_stream_data_off(b, f_stream), "hello", 5) == 0, "stream data");
        CHECK(quic_frame_end(b, f_stream) == f_ack, "stream end");
        CHECK(quic_ack_largest(b, f_ack) == 7 && quic_ack_delay(b, f_ack) == 3, "ack largest/delay");
        CHECK(quic_ack_range_count(b, f_ack) == 0 && quic_ack_first_range(b, f_ack) == 2, "ack ranges");
        CHECK(quic_frame_end(b, f_ack) == f_md, "ack end");
        CHECK(quic_frame_end(b, f_md) == f_msd, "max_data end");
        CHECK(quic_frame_end(b, f_msd) == f_ms, "max_stream_data end");
        CHECK(quic_frame_end(b, f_ms) == f_ping, "max_streams end");
        CHECK(quic_frame_end(b, f_ping) == f_ping + 1, "ping end");
        CHECK(quic_frame_end(b, f_ping + 1) == f_pad, "handshake_done end");
        CHECK(quic_frame_end(b, f_pad) == f_pad + 1, "padding is one byte per call");
        CHECK(quic_frame_end(b, f_cc) == p, "connection_close end == packet end");
        /* walk to the end */
        int pos = 0, n = 0;
        while (pos >= 0 && pos < p) { pos = quic_frame_end(b, pos); n++; }
        CHECK(pos == p && n == 7 + 3 + 1 + 1, "whole packet walks cleanly (frames incl. 3 padding bytes)");
        /* every truncation must be -1, never an OOB read (ASan guards) */
        int bad = 0;
        for (int cut = 1; cut < f_stream; cut++) { Bytes t = bytes_slice(pk, 0, cut, &a); if (quic_frame_end(t, 0) != -1) bad++; }
        CHECK(bad == 0, "truncated CRYPTO frame always -1");
    }
    /* STREAM without LEN runs to end of packet */
    { Bytes s = hex(&a, "0804aabbcc");
      CHECK(quic_frame_end(s, 0) == 5 && quic_stream_data_len(s, 0) == 3 && quic_stream_offset(s, 0) == 0, "STREAM no-LEN/no-OFF"); }
    CHECK(quic_frame_end(hex(&a, "40ff"), 0) == -1, "unknown frame type -> -1");
    CHECK(quic_frame_end(hex(&a, "0600" "4005" "aa"), 0) == -1, "CRYPTO length past end -> -1");

    /* random garbage: never crash, never read OOB (run under ASan/UBSan) */
    srand(12345);
    for (int it = 0; it < 4000; it++) {
        int n = rand() % 40;
        Bytes g = bytes_alloc_impl(&a, n);
        for (int i = 0; i < n; i++) g.data[i] = (unsigned char)rand();
        int pos = 0, guard = 0;
        while (pos >= 0 && pos < n && guard++ < 64) pos = quic_frame_end(g, pos);
        (void)quic_long_length(g); (void)quic_long_pn_off(g); (void)quic_initial_token_off(g);
        (void)quic_stream_data_len(g, 0); (void)quic_ack_first_range(g, 0);
    }
    CHECK(1, "4000 random buffers: no crash/OOB");

    arena_free_all(&a);
    printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
