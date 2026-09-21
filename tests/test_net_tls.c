/* tests/test_net_tls.c -- real end-to-end verification of stdlib/net/tls.prn (S508e).
 *
 * Founder real-time, DEADWEIGHT's client had zero TLS support and defaulted to a plaintext IDUNA
 * URL -- a real shipping bug for a client that reaches the public internet. Explicit pushback
 * given and accepted before any of this was written: never hand-roll TLS crypto in an immature
 * DSL with no cryptographic primitives. This binds the real, audited mbedTLS library via FFI
 * instead (runtime/parena_runtime.h's own PARENA_WITH_TLS block) -- this test is the real,
 * live proof that binding actually works, not just that it compiles.
 *
 * Real, live TLS handshake against a real, reachable HTTPS server (defaults to
 * https://okemily.com -- this monorepo's own real production front door, the actual server
 * DEADWEIGHT's client needs to reach; override via TLS_TEST_HOST/TLS_TEST_PORT env vars for a
 * different target). Asserts a real HTTP/1.x response line came back over the encrypted
 * connection -- not a mock, not localhost.
 *
 * Honest, deliberate limitations of this specific test, not the underlying binding:
 *   - Requires a real reachable network and a real, currently-valid certificate on the target
 *     host -- this test can fail for reasons entirely outside PARENA/DEADWEIGHT (target host
 *     down, cert expired, network partition), same class of external dependency
 *     test_net_udp.c's own real live sockets already accept for loopback traffic.
 *   - Does NOT test the Windows/mingw cross-build path -- runtime/parena_runtime.h's own header
 *     comment on PARENA_TLS_CA_BUNDLE_PATH already names that as real, separate, not-yet-done
 *     follow-up work (Windows has no equivalent single-file CA bundle at a fixed path).
 *   - Does NOT test the "wrong hostname correctly rejected" negative case inline (verified once,
 *     by hand, via a standalone throwaway harness before this file was written) -- a real,
 *     committed negative-path test is a reasonable follow-up, not done here to keep this file's
 *     own network usage to one connection.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_net_tls_gen.c"

int main(void) {
    const char *host_env = getenv("TLS_TEST_HOST");
    const char *port_env = getenv("TLS_TEST_PORT");
    const char *host = host_env && *host_env ? host_env : "okemily.com";
    int port = port_env && *port_env ? atoi(port_env) : 443;

    Arena a;
    arena_init(&a);

    Result r = tls_connect((char *)host, port, &a);
    if (!r.tag) {
        fprintf(stderr, "test_net_tls: tls_connect(%s:%d) failed -- unreachable, or the runtime's own real CA-bundle-load/handshake/cert-verify path did (see runtime/parena_runtime.h's tls_connect_impl)\n", host, port);
        return 1;
    }
    TlsStream *s = (TlsStream *)r.value;
    printf("test_net_tls: connected+handshook to %s:%d, handle=%d\n", host, port, s->handle);

    char req[256];
    snprintf(req, sizeof req, "GET / HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", host);
    Result wr = tls_write(s, req, &a);
    assert(wr.tag && "tls_write over the real encrypted connection failed");

    Result rr = tls_read(s, &a);
    assert(rr.tag && "tls_read over the real encrypted connection failed");
    char *body = (char *)rr.value;
    assert(strncmp(body, "HTTP/1.", 7) == 0 && "response did not start with a real HTTP status line");
    printf("test_net_tls: real HTTP response over TLS, first line: %.40s\n", body);

    Result cr = tls_close(s, &a);
    assert(cr.tag && "tls_close failed");

    printf("test_net_tls: PASS (real TLS handshake + cert verification + HTTP round-trip against %s:%d)\n", host, port);
    return 0;
}
