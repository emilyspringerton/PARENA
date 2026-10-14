/* tests/test_net_stream_tls.c -- HTTPS half of stdlib/net/stream.prn against a local
 * `openssl s_server` (hermetic: loopback, certificates generated here with the openssl CLI).
 *
 * The CA store is loaded once per process (that is the point: parsing the bundle on every connect
 * would be waste), so each case runs in its own forked child with its own SSL_CERT_FILE:
 *   trusted      server cert is the trust anchor, name matches        -> handshake, response
 *   untrusted    server cert is NOT the trust anchor                  -> refused, code 3
 *   wrong host   anchor is right but the name does not match          -> refused, code 3
 *   no bundle    SSL_CERT_FILE points nowhere                         -> refused, code 4 (fail closed)
 * and the hardening cases (each guards a bug a review found):
 *   stale handle     a closed TLS handle never closes/reads/writes the descriptor that reuses its number
 *   superseded       a second connect invalidates the first handle instead of aliasing it
 *   fd guards        pull/send/close on -1, out-of-range or forged handles fail cleanly (no FD_SET abort)
 *   cwd not trusted  a cacert.pem in the current directory is never a trust anchor
 *   exe-dir bundle   cacert.pem beside the executable IS used when nothing else is available
 *   truncation       a stream that ends without close_notify reports -3, a clean one -1
 *   deadline         a silent peer and a byte-dripping peer both hit the handshake cap (code 2)
 *   EINTR            a SIGALRM storm during connect/handshake does not fail a good connection
 *
 * Built with -DPARENA_TLS_SKIP_SYSTEM_BUNDLES (simulates a machine without a system store) and a
 * short -DPARENA_TLS_HANDSHAKE_MS. Skips (exit 0, says so) when the openssl CLI is not installed;
 * the python3-server cases skip when python3 is missing.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include "test_net_stream_tls_gen.c"

static char dir[128];

static int free_port(void) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(s, (struct sockaddr *)&a, sizeof a) == 0);
    socklen_t l = sizeof a;
    getsockname(s, (struct sockaddr *)&a, &l);
    int p = ntohs(a.sin_port);
    close(s);
    return p;
}

static int mkcert(const char *name, const char *cn) {
    char cmd[640];
    snprintf(cmd, sizeof cmd,
             "openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 2 "
             "-keyout %s/%s.key -out %s/%s.pem -subj /CN=%s -addext subjectAltName=DNS:%s >/dev/null 2>&1",
             dir, name, dir, name, cn, cn);
    return system(cmd);
}

static pid_t spawn_server(const char *name, int port) {
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        char acc[32], cert[256], key[256];
        snprintf(acc, sizeof acc, "%d", port);
        snprintf(cert, sizeof cert, "%s/%s.pem", dir, name);
        snprintf(key, sizeof key, "%s/%s.key", dir, name);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        execlp("openssl", "openssl", "s_server", "-accept", acc, "-cert", cert, "-key", key, "-www", "-quiet",
               (char *)NULL);
        _exit(127);
    }
    for (int i = 0; i < 100; i++) {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in a;
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons((unsigned short)port);
        int ok = connect(s, (struct sockaddr *)&a, sizeof a) == 0;
        close(s);
        if (ok) return pid;
        usleep(50 * 1000);
    }
    assert(0 && "openssl s_server did not start");
    return pid;
}

/* one case in a child process. Exit: 0 = fetched a response, 10+code = connect refused with that code */
static int run_case(const char *bundle_path, int port, const char *host, int *code_out, char *err_out) {
    int pipefd[2];
    assert(pipe(pipefd) == 0);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        close(pipefd[0]);
        setenv("SSL_CERT_FILE", bundle_path, 1);
        int fd = stream_connect_tls((char *)host, port);
        if (fd < 0) {
            char *e = stream_tls_error();
            write(pipefd[1], e, strlen(e));
            _exit(10 + stream_tls_code());
        }
        if (strncmp(stream_tls_info(), "TLSv1", 5) != 0) _exit(90);
        if (stream_send(fd, "GET / HTTP/1.0\r\nHost: x\r\n\r\n") != 0) _exit(91);
        int spins = 0;
        for (;;) {
            int r = stream_pull(fd);
            if (r > 0) continue;
            if (r == 0) { usleep(5000); if (++spins > 1000) _exit(92); continue; }
            break;
        }
        if (strstr(stream_buf(), "HTTP/1.0 200 ok") == NULL) _exit(93);
        stream_close(fd);
        _exit(0);
    }
    close(pipefd[1]);
    ssize_t n = read(pipefd[0], err_out, 255);
    err_out[n > 0 ? n : 0] = '\0';
    close(pipefd[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    assert(WIFEXITED(st));
    *code_out = WEXITSTATUS(st);
    return *code_out;
}


/* ---- hardening cases (each runs in its own forked child; exit code 0 = pass) ---- */
static char g_good[256];

static int serve_wait(int port) { /* wait until something accepts on 127.0.0.1:port */
    for (int i = 0; i < 100; i++) {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in a;
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons((unsigned short)port);
        int ok = connect(s, (struct sockaddr *)&a, sizeof a) == 0;
        close(s);
        if (ok) return 1;
        usleep(50 * 1000);
    }
    return 0;
}

static const char SRV_PY[] =
    "import socket, ssl, sys, threading, time\n"
    "port, cert, key, mode = int(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4]\n"
    "ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)\n"
    "ctx.load_cert_chain(cert, key)\n"
    "ls = socket.socket()\n"
    "ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)\n"
    "ls.bind(('127.0.0.1', port))\n"
    "ls.listen(16)\n"
    "def serve(c):\n"
    "    s = None\n"
    "    try:\n"
    "        if mode == 'silent':\n"
    "            time.sleep(60)\n"
    "        elif mode == 'drip':\n"
    "            c.sendall(b'\\x16\\x03\\x03\\x3e\\x80')\n"
    "            for _ in range(5000):\n"
    "                c.sendall(b'\\x00')\n"
    "                time.sleep(0.02)\n"
    "        else:\n"
    "            s = ctx.wrap_socket(c, server_side=True)\n"
    "            s.recv(4096)\n"
    "            s.sendall(b'HTTP/1.0 200 ok\\r\\nContent-Length: 100000\\r\\n\\r\\npartial')\n"
    "            if mode == 'notify':\n"
    "                s.unwrap()\n"
    "    except Exception:\n"
    "        pass\n"
    "    finally:\n"
    "        try:\n"
    "            (s or c).close()\n"
    "        except Exception:\n"
    "            pass\n"
    "while True:\n"
    "    c, _ = ls.accept()\n"
    "    threading.Thread(target=serve, args=(c,), daemon=True).start()\n";

static pid_t spawn_py(const char *mode, int port) {
    char script[256];
    snprintf(script, sizeof script, "%s/srv.py", dir);
    FILE *f = fopen(script, "w");
    assert(f != NULL);
    fputs(SRV_PY, f);
    fclose(f);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        char acc[32], cert[256], key[256];
        snprintf(acc, sizeof acc, "%d", port);
        snprintf(cert, sizeof cert, "%s/good.pem", dir);
        snprintf(key, sizeof key, "%s/good.key", dir);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        execlp("python3", "python3", script, acc, cert, key, mode, (char *)NULL);
        _exit(127);
    }
    assert(serve_wait(port) && "python TLS test server did not start");
    return pid;
}

