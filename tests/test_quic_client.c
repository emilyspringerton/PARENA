/* tests/test_quic_client.c -- net/quic_client.prn end to end against an aioquic HTTP/3 server
 * (tests/h3/h3_server.py: an independent QUIC + TLS 1.3 + HTTP/3 implementation). The server's cert is
 * signed by a throwaway CA; trust comes ONLY through SSL_CERT_FILE -- there is no verification bypass.
 * usage: test_quic_client <host> <port> */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "test_quic_client_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } else { printf("PASS: %s\n", msg); } } while (0)

static int get(Arena *a, const char *host, int port, const char *path, int cap, int *status, Bytes *conn) {
    Bytes c = quic_connect((char *)host, port, cap, 8000, a);
    if (c.len == 0) return -100;
    int st = 0;
    for (int i = 0; i < 4000 && st == 0; i++) {
        st = quic_pump(c, (char *)host, (char *)path);
        if (st == 0) { struct timespec ts = {0, 2000000}; nanosleep(&ts, NULL); }
    }
    *status = quic_http_status(c);
    *conn = c;
    return st;
}

int main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    int port = argc > 2 ? atoi(argv[2]) : 14433;
    const char *mode = argc > 3 ? argv[3] : "ok";
    Arena a; arena_init(&a);
    int http = 0; Bytes c;

    if (!strcmp(mode, "reject")) {            /* certificate must be refused: bad CA trust or wrong name */
        int st = get(&a, host, port, "/", 65536, &http, &c);
        CHECK(st == -3, "untrusted/mismatched certificate -> status -3, no response");
        CHECK(quic_body_len(c) == 0 && http == 0, "no body or status leaked from an unauthenticated peer");
        quic_close(c); arena_free_all(&a);
        printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
        return failures ? 1 : 0;
    }
    if (!strcmp(mode, "lossy")) {             /* through udp_proxy.py with drops/reordering */
        int st = get(&a, host, port, "/big", 400000, &http, &c);
        CHECK(st == 1 && http == 200 && quic_body_len(c) == 200000, "200000-byte body survives packet loss / reordering");
        int ok = 1; const unsigned char *b = c.data + quic_body_off(c);
        for (int i = 0; i < quic_body_len(c); i++) if (b[i] != "0123456789abcdef"[i % 16]) { ok = 0; break; }
        CHECK(ok, "lossy body is byte-exact");
        quic_close(c); arena_free_all(&a);
        printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
        return failures ? 1 : 0;
    }

    int st = get(&a, host, port, "/", 65536, &http, &c);
    CHECK(st == 1, "GET / completes");
    CHECK(http == 200, "HTTP status 200");
    CHECK(quic_body_len(c) == 21 && memcmp(c.data + quic_body_off(c), "hello from aioquic h3", 21) == 0, "body is 'hello from aioquic h3'");
    { const char *h = (const char *)c.data + quic_headers_off(c);
      CHECK(strstr(h, "content-type: text/plain") != NULL && strstr(h, "server: aioquic-test") != NULL, "response headers decoded (QPACK)"); }
    quic_close(c);

    st = get(&a, host, port, "/echo-ua", 65536, &http, &c);
    CHECK(st == 1 && quic_body_len(c) == 13 && memcmp(c.data + quic_body_off(c), "pellegrino-h3", 13) == 0, "server saw our user-agent (request QPACK encoding is valid)");
    quic_close(c);

    st = get(&a, host, port, "/missing", 65536, &http, &c);
    CHECK(st == 1 && http == 404 && quic_body_len(c) == 4, "404 + body");
    quic_close(c);

    st = get(&a, host, port, "/big", 400000, &http, &c);
    CHECK(st == 1 && http == 200 && quic_body_len(c) == 200000, "200000-byte multi-packet response completes");
    { int ok = 1; const unsigned char *b = c.data + quic_body_off(c);
      for (int i = 0; i < 200000; i++) if (b[i] != "0123456789abcdef"[i % 16]) { ok = 0; break; }
      CHECK(ok, "large body is byte-exact"); }
    quic_close(c);

    /* body cap smaller than the response must fail closed, not truncate silently */
    st = get(&a, host, port, "/big", 4096, &http, &c);
    CHECK(st < 0, "response larger than body cap -> failure, not silent truncation");
    quic_close(c);

    arena_free_all(&a);
    printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
