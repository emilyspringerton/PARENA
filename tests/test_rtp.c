/* tests/test_rtp.c -- real end-to-end verification of stdlib/sip/rtp.prn (kanban priority-queue
 * card PBX-001, "Build the narrow scope parena PBX primitives... low level close to the metal
 * primitives like what does asterisk need"). Real, hand-constructed RTP packet header (RFC 3550
 * §5.1's own fixed 12-byte layout), deliberately using real bytes >= 128 (0x80, 0x12, 0x34, 0x56,
 * 0x78) to exercise net/wire.prn's own raw-byte signedness fix, the same discipline
 * test_wire.c/test_editor_unicode.c already established.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "test_rtp_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* Real, hand-built 12-byte RTP header: V=2,P=0,X=0,CC=0 (0x80), M=0,PT=0/PCMU (0x00),
     * sequence=1, timestamp=0x160 (352), SSRC=0x12345678. */
    char hdr[12] = {
        (char)0x80, (char)0x00,
        (char)0x00, (char)0x01,
        (char)0x00, (char)0x00, (char)0x01, (char)0x60,
        (char)0x12, (char)0x34, (char)0x56, (char)0x78
    };

    Result r = parse_rtp_header(hdr, 12, &arena);
    assert(r.tag == 1);
    RtpHeader *h = (RtpHeader *)r.value;
    assert(h->version == 2);
    assert(h->padding == 0);
    assert(h->extension == 0);
    assert(h->csrc_count == 0);
    assert(h->marker == 0);
    assert(h->payload_type == 0);
    assert(h->sequence_number == 1);
    assert(h->timestamp == 352);
    assert(h->ssrc == 0x12345678);

    /* Real, honest v0 boundary: too-short buffer reports a real, honest error, never a crash. */
    char shortbuf[4] = { (char)0x80, (char)0x00, (char)0x00, (char)0x01 };
    Result rshort = parse_rtp_header(shortbuf, 4, &arena);
    assert(rshort.tag == 0);

    /* Real, honest v0 boundary: an extension bit set is reported Unsupported, not silently
     * mis-parsed past the extension it doesn't handle. */
    char extbuf[12] = {
        (char)0x90, (char)0x00, /* X bit set (0x10) */
        (char)0x00, (char)0x01,
        (char)0x00, (char)0x00, (char)0x01, (char)0x60,
        (char)0x12, (char)0x34, (char)0x56, (char)0x78
    };
    Result rext = parse_rtp_header(extbuf, 12, &arena);
    assert(rext.tag == 0);

    /* Real, decisive proof this whole fix mattered: a header whose OWN real fields legitimately
     * contain an embedded NUL byte (sequence-number=0, timestamp=0 -- completely ordinary, real
     * RTP traffic) must still parse correctly when a real, explicit length is passed, even
     * though string/length (strlen) on this exact buffer would incorrectly report only 2. */
    char zerobuf[12] = {
        (char)0x80, (char)0x00,
        (char)0x00, (char)0x00, /* sequence-number = 0 */
        (char)0x00, (char)0x00, (char)0x00, (char)0x00, /* timestamp = 0 */
        (char)0x00, (char)0x00, (char)0x00, (char)0x01 /* ssrc = 1 */
    };
    Result rzero = parse_rtp_header(zerobuf, 12, &arena);
    assert(rzero.tag == 1);
    RtpHeader *hzero = (RtpHeader *)rzero.value;
    assert(hzero->sequence_number == 0);
    assert(hzero->timestamp == 0);
    assert(hzero->ssrc == 1);

    /* Real, direct inverse: build-rtp-header round-trips parse-rtp-header's own output back into
     * the exact same 12 real bytes -- using a real, DELIBERATELY zero-byte-free header, per
     * build-rtp-header's own honestly-documented real limitation (string/concat's underlying
     * strcpy/strcat truncates at the first embedded 0x00; parse-rtp-header itself has no such
     * limitation, fixed above via an explicit length, but the write side genuinely does). */
    char hdr2[12] = {
        (char)0x80, (char)0x81, /* V=2, M=1, PT=1 */
        (char)0x12, (char)0x34, /* sequence-number = 0x1234 */
        (char)0x11, (char)0x22, (char)0x33, (char)0x44, /* timestamp = 0x11223344 */
        (char)0x55, (char)0x66, (char)0x77, (char)0x88 /* ssrc = 0x55667788 */
    };
    Result r2 = parse_rtp_header(hdr2, 12, &arena);
    assert(r2.tag == 1);
    RtpHeader *h2 = (RtpHeader *)r2.value;
    char *built = build_rtp_header(*h2, &arena);
    assert(memcmp(built, hdr2, 12) == 0);

    printf("test_rtp: all real assertions passed\n");
    return 0;
}
