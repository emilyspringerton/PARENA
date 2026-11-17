/* tests/iduna/discover_test.c -- the C target of stdlib/iduna/discover.prn and stdlib/net/http_probe.prn.
 *
 * Concatenated onto the generated C (scripts/test_iduna_discover.sh), so it calls the generated functions directly.
 *   1. every row of stdlib/iduna/discover_parity.txt (argv[1]), the fixture the Lua and JS suites also assert;
 *   2. the probe against a scripted server on 127.0.0.1 in a second thread: IDUNA's health answer, an HTML 404, someone
 *      else's 200, a server that never answers, one that stalls mid-headers, an oversized body, a non-HTTP banner, a
 *      closed port, and "localhost" when only 127.0.0.1 listens (::1 must not eat the whole budget).
 * Prints "iduna discover (C): N passed, M failed" and exits non-zero on any failure. Made-up data only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static int g_pass = 0, g_fail = 0;

static void check(int ok, const char *what) {
    if (ok) g_pass++;
    else { g_fail++; printf("FAIL %s\n", what); }
}

/* ---- 1. the parity fixture ------------------------------------------------------------------------------------------ */
static int call_rule(const char *fn, int n, const int *a, int *out) {
#define ARGS(k) if (n != (k)) return 0
    if (strcmp(fn, "probe-verdict") == 0) { ARGS(3); *out = probe_verdict(a[0], a[1], a[2]); return 1; }
    if (strcmp(fn, "verdict-usable") == 0) { ARGS(1); *out = verdict_usable(a[0]); return 1; }
    if (strcmp(fn, "next-candidate") == 0) { ARGS(3); *out = next_candidate(a[0], a[1], a[2]); return 1; }
    if (strcmp(fn, "should-try-local") == 0) { ARGS(2); *out = should_try_local(a[0], a[1]); return 1; }
    if (strcmp(fn, "should-try-same-origin") == 0) { ARGS(1); *out = should_try_same_origin(a[0]); return 1; }
    if (strcmp(fn, "probe-timeout-ms") == 0) { ARGS(2); *out = probe_timeout_ms(a[0], a[1]); return 1; }
    if (strcmp(fn, "cache-fresh") == 0) { ARGS(3); *out = cache_fresh(a[0], a[1], a[2]); return 1; }
    if (strcmp(fn, "fallback-source") == 0) { ARGS(0); *out = fallback_source(); return 1; }
#undef ARGS
    return 0;
}

static void run_parity(const char *path) {
    FILE *f = fopen(path, "r");
    char line[512];
    int rows = 0;
    if (f == NULL) { check(0, "open the parity fixture"); return; }
    while (fgets(line, sizeof line, f) != NULL) {
        char *tok, fn[64] = "";
        int args[8], n = 0, want = 0, have_want = 0, got = 0;
        char what[600];
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;
        snprintf(what, sizeof what, "parity: %s", line);
        for (tok = strtok(line, " \t"); tok != NULL; tok = strtok(NULL, " \t")) {
            if (fn[0] == '\0') { snprintf(fn, sizeof fn, "%s", tok); continue; }
            if (strcmp(tok, "=") == 0) { have_want = 1; continue; }
            if (have_want) want = atoi(tok);
            else if (n < 8) args[n++] = atoi(tok);
        }
        rows++;
        if (!call_rule(fn, n, args, &got)) { check(0, what); continue; }
        if (got != want) printf("  got %d, want %d\n", got, want);
        check(have_want && got == want, what);
    }
    fclose(f);
    check(rows >= 40, "parity: the fixture has its rows");
}

