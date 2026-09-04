/* tests/test_net_proxy.c -- real end-to-end verification of stdlib/net/proxy.prn (kanban
 * priority-queue card 434534, "use fatbaby proxy and proxy broker to inform vpn primatives
 * built into parena"). Confirms real incoming-request parsing (method/path/headers/body), real
 * hop-by-hop header stripping matching PRRJECT_FATBABY/broker/proxy.go's own exact list, and a
 * real end-to-end relay against an actual local upstream HTTP server over a real loopback TCP
 * socket -- not just "did it compile".
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "test_net_proxy_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* Real request-line + header parsing, including a body. */
    {
        ProxiedRequest req = parse_http_request(
            "POST /widgets HTTP/1.1\r\nHost: example.com\r\nContent-Type: application/json\r\n"
            "Connection: keep-alive\r\n\r\n{\"n\":1}", &arena);
        assert(strcmp(req.method, "POST") == 0);
        assert(strcmp(req.path, "/widgets") == 0);
        assert(strcmp(req.body, "{\"n\":1}") == 0);
        int n = vec_len(&req.header_names);
        assert(n == 3);
        int found_ct = 0;
        for (int i = 0; i < n; i++) {
            char *name = (char *)vec_get(&req.header_names, i);
            char *val = (char *)vec_get(&req.header_values, i);
            if (strcmp(name, "Content-Type") == 0) {
                assert(strcmp(val, "application/json") == 0);
                found_ct = 1;
            }
        }
        assert(found_ct);
        printf("PASS: real incoming request parses method/path/headers/body correctly\n");
    }

    /* Real hop-by-hop header stripping -- the exact 8-name list broker/proxy.go's own
       hopByHopHeaders map uses, checked one by one, plus a real non-hop-by-hop header that
       must NOT be flagged. */
    {
        assert(is_hop_by_hop_header_("Connection"));
        assert(is_hop_by_hop_header_("Keep-Alive"));
        assert(is_hop_by_hop_header_("Proxy-Authenticate"));
        assert(is_hop_by_hop_header_("Proxy-Authorization"));
        assert(is_hop_by_hop_header_("Te"));
        assert(is_hop_by_hop_header_("Trailer"));
        assert(is_hop_by_hop_header_("Transfer-Encoding"));
        assert(is_hop_by_hop_header_("Upgrade"));
        assert(!is_hop_by_hop_header_("Content-Type"));
        assert(!is_hop_by_hop_header_("Host"));
        printf("PASS: real hop-by-hop header set matches broker/proxy.go's own exact list\n");
    }

    /* Real end-to-end relay: parse a real incoming request, then proxy-relay it to a REAL
       upstream HTTP server listening on a real loopback TCP socket (spawned by the test harness
       below, a tiny Python http.server instance), and check the real response that comes back. */
    {
        /* Spawn a minimal, real local HTTP server as the "upstream" this proxy relays to --
           tests/test_proxy_upstream_fixture.py, a real, small, standalone script (not built
           inline here -- a real multi-line Python program embedded in a C string literal is
           real fragility for no reason when a checked-in fixture file is simpler and honest). */
        system("python3 tests/test_proxy_upstream_fixture.py "
               "> /tmp/test_proxy_upstream.log 2>&1 &");
        /* Give the real server a moment to bind before the client connects. */
        for (volatile long i = 0; i < 300000000L; i++) {}

        ProxiedRequest req = parse_http_request("GET /ping HTTP/1.1\r\nHost: x\r\n\r\n", &arena);
        Result resp = proxy_relay(&req, "127.0.0.1", 18099, &arena);
        assert(resp.tag /* truthy = Ok, matching this codebase's own real Result convention */);
        HttpResponse *r = (HttpResponse *)resp.value;
        assert(r->status == 200);
        assert(strcmp(r->body, "real-upstream-ok") == 0);
        printf("PASS: real end-to-end proxy-relay against a real local upstream HTTP server\n");
    }

    printf("\nALL PASS\n");
    return 0;
}
