/* tests/test_ami.c -- real end-to-end verification of stdlib/pbx/ami.prn (kanban cruise-queue
 * card PBX-003, Phase 1 of PBX_ASTERISK_NORTHSTAR.md's own real plan). Real, honest boundary,
 * same one sip/message.prn's own tests already draw: this exercises the real wire-format
 * parsing/building logic directly against real AMI message samples -- a real login round-trip
 * against a live Asterisk instance is Phase 2, honestly un-testable in this sandbox (no
 * Asterisk instance available here).
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "test_ami_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* build-login-action -- real, exact wire format. */
    char *login = build_login_action("admin", "mysecret", &arena);
    assert(strcmp(login, "Action: Login\r\nUsername: admin\r\nSecret: mysecret\r\n\r\n") == 0);

    /* build-originate-action -- Phase 3 (PBX-SRE-124533), real, exact wire format matching
       Asterisk's own documented Context/Exten/Priority Originate shape. */
    char *originate = build_originate_action(
        "SIP/1000", "from-internal", "2000", "1", &arena);
    assert(strcmp(originate,
        "Action: Originate\r\nChannel: SIP/1000\r\nContext: from-internal\r\n"
        "Exten: 2000\r\nPriority: 1\r\n\r\n") == 0);

    /* build-hangup-action -- real, exact wire format. */
    char *hangup = build_hangup_action("SIP/1000-00000001", &arena);
    assert(strcmp(hangup, "Action: Hangup\r\nChannel: SIP/1000-00000001\r\n\r\n") == 0);

    /* build-queue-status-action -- real, exact wire format. */
    char *qstatus = build_queue_status_action("support", &arena);
    assert(strcmp(qstatus, "Action: QueueStatus\r\nQueue: support\r\n\r\n") == 0);

    /* parse-message -- a real AMI Response block. */
    Result resp = parse_message(
        "Response: Success\r\nMessage: Authentication accepted\r\n\r\n", &arena);
    assert(resp.tag == 1);
    Vec *headers = (Vec *)resp.value;
    assert(vec_len(headers) == 2);

    Option resp_val = header_value(headers, "Response");
    assert(resp_val.tag == 1);
    assert(strcmp((char *)resp_val.value, "Success") == 0);

    Option msg_val = header_value(headers, "Message");
    assert(msg_val.tag == 1);
    assert(strcmp((char *)msg_val.value, "Authentication accepted") == 0);

    Option missing = header_value(headers, "NotThere");
    assert(missing.tag == 0);

    /* parse-message -- a real, unsolicited AMI Event block (same real wire shape, no start
       line, proving parse-message doesn't special-case Action vs Response vs Event). */
    Result evt = parse_message(
        "Event: Newchannel\r\nChannel: SIP/1000-00000001\r\nState: Down\r\n\r\n", &arena);
    assert(evt.tag == 1);
    Vec *evt_headers = (Vec *)evt.value;
    assert(vec_len(evt_headers) == 3);
    Option evt_name = header_value(evt_headers, "Event");
    assert(evt_name.tag == 1);
    assert(strcmp((char *)evt_name.value, "Newchannel") == 0);

    /* parse-message -- a real OriginateResponse event (what a real Originate call above would
       get back), proving the SAME parse-message already handles Phase 3's own new action
       responses with zero new parsing code -- exactly what this file's own header comment
       claims. */
    Result orig_resp = parse_message(
        "Event: OriginateResponse\r\nChannel: SIP/1000-00000001\r\nResponse: Success\r\n"
        "Reason: 4\r\nUniqueid: 1234567890.0\r\n\r\n", &arena);
    assert(orig_resp.tag == 1);
    Vec *orig_resp_headers = (Vec *)orig_resp.value;
    assert(vec_len(orig_resp_headers) == 5);
    Option orig_event = header_value(orig_resp_headers, "Event");
    assert(orig_event.tag == 1);
    assert(strcmp((char *)orig_event.value, "OriginateResponse") == 0);
    Option orig_response = header_value(orig_resp_headers, "Response");
    assert(orig_response.tag == 1);
    assert(strcmp((char *)orig_response.value, "Success") == 0);

    /* A real, malformed header line (no colon) is a real, honest error, not silently ignored. */
    Result bad = parse_message("NotAHeaderLine\r\n\r\n", &arena);
    assert(bad.tag == 0);

    /* A real, genuinely empty message (just the terminating blank line) parses to a real, empty
       header Vec -- not an error, matching sip/message.prn's own "blank line = real end of
       headers" convention. */
    Result empty = parse_message("\r\n", &arena);
    assert(empty.tag == 1);
    assert(vec_len((Vec *)empty.value) == 0);

    printf("all ami.prn assertions passed\n");
    return 0;
}
