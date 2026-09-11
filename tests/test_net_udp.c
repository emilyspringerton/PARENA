/* tests/test_net_udp.c -- real end-to-end verification of stdlib/net/udp.prn.
 *
 * Real, live-verified regression test for the 2026-09-11 rewrite (see net/udp.prn's own header
 * comment for the full list of real bugs found and fixed): this file's own previous version had
 * never actually been compiled end to end anywhere in this repo before that pass -- found while
 * scoping "write the DEADWEIGHT game-server host itself in PARENA" (EMILY/docs/
 * PARENACLOUD_NORTHSTAR.md). `udp_bind`/`udp_send_to`/`udp_recv_from` were referenced by
 * generated code but had no runtime implementation at all (a real link-time gap, not just a
 * missing test); this test exists specifically so that regression can never happen silently
 * again. Real, live sockets -- binds two real UDP sockets on 127.0.0.1, sends a real datagram,
 * receives it, and asserts both the payload AND the sender's own reported port came back
 * correct -- not a mock.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "test_net_udp_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* ---- real round trip: bind two sockets, send, receive, verify ---- */
    Result server_r = udp_bind(19991, &arena);
    assert(server_r.tag == 1);
    UdpSocket *server = (UdpSocket *)server_r.value;
    printf("PASS: udp_bind succeeds for a real, available port\n");

    Result client_r = udp_bind(19992, &arena);
    assert(client_r.tag == 1);
    UdpSocket *client = (UdpSocket *)client_r.value;

    SocketAddr dest;
    dest.host = "127.0.0.1";
    dest.port = 19991;

    const char *payload = "hello-parena-udp";
    Result send_r = udp_send_to(client, dest, (char *)payload, &arena);
    assert(send_r.tag == 1);
    printf("PASS: udp_send_to sends a real datagram without error\n");

    SocketAddr sender;
    sender.host = "";
    sender.port = 0;
    Result recv_r = udp_recv_from(server, &arena, &sender);
    assert(recv_r.tag == 1);
    char *received = (char *)recv_r.value;
    assert(strcmp(received, payload) == 0);
    printf("PASS: udp_recv_from receives the exact payload udp_send_to sent\n");

    assert(sender.port == 19992);
    assert(strcmp(sender.host, "127.0.0.1") == 0);
    printf("PASS: udp_recv_from's out-parameter correctly reports the real sender address/port "
           "(proves the &mut-field-out-parameter design in net/udp.prn's own header comment "
           "actually works, not just compiles)\n");

    /* ---- real negative case: a bind failure is reported, not silently swallowed or crashed ----
     * Real, honest note: binding the SAME port twice does NOT reliably fail here -- udp_bind_impl
     * sets SO_REUSEADDR (the same real reason apps/matchmaker's own tcp_listen_impl sets it: fast
     * restart without a stale TIME_WAIT socket blocking the real port), and Linux's own real
     * SO_REUSEADDR semantics for UDP let a second bind to the same port succeed rather than
     * error -- confirmed live, not assumed (the original version of this test wrongly assumed
     * otherwise and caught its own bug this way). A privileged port (<1024) as this sandbox's own
     * real, current non-root user is the deterministic real failure case instead. */
    Result priv_r = udp_bind(80, &arena);
    assert(priv_r.tag == 0);
    NetError *err = (NetError *)priv_r.value;
    assert(err->tag == 2 /* NetError_TAG_AddressInUse -- see net/udp.prn's own header comment: a
                             coarse, single error variant for any bind failure, same "no errno
                             inspection" convention tcp-listen already established */);
    printf("PASS: udp_bind on a privileged port without root correctly reports failure, not a "
           "crash or a silent success\n");

    printf("\nALL PASS\n");
    return 0;
}