typedef int (*case_fn)(int port);
static int run_fn(case_fn f, int port) {
    fflush(stdout);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) _exit(f(port));
    int st = 0;
    waitpid(pid, &st, 0);
    assert(WIFEXITED(st));
    return WEXITSTATUS(st);
}

static int case_stale_handle(int port) {
    setenv("SSL_CERT_FILE", g_good, 1);
    int h = stream_connect_tls((char *)"localhost", port);
    if (h < 0) return 1;
    if ((h & PARENA_TLS_HANDLE_BIT) == 0) return 2; /* a TLS handle is tagged, never a bare descriptor */
    int raw = h & 0xFFFF;
    if (stream_close(h) != 0) return 3;
    int other = open("/dev/null", O_RDONLY); /* takes the lowest free number == the old descriptor */
    if (other != raw) return 4;
    if (stream_close(h) != -1) return 5;          /* stale close must not touch `other` */
    if (fcntl(other, F_GETFD) < 0) return 6;
    if (stream_pull(h) != -1) return 7;
    if (stream_send(h, (char *)"x") != -1) return 8;
    if (fcntl(other, F_GETFD) < 0) return 9;
    return 0;
}

static int case_superseded(int port) {
    setenv("SSL_CERT_FILE", g_good, 1);
    int h1 = stream_connect_tls((char *)"localhost", port);
    if (h1 < 0) return 1;
    int h2 = stream_connect_tls((char *)"localhost", port); /* one TLS session at a time: h1 is gone */
    if (h2 < 0) return 2;
    if (h1 == h2) return 3;                 /* same descriptor number, different handle */
    if (stream_close(h1) != -1) return 4;   /* must not kill the live second session */
    if (stream_send(h2, (char *)"GET / HTTP/1.0\r\nHost: x\r\n\r\n") != 0) return 5;
    int spins = 0, r;
    do {
        r = stream_pull(h2);
        if (r == 0) { usleep(5000); if (++spins > 1000) return 6; }
    } while (r >= 0);
    if (strstr(stream_buf(), "HTTP/1.0 200 ok") == NULL) return 7;
    if (stream_close(h2) != 0) return 8;
    return 0;
}

