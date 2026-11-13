/* tests/test_h2_wire.c -- net/h2_wire.prn: HTTP/2 frames + HPACK. The header-block vectors are RFC 7541
 * Appendix C (C.2 single fields, C.3 requests without Huffman, C.4 the same with Huffman, C.5 responses with
 * a 256-byte table that forces eviction) -- each sequence runs on ONE decoder table, as the RFC does, and the
 * table size after every block is checked against the RFC's own number. */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "test_h2_wire_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } else { printf("PASS: %s\n", msg); } } while (0)

static Bytes mk(Arena *a, const void *p, int n) { Bytes b = bytes_alloc_impl(a, n); if (n) memcpy(b.data, p, (size_t)n); return b; }
static Bytes S(Arena *a, const char *s) { return mk(a, s, (int)strlen(s)); }

/* hex string -> Bytes ("8286 8441" etc., spaces ignored) */
static Bytes H(Arena *a, const char *hex) {
    unsigned char buf[512]; int n = 0;
    for (const char *p = hex; *p; ) {
        if (*p == ' ') { p++; continue; }
        unsigned v; sscanf(p, "%2x", &v); buf[n++] = (unsigned char)v; p += 2;
    }
    return mk(a, buf, n);
}

static void block(Arena *a, Bytes dyn, const char *hex, const char *want, int want_size, const char *name) {
    Bytes in = H(a, hex), out = bytes_alloc_impl(a, 2048);
    int e = hpack_decode_block(in, 0, (int)in.len, dyn, out);
    char m[160];
    snprintf(m, sizeof m, "%s decodes", name);
    CHECK(e == (int)strlen(want) && memcmp(out.data, want, (size_t)e) == 0, m);
    if (!(e == (int)strlen(want) && memcmp(out.data, want, (size_t)e) == 0) && e > 0) printf("  got: %.*s\n", e, out.data);
    snprintf(m, sizeof m, "%s leaves the table at %d", name, want_size);
    CHECK(hpack_dyn_size(dyn) == want_size, m);
    if (hpack_dyn_size(dyn) != want_size) printf("  size: %d\n", hpack_dyn_size(dyn));
}

