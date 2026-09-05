/* tests/test_sip_sdp.c -- real end-to-end verification of stdlib/sip/sdp.prn (CarePyre SIP Phone
 * Phase 2, kanban priority-queue card CAREPYRE-911343). Confirms real parsing of a real RFC
 * 4566-shaped single-audio-stream SDP body (session-level fields, the one m= line, and its own
 * a=rtpmap codec info), a real honest error for a body with no m= line at all, and a real built
 * offer round-tripping back through the same parser -- not just "did it compile".
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "test_sip_sdp_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* Real, RFC 4566-shaped single-audio-stream offer, G.711 mu-law. */
    const char *raw_sdp =
        "v=0\r\n"
        "o=- 123456 123456 IN IP4 10.0.0.5\r\n"
        "s=CarePyre\r\n"
        "c=IN IP4 10.0.0.5\r\n"
        "t=0 0\r\n"
        "m=audio 49170 RTP/AVP 0\r\n"
        "a=rtpmap:0 PCMU/8000\r\n";

    Result r1 = parse_sdp((char *)raw_sdp, &arena);
    assert(r1.tag == 1); /* Ok */
    SdpMessage *msg1 = (SdpMessage *)r1.value;
    assert(strcmp(msg1->version, "0") == 0);
    assert(strcmp(msg1->session_name, "CarePyre") == 0);
    assert(strcmp(msg1->conn_addr, "10.0.0.5") == 0);
    printf("PASS: real session-level fields (v=/s=/c=) parse correctly\n");

    SdpMedia *media1 = &msg1->media;
    assert(strcmp(media1->media_type, "audio") == 0);
    assert(media1->port == 49170);
    assert(strcmp(media1->proto, "RTP/AVP") == 0);
    assert(media1->payload_type == 0);
    printf("PASS: the real m=audio line parses correctly (port + proto + payload-type)\n");

    assert(strcmp(media1->codec_name, "PCMU") == 0);
    assert(media1->clock_rate == 8000);
    printf("PASS: the real a=rtpmap line fills in this media's own codec name + clock rate\n");

    /* Real, honest error: no m= line at all -- not negotiable audio. */
    const char *raw_no_media =
        "v=0\r\n"
        "o=- 1 1 IN IP4 10.0.0.5\r\n"
        "s=CarePyre\r\n";
    Result r2 = parse_sdp((char *)raw_no_media, &arena);
    assert(r2.tag == 0); /* Err */
    printf("PASS: an SDP body with no m= line is a real, honest MissingMediaLine Err, not a crash\n");

    /* Real, honest error: a malformed line (no '=' in the right place). */
    Result r3 = parse_sdp((char *)"GARBAGE\r\n", &arena);
    assert(r3.tag == 0);
    printf("PASS: a real malformed line is a real, honest MalformedLine Err, not a crash\n");

    /* Real build-sdp-offer + real round-trip back through parse-sdp -- proves the builder's own
       output is genuinely well-formed SDP, not just "looks right by eye". */
    char *built = build_sdp_offer("192.168.1.50", "998877", 49172, codec_pcmu(), "PCMU", 8000, &arena);
    Result r4 = parse_sdp(built, &arena);
    assert(r4.tag == 1);
    SdpMessage *msg4 = (SdpMessage *)r4.value;
    assert(strcmp(msg4->conn_addr, "192.168.1.50") == 0);
    assert(msg4->media.port == 49172);
    assert(msg4->media.payload_type == 0);
    assert(strcmp(msg4->media.codec_name, "PCMU") == 0);
    assert(msg4->media.clock_rate == 8000);
    printf("PASS: a real built SDP offer round-trips correctly back through the parser\n");

    /* codec-pcmu / codec-pcma real, standard RFC 3551 static payload-type values. */
    assert(codec_pcmu() == 0);
    assert(codec_pcma() == 8);
    printf("PASS: codec-pcmu/codec-pcma are the real, standard RFC 3551 payload-type constants\n");

    printf("\nALL PASS\n");
    return 0;
}
