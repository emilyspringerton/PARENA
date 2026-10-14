/* tests/test_net_stream.c -- real end-to-end verification of stdlib/net/stream.prn (PELLEGRINO's
 * incremental TCP client) on loopback, no external network, no TLS library needed.
 *
 * A forked child is a one-shot HTTP-ish server that sends "first", pauses, sends "second", closes.
 * The parent drives the PARENA-level API (stream-connect/send/pull/buf/len/reset/close) and asserts:
 *   - bytes are readable BEFORE the peer closes (the whole point vs net/tcp.prn's read-to-EOF),
 *   - the final buffer is exactly first+second, then pull reports closed (-1),
 *   - a refused connection returns -1 fast,
 *   - the line buffer and slots behave,
 *   - a build without PARENA_WITH_TLS reports TLS as unavailable instead of pretending (code 5).
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "test_net_stream_gen.c"

static int listen_loopback(int *port_out) {
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    assert(ls >= 0);
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    assert(bind(ls, (struct sockaddr *)&a, sizeof a) == 0);
    assert(listen(ls, 4) == 0);
    socklen_t al = sizeof a;
    assert(getsockname(ls, (struct sockaddr *)&a, &al) == 0);
    *port_out = ntohs(a.sin_port);
    return ls;
}

static void serve_once(int ls) {
    int c = accept(ls, NULL, NULL);
    char buf[512];
    size_t got = 0;
    while (got < sizeof buf - 1) {
        ssize_t n = read(c, buf + got, sizeof buf - 1 - got);
        if (n <= 0) break;
        got += (size_t)n;
        buf[got] = '\0';
        if (strstr(buf, "\r\n\r\n")) break;
    }
    const char *p1 = "HTTP/1.0 200 OK\r\n\r\nfirst";
    write(c, p1, strlen(p1));
    usleep(250 * 1000);
    const char *p2 = "second";
    write(c, p2, strlen(p2));
    close(c);
}

int main(void) {
    /* 0. no TLS in this build: honest "unavailable", never a fake success */
    assert(stream_connect_tls("127.0.0.1", 1) == -1);
    assert(stream_tls_code() == 5);

    int port = 0;
    int ls = listen_loopback(&port);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        serve_once(ls);
        _exit(0);
    }
    close(ls);

    /* 1. connect + send + incremental pull */
    stream_reset();
    int fd = stream_connect("127.0.0.1", port);
    assert(fd >= 0);
    assert(stream_send(fd, "GET / HTTP/1.0\r\nHost: x\r\n\r\n") == 0);

    const char *part1 = "HTTP/1.0 200 OK\r\n\r\nfirst";
    int saw_partial = 0, closed = 0, spins = 0, saw_idle = 0;
    while (!closed && spins < 600) {
        int r = stream_pull(fd);
        if (r > 0) {
            if (stream_len() == (int)strlen(part1) && strcmp(stream_buf(), part1) == 0) saw_partial = 1;
        } else if (r == 0) {
            saw_idle = 1;
            usleep(10 * 1000);
            spins++;
        } else {
            closed = 1;
        }
    }
    assert(closed && "peer close was never reported");
    assert(saw_partial && "the first part must be readable before the peer closes");
    assert(saw_idle && "pull must return 0 (not block) while nothing has arrived");
    assert(strcmp(stream_buf(), "HTTP/1.0 200 OK\r\n\r\nfirstsecond") == 0);
    assert(stream_len() == (int)strlen("HTTP/1.0 200 OK\r\n\r\nfirstsecond"));
    assert(stream_close(fd) == 0);
    int status = 0;
    waitpid(pid, &status, 0);

    /* 2. reset */
    stream_reset();
    assert(stream_len() == 0);
    assert(stream_buf()[0] == '\0');

    /* 3. refused connection fails fast (a port nothing listens on) */
    int ls2 = listen_loopback(&port);
    close(ls2);
    long long t0 = stream_now_ms();
    assert(stream_connect("127.0.0.1", port) == -1);
    assert(stream_now_ms() - t0 < 2000 && "a refused connect must not wait out the 5 s connect timeout");

    /* 4. line buffer */
    linebuf_clear();
    linebuf_append("http://");
    linebuf_append("a.b");
    assert(linebuf_len() == 10);
    assert(strcmp(linebuf_get(), "http://a.b") == 0);
    linebuf_backspace();
    assert(strcmp(linebuf_get(), "http://a.") == 0);
    linebuf_clear();
    assert(linebuf_len() == 0);

    /* 5. slots + current url */
    slot_set(7, 42);
    assert(slot_get(7) == 42);
    cururl_set("http://example.test/x");
    assert(strcmp(cururl_get(), "http://example.test/x") == 0);

    printf("test_net_stream: ok\n");
    return 0;
}