static int case_fd_guards(int port) {
    (void)port;
    if (stream_pull(-1) != -1) return 1;
    if (stream_send(-1, (char *)"x") != -1) return 2;
    if (stream_close(-1) != -1) return 3;
    if (stream_pull(100000) != -1) return 4;      /* >= FD_SETSIZE: FD_SET would write out of bounds */
    if (stream_send(100000, (char *)"x") != -1) return 5;
    if (stream_close(100000) != -1) return 6;
    if (stream_pull(PARENA_TLS_HANDLE_BIT | 5) != -1) return 7; /* forged / stale TLS handle */
    if (stream_send(PARENA_TLS_HANDLE_BIT | 5, (char *)"x") != -1) return 8;
    if (stream_close(PARENA_TLS_HANDLE_BIT | 5) != -1) return 9;
    return 0;
}

static int case_cwd_not_trusted(int port) {
    char cwd[256], bundle[700];
    snprintf(cwd, sizeof cwd, "%s/cwd", dir);
    mkdir(cwd, 0700);
    snprintf(bundle, sizeof bundle, "cp %s %s/cacert.pem", g_good, cwd);
    if (system(bundle) != 0) return 1;
    unsetenv("SSL_CERT_FILE");
    if (chdir(cwd) != 0) return 2;
    int h = stream_connect_tls((char *)"localhost", port);
    if (h >= 0) return 3;                         /* the cwd bundle became a trust anchor */
    return stream_tls_code() == 4 ? 0 : 4;        /* fails closed: no CA bundle */
}

static int case_exedir_bundle(int port) {
    char exedir[256], cmd[1400], exe[300];
    snprintf(exedir, sizeof exedir, "%s/exe", dir);
    mkdir(exedir, 0700);
    snprintf(exe, sizeof exe, "%s/tbin", exedir);
    char self[400];
    ssize_t sn = readlink("/proc/self/exe", self, sizeof self - 1);
    if (sn <= 0) return 3;
    self[sn] = '\0';
    snprintf(cmd, sizeof cmd, "cp %s %s && cp %s %s/cacert.pem", self, exe, g_good, exedir);
    if (system(cmd) != 0) return 1;
    char p[16];
    snprintf(p, sizeof p, "%d", port);
    unsetenv("SSL_CERT_FILE");
    execl(exe, exe, "--exedir", p, (char *)NULL);
    return 2;
}

static int stream_result(int port, int expect_end) { /* read to the end; returns 0 when it ended with expect_end */
    setenv("SSL_CERT_FILE", g_good, 1);
    int h = stream_connect_tls((char *)"localhost", port);
    if (h < 0) return 1;
    if (stream_send(h, (char *)"GET / HTTP/1.0\r\nHost: x\r\n\r\n") != 0) return 2;
    int spins = 0, r;
    do {
        r = stream_pull(h);
        if (r == 0) { usleep(5000); if (++spins > 1000) return 3; }
    } while (r >= 0);
    if (strstr(stream_buf(), "partial") == NULL) return 4;
    if (r != expect_end) return 10 + (-r);
    stream_close(h);
    return 0;
}
static int case_truncated(int port) { return stream_result(port, -3); }
static int case_clean_end(int port) { return stream_result(port, -1); }

static int case_deadline(int port) {
    setenv("SSL_CERT_FILE", g_good, 1);
    long long t0 = stream_now_ms();
    int h = stream_connect_tls((char *)"localhost", port);
    long long dt = stream_now_ms() - t0;
    if (h >= 0) return 1;
    if (stream_tls_code() != 2) return 2;
    if (dt < 1000) return 3;   /* the cap (1500 ms in this build) must actually be waited out */
    if (dt > 4500) return 4;   /* ...and a dripping peer must not stretch it */
    return 0;
}

