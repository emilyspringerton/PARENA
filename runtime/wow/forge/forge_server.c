/* runtime/wow/forge/forge_server.c -- the plugin forge's local web page: a tiny HTTP server on 127.0.0.1 that serves
 * forge.html and answers its API by running the forge's own command line (forge.lua through scripts/forge.sh), so the
 * page and the CLI can never disagree. Started by `scripts/forge.sh serve [PORT]`; C99, no dependencies (winsock on
 * Windows, where MSYS2 UCRT64 gcc builds it with -lws2_32).
 *
 *   forge_server PORT PAGE FORGE_DIR COMMAND...     e.g. forge_server 7744 forge.html ~/forge bash scripts/forge.sh
 *
 *   GET  /               the page
 *   POST /api            body: the forge command and its arguments, one per line, with "\\" and "\n" escaped
 *                        (exactly forge.lua's --argfile format); answer: forge.lua's --json output
 *
 * Safety: it listens on 127.0.0.1 only and handles one request at a time. A visitor's text never reaches a command
 * line -- it goes into an argument file and the command line is made only of this server's own arguments (refused at
 * start if they hold a quote, $, `, % or a newline). Other web sites cannot drive it: /api needs an `X-Forge: 1`
 * header (a cross-site form cannot send one without a preflight this server never answers) and a Host of
 * 127.0.0.1:PORT or localhost:PORT (no DNS rebinding). The forge itself only ever edits FORGE_DIR/kits.lua. */
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
typedef SOCKET sock_t;
#  define CLOSESOCK closesocket
#  define BAD_SOCK INVALID_SOCKET
#else
#  define _POSIX_C_SOURCE 200809L
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
typedef int sock_t;
#  define CLOSESOCK close
#  define BAD_SOCK (-1)
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_REQ   (128 * 1024)
#define MAX_REPLY (4 * 1024 * 1024)

static int port;
static const char *page_path, *forge_dir;
static char command[4096];

static int safe_arg(const char *s) {
    for (; *s; s++) if (strchr("\"$`%\n\r", *s)) return 0;
    return 1;
}

static long read_file(const char *path, char *buf, long cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    long n = (long)fread(buf, 1, (size_t)cap - 1, f);
    fclose(f);
    buf[n] = 0;
    return n;
}

static void send_all(sock_t c, const char *p, long n) {
    while (n > 0) {
        int w = (int)send(c, p, (int)n, 0);
        if (w <= 0) return;
        p += w; n -= w;
    }
}

static void reply(sock_t c, int code, const char *status, const char *type, const char *body, long n) {
    char head[512];
    int h = snprintf(head, sizeof head,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %ld\r\nCache-Control: no-store\r\n"
        "X-Content-Type-Options: nosniff\r\nX-Frame-Options: DENY\r\nConnection: close\r\n\r\n", code, status, type, n);
    send_all(c, head, h);
    send_all(c, body, n);
}

static void reply_json_error(sock_t c, int code, const char *status, const char *msg) {
    char body[1024];
    /* msg is always one of this file's own literals: no escaping needed */
    int n = snprintf(body, sizeof body, "{\"error\":\"%s\"}\n", msg);
    reply(c, code, status, "application/json", body, n);
}

/* The value of header `name` (case-insensitive), copied into out; 0 when absent. */
static int header(const char *req, const char *name, char *out, size_t cap) {
    size_t nl = strlen(name);
    const char *p = strstr(req, "\r\n");
    while (p && p[2] && !(p[2] == '\r' && p[3] == '\n')) {
        p += 2;
        size_t i = 0;
        while (i < nl && p[i] && (p[i] | 32) == (name[i] | 32)) i++;
        if (i == nl && p[i] == ':') {
            const char *v = p + i + 1;
            while (*v == ' ') v++;
            size_t k = 0;
            while (v[k] && v[k] != '\r' && k + 1 < cap) { out[k] = v[k]; k++; }
            out[k] = 0;
            return 1;
        }
        p = strstr(p, "\r\n");
    }
    return 0;
}