/* ---- 2. the probe against a scripted server ------------------------------------------------------------------------- */
static void nap_ms(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

typedef struct {
    http_probe_sock_t lst;
    const char *reply;  /* sent after the request is read */
    size_t reply_len;
    int hold_ms;        /* then the connection stays open this long before it closes */
} Script;

static http_probe_sock_t listen_loopback(int *port) {
    struct sockaddr_in a;
    socklen_t len = (socklen_t)sizeof a;
    http_probe_sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == HTTP_PROBE_BAD_SOCK) return s;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    if (bind(s, (struct sockaddr *)&a, (int)sizeof a) != 0 || listen(s, 4) != 0 ||
        getsockname(s, (struct sockaddr *)&a, &len) != 0) {
        http_probe_close(s);
        return HTTP_PROBE_BAD_SOCK;
    }
    *port = ntohs(a.sin_port);
    return s;
}

static void *serve_once(void *arg) {
    Script *sc = (Script *)arg;
    char req[2048];
    size_t got = 0;
    http_probe_sock_t c = accept(sc->lst, NULL, NULL);
    if (c == HTTP_PROBE_BAD_SOCK) return NULL;
    while (got < sizeof req - 1) { /* read the request up to its blank line */
        int r = (int)recv(c, req + got, (int)(sizeof req - 1 - got), 0);
        if (r <= 0) break;
        got += (size_t)r;
        req[got] = '\0';
        if (strstr(req, "\r\n\r\n") != NULL) break;
    }
    if (sc->reply_len > 0) send(c, sc->reply, (int)sc->reply_len, 0);
    if (sc->hold_ms > 0) nap_ms(sc->hold_ms);
    http_probe_close(c);
    return NULL;
}

typedef struct { int verdict, status, timed_out; long long ms; size_t body_len; char body[64]; } Seen;

/* Probe a scripted server once. host is "127.0.0.1" or "localhost". */
static Seen probe_script(const char *host, const char *reply, size_t reply_len, int hold_ms, int timeout_ms) {
    Seen r;
    Script sc;
    pthread_t th;
    Arena scratch;
    int port = 0;
    long long t0;
    char *body;
    memset(&r, 0, sizeof r);
    r.verdict = -1;
    sc.lst = listen_loopback(&port);
    sc.reply = reply;
    sc.reply_len = reply_len;
    sc.hold_ms = hold_ms;
    if (sc.lst == HTTP_PROBE_BAD_SOCK) return r;
    pthread_create(&th, NULL, serve_once, &sc);
    arena_init(&scratch);
    t0 = http_probe_now_ms();
    r.verdict = probe_iduna_at((char *)host, port, (char *)"/health", timeout_ms, &scratch);
    r.ms = http_probe_now_ms() - t0;
    r.status = probe_status();
    r.timed_out = probe_timed_out();
    body = probe_body(&scratch);
    r.body_len = strlen(body);
    snprintf(r.body, sizeof r.body, "%s", body);
    pthread_join(th, NULL);
    http_probe_close(sc.lst);
    arena_free_all(&scratch);
    return r;
}

#define REPLY(s) s, sizeof(s) - 1

