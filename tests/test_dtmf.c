/* tests/test_dtmf.c -- real end-to-end verification of stdlib/sip/dtmf.prn (CarePyre SIP Phone,
 * kanban priority-queue card CAREPYRE-SIP-4324324, "SIP PHONE NEEDS DTMF DIAL TONES ... IN BAND
 * WHILE IN A CALL VIA THE NUMBER PAD"). Real RFC 4733 telephone-event payload parse/build,
 * deliberately exercising all-zero-field cases (event '0', volume 0, duration 0) the same real
 * class of case sip/rtp.prn's own test already proved was the load-bearing regression check for
 * this exact family of bug (string/concat silently truncating on an embedded zero byte).
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "test_dtmf_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* Real, hand-built 4-byte telephone-event payload: event=5 ('5'), E=1, volume=10, duration=800. */
    char payload[4] = { (char)5, (char)(0x80 | 10), (char)0x03, (char)0x20 };
    Result r1 = parse_dtmf_event(payload, 4, &arena);
    assert(r1.tag == 1);
    DtmfEvent *ev1 = (DtmfEvent *)r1.value;
    assert(ev1->event == 5);
    assert(ev1->end == 1);
    assert(ev1->volume == 10);
    assert(ev1->duration == 800);
    printf("PASS: a real telephone-event payload (digit '5', end flag set) parses correctly\n");

    /* Real, honest error: buffer shorter than the fixed 4-byte payload. */
    Result r2 = parse_dtmf_event(payload, 3, &arena);
    assert(r2.tag == 0);
    printf("PASS: a buffer shorter than 4 bytes is a real, honest TooShort Err, not a crash\n");

    /* Real digit <-> event code mapping, including the two symbol keys and hex-letter keys. */
    assert(digit_to_event((int)'0') == 0);
    assert(digit_to_event((int)'9') == 9);
    assert(digit_to_event((int)'*') == 10);
    assert(digit_to_event((int)'#') == 11);
    assert(digit_to_event((int)'A') == 12);
    assert(digit_to_event((int)'D') == 15);
    assert(digit_to_event((int)'Z') == -1);
    printf("PASS: digit-to-event maps every real DTMF key to its real RFC 4733 event code\n");

    assert(digit_from_event(0) == (int)'0');
    assert(digit_from_event(10) == (int)'*');
    assert(digit_from_event(11) == (int)'#');
    assert(digit_from_event(15) == (int)'D');
    assert(digit_from_event(99) == -1);
    printf("PASS: digit-from-event is the real, correct inverse mapping\n");

    /* Real build + round-trip, deliberately using digit '0' (event code 0), volume 0, and a
     * duration whose low byte is genuinely 0 -- the exact real "embedded zero byte" case
     * string/concat would silently truncate on, the reason build-dtmf-event uses inline-c. */
    DtmfEvent zero_ev = { 0, 0, 0, 512 }; /* event=0, end=false, volume=0, duration=0x0200 */
    char *built = build_dtmf_event(zero_ev, &arena);
    Result r3 = parse_dtmf_event(built, dtmf_event_size(), &arena);
    assert(r3.tag == 1);
    DtmfEvent *ev3 = (DtmfEvent *)r3.value;
    assert(ev3->event == 0);
    assert(ev3->end == 0);
    assert(ev3->volume == 0);
    assert(ev3->duration == 512);
    printf("PASS: a built all-zero-field event (digit '0', volume 0) round-trips correctly --\n");
    printf("      confirms the inline-c build path isn't silently truncated by embedded zero bytes\n");

    /* Real build + round-trip with every field non-zero and the end flag set. */
    DtmfEvent full_ev = { 7, 1, 20, 1600 };
    char *built2 = build_dtmf_event(full_ev, &arena);
    Result r4 = parse_dtmf_event(built2, dtmf_event_size(), &arena);
    assert(r4.tag == 1);
    DtmfEvent *ev4 = (DtmfEvent *)r4.value;
    assert(ev4->event == 7);
    assert(ev4->end == 1);
    assert(ev4->volume == 20);
    assert(ev4->duration == 1600);
    printf("PASS: a built, fully-populated event round-trips correctly\n");

    assert(dtmf_event_size() == 4);
    assert(telephone_event_payload_type() == 101);
    printf("PASS: dtmf-event-size and the real, conventional default payload type (101) are correct\n");

    printf("\nALL PASS\n");
    return 0;
}