int main(void) {
    Arena a; arena_init(&a);
    Bytes dyn = bytes_alloc_impl(&a, 12 + 4096);

    /* ---- frames ---- */
    { Bytes o = bytes_alloc_impl(&a, 128);
      int e = h2_put_frame_header_(o, 0, 0x123456, 1, 5, 7);
      CHECK(e == 9 && o.data[0] == 0x12 && o.data[1] == 0x34 && o.data[2] == 0x56 && o.data[3] == 1 && o.data[4] == 5 &&
            o.data[8] == 7, "frame header bytes");
      CHECK(h2_frame_len(o, 0) == 0x123456 && h2_frame_type(o, 0) == 1 && h2_frame_flags(o, 0) == 5 && h2_frame_stream(o, 0) == 7,
            "frame header read back");
      CHECK(h2_frame_end(o, 0) == -1, "a frame whose payload has not arrived has no end");
      CHECK(h2_frame_len(o, 120) == -1, "a truncated header reads as -1");
      CHECK(h2_put_frame_header_(o, 0, 16777216, 0, 0, 1) == -1, "a 2^24 length is refused");
      e = h2_put_preface_(o, 0);
      CHECK(e == 24 && memcmp(o.data, "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", 24) == 0, "connection preface");
      e = h2_put_settings_(o, 0, 1048576);
      CHECK(e == 21 && h2_frame_type(o, 0) == 4 && h2_settings_count(o, 0) == 2, "SETTINGS: two settings");
      CHECK(h2_settings_id(o, 0, 0) == 2 && h2_settings_value(o, 0, 0) == 0, "ENABLE_PUSH = 0");
      CHECK(h2_settings_id(o, 0, 1) == 4 && h2_settings_value(o, 0, 1) == 1048576, "INITIAL_WINDOW_SIZE = 1 MiB");
      e = h2_put_settings_ack_(o, 0);
      CHECK(e == 9 && h2_frame_flags(o, 0) == 1 && h2_frame_len(o, 0) == 0, "SETTINGS ACK is empty with ACK");
      e = h2_put_window_update_(o, 0, 0, 65535);
      CHECK(e == 13 && h2_frame_type(o, 0) == 8 && o.data[12] == 0xff && o.data[11] == 0xff, "WINDOW_UPDATE");
      CHECK(h2_put_window_update_(o, 0, 0, 0) == -1, "a zero window increment is refused");
      e = h2_put_goaway_(o, 0, 5, 0);
      CHECK(e == 17 && h2_frame_type(o, 0) == 7 && o.data[12] == 5, "GOAWAY last stream 5");
      e = h2_put_rst_stream_(o, 0, 3, 8);
      CHECK(e == 13 && h2_frame_stream(o, 0) == 3 && o.data[12] == 8, "RST_STREAM CANCEL"); }

    { Bytes ping = H(&a, "000008 06 00 00000000 0102030405060708"), o = bytes_alloc_impl(&a, 32);
      int e = h2_put_ping_ack_(o, 0, ping, 0);
      CHECK(e == 17 && h2_frame_flags(o, 0) == 1 && memcmp(o.data + 9, ping.data + 9, 8) == 0, "PING ACK echoes 8 bytes"); }

    /* padded + priority HEADERS: pad length 2, 5 priority bytes, 3 block bytes, 2 pad bytes */
    { Bytes f = H(&a, "00000b 01 2c 00000001 02 8000000010 828487 0000");
      CHECK(h2_frame_end(f, 0) == 20, "padded HEADERS frame end");
      CHECK(h2_payload_start(f, 0) == 15 && h2_payload_end(f, 0) == 18, "padding and priority skipped");
      Bytes bad = H(&a, "000002 00 08 00000001 09 41");
      CHECK(h2_payload_end(bad, 0) == -1, "padding longer than the frame is an error"); }

    /* ---- HPACK: RFC 7541 C.2 (each on a fresh table) ---- */
    hpack_dyn_init_(dyn, 4096);
    block(&a, dyn, "400a 6375 7374 6f6d 2d6b 6579 0d63 7573 746f 6d2d 6865 6164 6572",
          "custom-key: custom-header\n", 55, "C.2.1 literal with indexing");
    hpack_dyn_init_(dyn, 4096);
    block(&a, dyn, "040c 2f73 616d 706c 652f 7061 7468", ":path: /sample/path\n", 0, "C.2.2 literal without indexing");
    hpack_dyn_init_(dyn, 4096);
    block(&a, dyn, "1008 7061 7373 776f 7264 0673 6563 7265 74", "password: secret\n", 0, "C.2.3 never indexed");
    hpack_dyn_init_(dyn, 4096);
    block(&a, dyn, "82", ":method: GET\n", 0, "C.2.4 indexed");

    /* C.3: three requests on one table, no Huffman */
    hpack_dyn_init_(dyn, 4096);
    block(&a, dyn, "8286 8441 0f77 7777 2e65 7861 6d70 6c65 2e63 6f6d",
          ":method: GET\n:scheme: http\n:path: /\n:authority: www.example.com\n", 57, "C.3.1");
    block(&a, dyn, "8286 84be 5808 6e6f 2d63 6163 6865",
          ":method: GET\n:scheme: http\n:path: /\n:authority: www.example.com\ncache-control: no-cache\n", 110, "C.3.2");
    block(&a, dyn, "8287 85bf 400a 6375 7374 6f6d 2d6b 6579 0c63 7573 746f 6d2d 7661 6c75 65",
          ":method: GET\n:scheme: https\n:path: /index.html\n:authority: www.example.com\ncustom-key: custom-value\n",
          164, "C.3.3");
    CHECK(hpack_dyn_count(dyn) == 3, "C.3 ends with three entries");

    /* C.4: the same requests, Huffman-coded */
    hpack_dyn_init_(dyn, 4096);
    block(&a, dyn, "8286 8441 8cf1 e3c2 e5f2 3a6b a0ab 90f4 ff",
          ":method: GET\n:scheme: http\n:path: /\n:authority: www.example.com\n", 57, "C.4.1");
    block(&a, dyn, "8286 84be 5886 a8eb 1064 9cbf",
          ":method: GET\n:scheme: http\n:path: /\n:authority: www.example.com\ncache-control: no-cache\n", 110, "C.4.2");
    block(&a, dyn, "8287 85bf 4088 25a8 49e9 5ba9 7d7f 8925 a849 e95b b8e8 b4bf",
          ":method: GET\n:scheme: https\n:path: /index.html\n:authority: www.example.com\ncustom-key: custom-value\n",
          164, "C.4.3");

    /* C.5: responses, table limit 256 -- the second evicts, the third evicts twice */
    { Bytes small = bytes_alloc_impl(&a, 12 + 512);
      hpack_dyn_init_(small, 256);
      block(&a, small,
            "4803 3330 3258 0770 7269 7661 7465 611d 4d6f 6e2c 2032 3120 4f63 7420 3230 3133 2032 303a 3133 3a32 3120 474d"
            "546e 1768 7474 7073 3a2f 2f77 7777 2e65 7861 6d70 6c65 2e63 6f6d",
            ":status: 302\ncache-control: private\ndate: Mon, 21 Oct 2013 20:13:21 GMT\nlocation: https://www.example.com\n",
            222, "C.5.1");
      block(&a, small, "4803 3330 37c1 c0bf",
            ":status: 307\ncache-control: private\ndate: Mon, 21 Oct 2013 20:13:21 GMT\nlocation: https://www.example.com\n",
            222, "C.5.2 (evicts :status 302)");
      block(&a, small,
            "88c1 611d 4d6f 6e2c 2032 3120 4f63 7420 3230 3133 2032 303a 3133 3a32 3220 474d 54c0 5a04 677a 6970 7738 666f"
            "6f3d 4153 444a 4b48 514b 425a 584f 5157 454f 5049 5541 5851 5745 4f49 553b 206d 6178 2d61 6765 3d33 3630 303b"
            "2076 6572 7369 6f6e 3d31",
            ":status: 200\ncache-control: private\ndate: Mon, 21 Oct 2013 20:13:22 GMT\nlocation: https://www.example.com\n"
            "content-encoding: gzip\nset-cookie: foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1\n",
            215, "C.5.3 (evicts twice)");
      CHECK(hpack_dyn_count(small) == 3, "C.5 ends with three entries");

      /* table size updates */
      Bytes up = H(&a, "3f e1 01"); /* 001 11111 + 225 -> 256 */
      Bytes o = bytes_alloc_impl(&a, 64);
      CHECK(hpack_decode_block(up, 0, (int)up.len, small, o) == 0 && hpack_dyn_max(small) == 256, "size update to the limit");
      Bytes zero = H(&a, "20");
      CHECK(hpack_decode_block(zero, 0, 1, small, o) == 0 && hpack_dyn_count(small) == 0 && hpack_dyn_size(small) == 0,
            "size update to 0 empties the table");
      Bytes over = H(&a, "3f e2 01"); /* 257 > limit 256 */
      CHECK(hpack_decode_block(over, 0, (int)over.len, small, o) == -1, "a size update above our limit is an error"); }

    /* errors */
    { Bytes o = bytes_alloc_impl(&a, 256);
      hpack_dyn_init_(dyn, 4096);
      CHECK(hpack_decode_block(H(&a, "80"), 0, 1, dyn, o) == -1, "index 0 is an error");
      CHECK(hpack_decode_block(H(&a, "be"), 0, 1, dyn, o) == -1, "a dynamic index past the table is an error");
      CHECK(hpack_decode_block(H(&a, "400a 6375"), 0, 4, dyn, o) == -1, "a truncated literal is an error");
      Bytes tiny = bytes_alloc_impl(&a, 4);
      CHECK(hpack_decode_block(H(&a, "82"), 0, 1, dyn, tiny) == -1, "output overflow is an error"); }

    /* our GET encoder round-trips through our decoder, and frames as one HEADERS */
    { Bytes blk = bytes_alloc_impl(&a, 256), out = bytes_alloc_impl(&a, 512), fr = bytes_alloc_impl(&a, 512);
      int e = hpack_put_get_request_(blk, 0, S(&a, "example.com"), S(&a, "/index.html"), S(&a, "user-agent"), S(&a, "pellegrino"));
      CHECK(e > 0, "GET encoded");
      hpack_dyn_init_(dyn, 4096);
      int d = hpack_decode_block(blk, 0, e, dyn, out);
      const char *want = ":method: GET\n:scheme: https\n:authority: example.com\n:path: /index.html\nuser-agent: pellegrino\n";
      CHECK(d == (int)strlen(want) && memcmp(out.data, want, (size_t)d) == 0, "own encoder round-trips");
      CHECK(hpack_dyn_count(dyn) == 0, "and touches no table (literals without indexing)");
      int f = h2_put_headers_(fr, 0, 1, blk, e, 1);
      CHECK(f == 9 + e && h2_frame_type(fr, 0) == 1 && h2_frame_flags(fr, 0) == 5 && h2_frame_stream(fr, 0) == 1,
            "one HEADERS frame, END_HEADERS | END_STREAM, stream 1");

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}