static void run_probe_tests(void) {
    static char big[200 + 10000];
    char what[200];
    Seen r;
    int port = 0;
    http_probe_sock_t gone;
    Arena scratch;
    long long t0;
    int v;

    r = probe_script("127.0.0.1", REPLY("HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n\r\n"
                                       "{\"ok\":true,\"service\":\"iduna\",\"time\":\"2026-11-17T00:00:00Z\"}\n"), 0, 2000);
    check(r.verdict == 0, "probe: IDUNA's health answer is IDUNA");
    check(r.status == 200 && r.timed_out == 0, "probe: status 200, not timed out");
    check(strncmp(r.body, "{\"ok\":true,\"service\":\"iduna\"", 28) == 0, "probe: the body comes back");
    check(r.ms < 1000, "probe: a local answer is fast");

    r = probe_script("127.0.0.1", REPLY("HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\n<html>404</html>"), 0, 2000);
    check(r.verdict == 1 && r.status == 404, "probe: an HTML 404 is something else, not IDUNA");

    r = probe_script("127.0.0.1", REPLY("HTTP/1.0 200 OK\r\n\r\n{\"ok\":true,\"service\":\"hoard\"}"), 0, 2000);
    check(r.verdict == 1 && r.status == 200, "probe: someone else's 200 is not IDUNA");

    r = probe_script("127.0.0.1", REPLY("HTTP/1.0 503 Service Unavailable\r\n\r\n{\"ok\":false,\"service\":\"iduna\"}"), 0, 2000);
    check(r.verdict == 1 && r.status == 503, "probe: an IDUNA that says it is not ok is not usable");

    r = probe_script("127.0.0.1", "", 0, 1500, 400);
    check(r.verdict == 3 && r.status == 0 && r.timed_out == 1, "probe: a server that never answers times out");
    snprintf(what, sizeof what, "probe: the timeout is kept (took %lld ms for 400)", r.ms);
    check(r.ms >= 350 && r.ms < 1200, what);

    r = probe_script("127.0.0.1", REPLY("HTTP/1.0 200 OK\r\nContent-Type: appl"), 1500, 400);
    check(r.verdict == 3 && r.status == 0, "probe: headers cut off by the deadline give no status");

    memcpy(big, "HTTP/1.0 200 OK\r\n\r\n", 19);
    memset(big + 19, 'x', sizeof big - 19);
    r = probe_script("127.0.0.1", big, sizeof big, 0, 2000);
    check(r.status == 200 && r.body_len == 4096, "probe: an oversized body is cut at 4096 bytes");

    r = probe_script("127.0.0.1", REPLY("SSH-2.0-OpenSSH_9.6\r\n"), 0, 2000);
    check(r.verdict == 2 && r.status == 0 && r.timed_out == 0, "probe: a non-HTTP banner is no HTTP answer");

    /* "localhost" is ::1 and 127.0.0.1; only 127.0.0.1 listens. The ::1 attempt gets half the budget, not all of it. */
    r = probe_script("localhost", REPLY("HTTP/1.0 200 OK\r\n\r\n{\"ok\":true,\"service\":\"iduna\"}"), 0, 1500);
    snprintf(what, sizeof what, "probe: localhost finds the 127.0.0.1 listener (verdict %d, %lld ms)", r.verdict, r.ms);
    check(r.verdict == 0, what);

    /* A closed port: refused at once on POSIX; Windows retries a refused loopback connect, so it may hit the deadline. */
    gone = listen_loopback(&port);
    check(gone != HTTP_PROBE_BAD_SOCK, "probe: a port to close");
    http_probe_close(gone);
    arena_init(&scratch);
    t0 = http_probe_now_ms();
    v = probe_iduna_at((char *)"127.0.0.1", port, (char *)"/health", 600, &scratch);
    snprintf(what, sizeof what, "probe: a closed port is not IDUNA (verdict %d, %lld ms)", v, http_probe_now_ms() - t0);
    check((v == 2 || v == 3) && http_probe_now_ms() - t0 < 1500, what);
    arena_free_all(&scratch);

    arena_init(&scratch);
    v = probe_iduna_at((char *)"", 80, (char *)"/health", 200, &scratch);
    check(v == 2, "probe: an empty host is refused without a connect");
    v = probe_iduna_at((char *)"127.0.0.1", 70000, (char *)"/health", 200, &scratch);
    check(v == 2, "probe: an impossible port is refused without a connect");
    arena_free_all(&scratch);

    check(says_iduna((char *)"{\"ok\":true,\"service\":\"iduna\",\"time\":\"x\"}") == 1, "says-iduna: IDUNA's body");
    check(says_iduna((char *)"{\"ok\":false,\"service\":\"iduna\"}") == 0, "says-iduna: not ok");
    check(says_iduna((char *)"<html>iduna</html>") == 0, "says-iduna: a page that mentions iduna");
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: discover_test <discover_parity.txt>\n"); return 2; }
#ifdef _WIN32
    if (parena_wsa_start() != 0) { fprintf(stderr, "WSAStartup failed\n"); return 2; }
#endif
    run_parity(argv[1]);
    run_probe_tests();
    printf("iduna discover (C): %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
