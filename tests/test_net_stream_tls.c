/* tests/test_net_stream_tls.c -- HTTPS half of stdlib/net/stream.prn against a local
 * `openssl s_server` (hermetic: loopback, certificates generated here with the openssl CLI).
 *
 * The CA store is loaded once per process (that is the point: parsing the bundle on every connect
 * would be waste), so each case runs in its own forked child with its own SSL_CERT_FILE:
 *   trusted      server cert is the trust anchor, name matches        -> handshake, response
 *   untrusted    server cert is NOT the trust anchor                  -> refused, code 3
 *   wrong host   anchor is right but the name does not match          -> refused, code 3
 *   no bundle    SSL_CERT_FILE points nowhere                         -> refused, code 4 (fail closed)
 *
 * Skips (exit 0, says so) when the openssl CLI is not installed.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

int main(void) {
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
    kill(s1, SIGTERM);
    waitpid(s1, NULL, 0);

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
