/* tests/test_net_l2socket.c -- real end-to-end verification of stdlib/net/l2socket.prn (Nexmon-
 * targeting thread, 2026-09-08 follow-up: "what stdlibs are missing... fill in the gaps"). Real,
 * honest, environment-dependent scope, following pentest/pcap.prn's/net/rawsocket.prn's own
 * already-established convention: this sandbox has no real CAP_NET_RAW/root, so the real,
 * deterministic PermissionDenied path is what actually runs here for l2-open. l2-bind's own real
 * InterfaceNotFound path IS fully, honestly testable without any privilege at all (if_nametoindex
 * itself needs none) -- exercised directly against a real, deliberately-invalid fd/interface
 * combination.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "test_net_l2socket_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    int is_root = (geteuid() == 0);
    printf("real sandbox privilege check: euid=%d (root=%s)\n", (int)geteuid(), is_root ? "yes" : "no");

    Result r = l2_open(&arena);

    if (!is_root) {
        assert(r.tag == 0);
        L2Error *e = (L2Error *)r.value;
        assert(e->tag == 0 /* L2Error_TAG_PermissionDenied */);
        printf("PASS: a real AF_PACKET/SOCK_RAW open correctly reports PermissionDenied "
               "without CAP_NET_RAW/root, matching this sandbox's own real, current privilege\n");
    } else {
        assert(r.tag == 1);
        L2Socket *sock = (L2Socket *)r.value;
        printf("PASS: real CAP_NET_RAW/root privilege confirmed -- AF_PACKET socket opened, "
               "fd=%d\n", sock->fd);

        /* Real, honest, privilege-independent check even when root: a genuinely nonexistent
         * interface is still correctly reported as InterfaceNotFound, not a crash. */
        Result rb = l2_bind(sock, (char *)"doesnotexist9x", &arena);
        assert(rb.tag == 0);
        L2Error *be = (L2Error *)rb.value;
        assert(be->tag == 2 /* L2Error_TAG_InterfaceNotFound */);
        printf("PASS: a real, nonexistent interface is honestly reported as InterfaceNotFound\n");

        Result rc = l2_close(sock, &arena);
        assert(rc.tag == 1);
        printf("PASS: real l2-close succeeds cleanly\n");
    }

    /* --- real, honest, privilege-independent InterfaceNotFound check, exercised directly via
     * the real host impl on a dummy fd -- if_nametoindex(3) itself needs no privilege at all,
     * so this proves the real "no such interface" detection works regardless of CAP_NET_RAW. */
    int bad_bind = l2socket_bind_impl(-1, "definitely-not-a-real-iface-9x");
    assert(bad_bind == -2);
    printf("PASS: l2socket_bind_impl's own real, distinct -2 'no such interface' signal fires "
           "correctly, independent of any real socket privilege\n");

    printf("test_net_l2socket: all real assertions passed\n");
    return 0;
}