static void run_api(sock_t c, const char *body, long n) {
    static char out[MAX_REPLY];
    char argfile[1024], outfile[1024], errfile[1024], cmd[16384];
    for (long i = 0; i < n; i++) if (body[i] == 0) { reply_json_error(c, 400, "Bad Request", "the request has a NUL byte"); return; }
    snprintf(argfile, sizeof argfile, "%s/.server-args.txt", forge_dir);
    snprintf(outfile, sizeof outfile, "%s/.server-out.json", forge_dir);
    snprintf(errfile, sizeof errfile, "%s/.server-err.txt", forge_dir);
    FILE *f = fopen(argfile, "wb");
    if (!f) { reply_json_error(c, 500, "Internal Server Error", "cannot write the argument file in FORGE_DIR"); return; }
    fwrite(body, 1, (size_t)n, f);
    fclose(f);
    snprintf(cmd, sizeof cmd, "%s --json --argfile \"%s\" > \"%s\" 2> \"%s\"", command, argfile, outfile, errfile);
#ifdef _WIN32
    { /* cmd.exe drops the outer quotes of a line that starts with one; wrap the whole line once more */
        char wrapped[16400];
        snprintf(wrapped, sizeof wrapped, "\"%s\"", cmd);
        system(wrapped);
    }
#else
    if (system(cmd) == -1) { reply_json_error(c, 500, "Internal Server Error", "could not run the forge"); return; }
#endif
    long m = read_file(outfile, out, sizeof out);
    if (m <= 0) {
        /* nothing on stdout: say what the forge said on stderr, as a JSON string */
        static char err[16384], esc[40000];
        long e = read_file(errfile, err, sizeof err);
        long k = 0;
        esc[k++] = '{'; memcpy(esc + k, "\"error\":\"", 9); k += 9;
        for (long i = 0; i < (e > 0 ? e : 0) && k < (long)sizeof esc - 16; i++) {
            unsigned char ch = (unsigned char)err[i];
            if (ch == '"' || ch == '\\') { esc[k++] = '\\'; esc[k++] = (char)ch; }
            else if (ch == '\n') { esc[k++] = '\\'; esc[k++] = 'n'; }
            else if (ch < 0x20) { k += snprintf(esc + k, 8, "\\u%04x", ch); }
            else esc[k++] = (char)ch;
        }
        if (e <= 0) { const char *t = "the forge printed nothing"; memcpy(esc + k, t, strlen(t)); k += (long)strlen(t); }
        esc[k++] = '"'; esc[k++] = '}'; esc[k++] = '\n';
        reply(c, 200, "OK", "application/json", esc, k);
        return;
    }
    reply(c, 200, "OK", "application/json", out, m);
}

static void handle(sock_t c) {
    static char req[MAX_REQ + 1];
    long got = 0, body_at = -1, want = 0;
    for (;;) {
        int r = (int)recv(c, req + got, (int)(MAX_REQ - got), 0);
        if (r <= 0) break;
        got += r;
        req[got] = 0;
        if (body_at < 0) {
            char *e = strstr(req, "\r\n\r\n");
            if (e) {
                body_at = (long)(e - req) + 4;
                char len[32];
                want = header(req, "Content-Length", len, sizeof len) ? atol(len) : 0;
                if (want < 0 || want > MAX_REQ - body_at) { reply_json_error(c, 413, "Payload Too Large", "too large"); return; }
            }
        }
        if (body_at >= 0 && got >= body_at + want) break;
        if (got >= MAX_REQ) { reply_json_error(c, 413, "Payload Too Large", "too large"); return; }
    }
    if (body_at < 0) return;

    char host[256], expect1[64], expect2[64];
    snprintf(expect1, sizeof expect1, "127.0.0.1:%d", port);
    snprintf(expect2, sizeof expect2, "localhost:%d", port);
    if (!header(req, "Host", host, sizeof host) || (strcmp(host, expect1) != 0 && strcmp(host, expect2) != 0)) {
        reply_json_error(c, 403, "Forbidden", "open the forge at http://127.0.0.1:PORT/");
        return;
    }

    if (strncmp(req, "GET / ", 6) == 0 || strncmp(req, "GET /index.html ", 16) == 0) {
        static char page[1024 * 1024];
        long n = read_file(page_path, page, sizeof page);
        if (n < 0) { reply_json_error(c, 500, "Internal Server Error", "cannot read the page"); return; }
        reply(c, 200, "OK", "text/html; charset=utf-8", page, n);
    } else if (strncmp(req, "POST /api ", 10) == 0) {
        char xf[16];
        if (!header(req, "X-Forge", xf, sizeof xf) || strcmp(xf, "1") != 0) {
            reply_json_error(c, 403, "Forbidden", "the forge's API answers its own page only");
            return;
        }
        run_api(c, req + body_at, want);
    } else {
        reply_json_error(c, 404, "Not Found", "not found");
    }
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: forge_server PORT PAGE FORGE_DIR COMMAND...   (run it with scripts/forge.sh serve)\n");
        return 2;
    }
    port = atoi(argv[1]);
    page_path = argv[2];
    forge_dir = argv[3];
    if (port <= 0 || port > 65535) { fprintf(stderr, "forge_server: bad port\n"); return 2; }
    if (!safe_arg(forge_dir)) { fprintf(stderr, "forge_server: FORGE_DIR may not hold a quote, $, `, %% or a newline\n"); return 2; }
    size_t k = 0;
    for (int i = 4; i < argc; i++) {
        if (!safe_arg(argv[i])) { fprintf(stderr, "forge_server: '%s' may not hold a quote, $, `, %% or a newline\n", argv[i]); return 2; }
        int w = snprintf(command + k, sizeof command - k, "%s\"%s\"", i > 4 ? " " : "", argv[i]);
        if (w < 0 || (size_t)w >= sizeof command - k) { fprintf(stderr, "forge_server: command too long\n"); return 2; }
        k += (size_t)w;
    }
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { fprintf(stderr, "forge_server: no winsock\n"); return 1; }
#endif
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == BAD_SOCK) { perror("socket"); return 1; }
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);          /* 127.0.0.1 only: never reachable from the network */
    if (bind(s, (struct sockaddr *)&a, sizeof a) != 0) { fprintf(stderr, "forge_server: port %d is taken\n", port); return 1; }
    if (listen(s, 8) != 0) { perror("listen"); return 1; }
    fflush(stdout);
    for (;;) {
        sock_t c = accept(s, NULL, NULL);
        if (c == BAD_SOCK) continue;
        handle(c);
        CLOSESOCK(c);
    }
}