static void on_alarm(int sig) { (void)sig; }
static int case_eintr(int port) {
    setenv("SSL_CERT_FILE", g_good, 1);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_alarm; /* no SA_RESTART: select() really returns EINTR */
    sigaction(SIGALRM, &sa, NULL);
    struct itimerval it;
    it.it_interval.tv_sec = 0; it.it_interval.tv_usec = 1000;
    it.it_value = it.it_interval;
    setitimer(ITIMER_REAL, &it, NULL);
    for (int i = 0; i < 25; i++) {
        int h = stream_connect_tls((char *)"localhost", port);
        if (h < 0) return 1 + i;
        stream_close(h);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 3 && strcmp(argv[1], "--exedir") == 0) { /* re-exec'd copy: only cacert.pem beside it */
        int h = stream_connect_tls((char *)"localhost", atoi(argv[2]));
        if (h < 0) return 20 + stream_tls_code();
        stream_close(h);
        return 0;
    }
    if (system("openssl version >/dev/null 2>&1") != 0) {
        printf("test_net_stream_tls: skipped (no openssl CLI)\n");
        return 0;
    }
    snprintf(dir, sizeof dir, "/tmp/pn_stls_XXXXXX");
    assert(mkdtemp(dir) != NULL);
    assert(mkcert("good", "localhost") == 0);
    assert(mkcert("other", "unrelated.example") == 0);
    assert(mkcert("wrong", "other.example") == 0);

    char good[256], other[256], wrong[256], err[256];
    snprintf(g_good, sizeof g_good, "%s/good.pem", dir);
    snprintf(good, sizeof good, "%s/good.pem", dir);
    snprintf(other, sizeof other, "%s/other.pem", dir);
    snprintf(wrong, sizeof wrong, "%s/wrong.pem", dir);
    int code = 0;

    int p1 = free_port();
    pid_t s1 = spawn_server("good", p1);
    run_case(good, p1, "localhost", &code, err);
    assert(code == 0 && "trusted certificate: handshake + response expected");
    run_case(other, p1, "localhost", &code, err);
    assert(code == 10 + 3 && "untrusted certificate must be refused with code 3");
    assert(strstr(err, "certificate rejected") != NULL);
    run_case("/nonexistent/ca.pem", p1, "localhost", &code, err);
    assert(code == 10 + 4 && "unreadable SSL_CERT_FILE must fail closed with code 4");

    assert(run_fn(case_stale_handle, p1) == 0 && "closed TLS handle must not alias a reused descriptor");
    assert(run_fn(case_superseded, p1) == 0 && "superseded TLS handle must be invalidated, not aliased");
    assert(run_fn(case_fd_guards, p1) == 0 && "out-of-range / forged handles must fail cleanly");
    assert(run_fn(case_cwd_not_trusted, p1) == 0 && "cacert.pem in the cwd must never be trusted");
    assert(run_fn(case_exedir_bundle, p1) == 0 && "cacert.pem beside the executable must be used");
    assert(run_fn(case_eintr, p1) == 0 && "signals must not fail a good connection");
    kill(s1, SIGTERM);
    waitpid(s1, NULL, 0);

    if (system("python3 --version >/dev/null 2>&1") == 0) {
        int pt = free_port(), pn = free_port(), ps = free_port(), pd = free_port();
        pid_t a = spawn_py("truncate", pt), b = spawn_py("notify", pn), c = spawn_py("silent", ps),
              d = spawn_py("drip", pd);
        assert(run_fn(case_truncated, pt) == 0 && "an end without close_notify must report -3");
        assert(run_fn(case_clean_end, pn) == 0 && "an authenticated close_notify must report -1");
        assert(run_fn(case_deadline, ps) == 0 && "a silent peer must hit the handshake cap (code 2)");
        assert(run_fn(case_deadline, pd) == 0 && "a dripping peer must still hit the handshake cap");
        kill(a, SIGTERM); kill(b, SIGTERM); kill(c, SIGTERM); kill(d, SIGTERM);
        waitpid(a, NULL, 0); waitpid(b, NULL, 0); waitpid(c, NULL, 0); waitpid(d, NULL, 0);
    } else {
        printf("test_net_stream_tls: python3 cases skipped (no python3)\n");
    }

    int p2 = free_port();
    pid_t s2 = spawn_server("wrong", p2);
    run_case(wrong, p2, "localhost", &code, err);
    assert(code == 10 + 3 && "name mismatch must be refused with code 3 even though the chain is trusted");
    assert(strstr(err, "does not match") != NULL);
    kill(s2, SIGTERM);
    waitpid(s2, NULL, 0);

    char cmd[200];
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    (void)!system(cmd);
    printf("test_net_stream_tls: ok\n");
    return 0;
}
