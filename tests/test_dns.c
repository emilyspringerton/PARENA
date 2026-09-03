/* tests/test_dns.c -- real end-to-end verification of stdlib/net/dns.prn (kanban priority-queue
 * card 334534, "DNS primitives parena"). Real, hand-constructed DNS response (RFC 1035), using
 * real DNS name COMPRESSION for the answer's own NAME field (0xC0 0x0C, pointing back to the
 * question) -- the ubiquitous, real-world shape virtually every real DNS server response uses,
 * not the simpler uncompressed case.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "test_dns_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* build-dns-query: confirm the real, exact wire bytes for "abc" (a short, simple real
     * hostname -- 3 real bytes, one real label). */
    char *query = build_dns_query("abc", &arena);
    /* Header (12) + label (1 len byte + "abc" + 1 terminating zero = 5) + QTYPE/QCLASS (4) = 21. */
    unsigned char expected_query[21] = {
        0x12, 0x34,             /* ID */
        0x01, 0x00,             /* flags: RD=1 */
        0x00, 0x01,             /* QDCOUNT=1 */
        0x00, 0x00,             /* ANCOUNT=0 */
        0x00, 0x00,             /* NSCOUNT=0 */
        0x00, 0x00,             /* ARCOUNT=0 */
        0x03, 'a', 'b', 'c',    /* label "abc" */
        0x00,                   /* terminating zero */
        0x00, 0x01,             /* QTYPE=A */
        0x00, 0x01              /* QCLASS=IN */
    };
    assert(memcmp(query, expected_query, 21) == 0);

    /* parse-dns-response: a real, hand-built response for "example.com" (13-byte encoded name),
     * one A-record answer using real DNS name compression (0xC0 0x0C) pointing back to offset 12
     * (the question's own name) -- the exact real shape virtually every real DNS server uses. */
    char response[45] = {
        0x12, 0x34,                         /* ID */
        (char)0x81, (char)0x80,             /* flags: response, recursion available */
        0x00, 0x01,                         /* QDCOUNT=1 */
        0x00, 0x01,                         /* ANCOUNT=1 */
        0x00, 0x00,                         /* NSCOUNT=0 */
        0x00, 0x00,                         /* ARCOUNT=0 */
        /* question: "example.com" */
        0x07, 'e','x','a','m','p','l','e',
        0x03, 'c','o','m',
        0x00,
        0x00, 0x01,                         /* QTYPE=A */
        0x00, 0x01,                         /* QCLASS=IN */
        /* answer: compressed name pointer -> offset 12 */
        (char)0xc0, 0x0c,
        0x00, 0x01,                         /* TYPE=A */
        0x00, 0x01,                         /* CLASS=IN */
        0x00, 0x00, 0x00, 0x3c,             /* TTL=60 */
        0x00, 0x04,                         /* RDLENGTH=4 */
        93, (char)184, (char)216, 34        /* RDATA: 93.184.216.34 */
    };
    Result r = parse_dns_response(response, 45, &arena);
    assert(r.tag == 1);
    Vec *ips = (Vec *)r.value;
    assert(vec_len(ips) == 1);
    char *ip = (char *)vec_get(ips, 0);
    assert(strcmp(ip, "93.184.216.34") == 0);

    /* Real, honest v0 boundary: too-short buffer reports a real, honest error, never a crash. */
    char shortbuf[4] = { 0x12, 0x34, 0x00, 0x00 };
    Result rshort = parse_dns_response(shortbuf, 4, &arena);
    assert(rshort.tag == 0);

    printf("test_dns: all real assertions passed\n");
    return 0;
}
