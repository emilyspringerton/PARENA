/* tests/test_net_unixsocket.c -- real end-to-end verification of stdlib/net/unixsocket.prn
 * (S498, founder real-time: "we need to put in PARENA primatives for MSSQL and double down on
 * all the unix socket stuff and raw socket stuff"). Unlike net/rawsocket.prn/net/l2socket.prn,
 * a Unix domain stream socket needs no CAP_NET_RAW/root -- every path below is a real, live,
 * unprivileged round trip over an actual AF_UNIX socket file in /tmp, not a permission-gated
 * skip.
 *
 * This test's own first draft found a real, genuine bug in unixsocket_listen_impl's original
 * design (a "probe connect() to check staleness before unlinking" scheme): if the existing path
 * WAS live, that probe's own connect(2) call created a real, completed connection sitting in the
 * live listener's own accept(2) backlog, immediately abandoned -- corrupting real FIFO accept
 * order for whatever process actually owns that listener. Caught live by this exact test (the
 * "second listen on the same live path" check immediately followed by a real connect+accept+
 * read/write round trip in the SAME process) -- runtime/parena_runtime.h's own unixsocket_listen_
 * impl header comment has the full story. Fixed by removing the probe entirely: bind(2) either
 * succeeds or reports AddressInUse, honestly, no automatic stale-socket recovery attempted.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "test_net_unixsocket_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    const char *sock_path = "/tmp/parena_test_net_unixsocket.sock";
    unlink(sock_path);

    Result lr = unix_listen((char *)sock_path, &arena);
    assert(lr.tag == 1);
    UnixListener *listener = (UnixListener *)lr.value;
    printf("PASS: unix-listen opened a real AF_UNIX listener, fd=%d\n", listener->fd);

    /* A second listen on the SAME live path must fail, honestly -- proves this doesn't silently
     * steal/rebind a path a real, already-running peer owns. */
    Result lr2 = unix_listen((char *)sock_path, &arena);
    assert(lr2.tag == 0);
    UnixSocketError *lr2_err = (UnixSocketError *)lr2.value;
    assert(lr2_err->tag == 1 /* UnixSocketError_TAG_AddressInUse */);
    printf("PASS: a second unix-listen on the same live path correctly reports AddressInUse, "
           "not hijacked\n");

    Result cr = unix_connect((char *)sock_path, &arena);
    assert(cr.tag == 1);
    UnixStream *client = (UnixStream *)cr.value;
    printf("PASS: unix-connect opened a real client stream, fd=%d\n", client->fd);

    Result ar = unix_accept(listener, &arena);
    assert(ar.tag == 1);
    UnixStream *server_side = (UnixStream *)ar.value;
    printf("PASS: unix-accept accepted the real, correct connection, fd=%d\n", server_side->fd);

    Result wr = unix_write(client, (char *)"hello unix socket", &arena);
    assert(wr.tag == 1);

    Result rr = unix_read(server_side, &arena);
    assert(rr.tag == 1);
    char *received = (char *)rr.value;
    assert(strcmp(received, "hello unix socket") == 0);
    printf("PASS: real bytes round-tripped client->server over a real AF_UNIX stream socket: "
           "\"%s\"\n", received);

    /* Round-trip the OTHER direction too -- a stream socket is full-duplex. */
    Result wr2 = unix_write(server_side, (char *)"reply from server", &arena);
    assert(wr2.tag == 1);
    Result rr2 = unix_read(client, &arena);
    assert(rr2.tag == 1);
    char *received2 = (char *)rr2.value;
    assert(strcmp(received2, "reply from server") == 0);
    printf("PASS: real bytes round-tripped server->client too: \"%s\"\n", received2);

    Result ccr = unix_close(client, &arena);
    assert(ccr.tag == 1);
    Result csr = unix_close(server_side, &arena);
    assert(csr.tag == 1);
    printf("PASS: real unix-close succeeds cleanly on both ends\n");

    /* Real, honest, privilege-independent failure paths, exercised directly via the host impls:
     * connecting to a path nothing is listening on, and a path too long for sockaddr_un.sun_path
     * (108 bytes on Linux). */
    int bad_connect = unixsocket_connect_impl("/tmp/parena_test_net_unixsocket_nothing_here.sock");
    assert(bad_connect < 0);
    printf("PASS: connecting to a path with no listener correctly fails\n");

    char too_long[300];
    memset(too_long, 'x', sizeof too_long - 1);
    too_long[sizeof too_long - 1] = '\0';
    int bad_listen = unixsocket_listen_impl(too_long);
    assert(bad_listen < 0);
    printf("PASS: a path longer than sizeof(sun_path) is correctly rejected, not truncated "
           "silently\n");

    unlink(sock_path);
    printf("test_net_unixsocket: all real assertions passed\n");
    return 0;
}
