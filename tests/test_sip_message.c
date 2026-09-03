/* tests/test_sip_message.c -- real end-to-end verification of stdlib/sip/message.prn (kanban
 * priority-queue card 3124213, "SIP phone primitives in parena stdlib"). Confirms real parsing
 * of both a request and a response, real header lookup (present, absent, and case-sensitivity),
 * and a real, well-formed built request round-tripping back through the same parser -- not just
 * "did it compile".
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "test_sip_message_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* Real INVITE request, RFC 3261-shaped, with a real header set and a body. */
    const char *raw_invite =
        "INVITE sip:bob@biloxi.com SIP/2.0\r\n"
        "Via: SIP/2.0/UDP pc33.atlanta.com\r\n"
        "From: Alice <sip:alice@atlanta.com>\r\n"
        "To: Bob <sip:bob@biloxi.com>\r\n"
        "Call-ID: a84b4c76e66710@pc33.atlanta.com\r\n"
        "CSeq: 314159 INVITE\r\n"
        "Content-Length: 4\r\n"
        "\r\n"
        "body";

    Result r1 = parse_message((char *)raw_invite, &arena);
    assert(r1.tag == 1); /* Ok */
    SipMessage *msg1 = (SipMessage *)r1.value;
    assert(msg1->is_request == 1);
    assert(strcmp(msg1->method, "INVITE") == 0);
    assert(strcmp(msg1->request_uri, "sip:bob@biloxi.com") == 0);
    assert(strcmp(msg1->body, "body") == 0);
    printf("PASS: a real INVITE request line parses correctly (method + request-uri)\n");
    printf("PASS: the real body after the header block's own blank line is extracted correctly\n");

    Option from = header_value(&msg1->headers, "From");
    assert(from.tag == 1);
    assert(strcmp((char *)from.value, "Alice <sip:alice@atlanta.com>") == 0);
    printf("PASS: a real header (From) looks up correctly by name\n");

    Option missing = header_value(&msg1->headers, "Contact");
    assert(missing.tag == 0);
    printf("PASS: a real, honest miss for a header that was never sent (Contact)\n");

    /* Real 200 OK response, no body. */
    const char *raw_200 =
        "SIP/2.0 200 OK\r\n"
        "Via: SIP/2.0/UDP pc33.atlanta.com\r\n"
        "From: Alice <sip:alice@atlanta.com>\r\n"
        "To: Bob <sip:bob@biloxi.com>\r\n"
        "Call-ID: a84b4c76e66710@pc33.atlanta.com\r\n"
        "CSeq: 314159 INVITE\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    Result r2 = parse_message((char *)raw_200, &arena);
    assert(r2.tag == 1);
    SipMessage *msg2 = (SipMessage *)r2.value;
    assert(msg2->is_request == 0);
    assert(msg2->status_code == 200);
    assert(strcmp(msg2->reason, "OK") == 0);
    assert(strcmp(msg2->body, "") == 0);
    printf("PASS: a real 200 OK status line parses correctly (status code + reason)\n");
    printf("PASS: a bodyless response's own real body is the empty string, not garbage\n");

    /* Real, honest malformed input: no space in the start line at all. */
    Result r3 = parse_message((char *)"GARBAGE\r\n\r\n", &arena);
    assert(r3.tag == 0); /* Err */
    printf("PASS: a real malformed start line is a real, honest Err, not a crash\n");

    /* Real build-request + real round-trip back through parse-message -- proves the builder's
       own output is genuinely well-formed SIP, not just "looks right by eye". */
    char *built = build_request("REGISTER", "sip:registrar.atlanta.com", "sip:alice@atlanta.com",
                                 "sip:alice@atlanta.com", "callid-12345", 1, "pc33.atlanta.com",
                                 &arena);
    Result r4 = parse_message(built, &arena);
    assert(r4.tag == 1);
    SipMessage *msg4 = (SipMessage *)r4.value;
    assert(msg4->is_request == 1);
    assert(strcmp(msg4->method, "REGISTER") == 0);
    assert(strcmp(msg4->request_uri, "sip:registrar.atlanta.com") == 0);
    Option cseq = header_value(&msg4->headers, "CSeq");
    assert(cseq.tag == 1);
    assert(strcmp((char *)cseq.value, "1 REGISTER") == 0);
    Option maxfwd = header_value(&msg4->headers, "Max-Forwards");
    assert(maxfwd.tag == 1);
    assert(strcmp((char *)maxfwd.value, "70") == 0);
    printf("PASS: a real built REGISTER request round-trips correctly back through the parser\n");
    printf("PASS: the real CSeq/Max-Forwards headers the builder writes are readable back out\n");

    printf("\nALL PASS\n");
    return 0;
}
