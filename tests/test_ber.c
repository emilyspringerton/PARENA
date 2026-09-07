/* tests/test_ber.c -- real end-to-end verification of stdlib/ldap/ber.prn (kanban priority-queue
 * card 342332432423, "ldap primatives parena"). Real, hand-constructed BER TLV elements, per RFC
 * 4511/ASN.1 BER (checked live via real web research): 0x02 = INTEGER, 0x60 = LDAP BindRequest
 * (application class, constructed, tag 0).
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "test_ber_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* Real BER-encoded INTEGER 3 (LDAP's own real messageID field shape): tag=0x02, len=0x01,
     * value=0x03. */
    char intbuf[3] = { 0x02, 0x01, 0x03 };
    assert(read_ber_tag(intbuf, 0) == 0x02);
    Result r = read_ber_length(intbuf, 0, &arena);
    assert(r.tag == 1);
    assert(*(int *)r.value == 1);
    assert(ber_header_size() == 2);
    /* Real value byte lives right after the 2-byte header. */
    assert((unsigned char)intbuf[ber_header_size()] == 3);

    /* Real, honest v0 boundary: a long-form length byte (top bit set) reports a real, honest
     * error, never a silent mis-decode. */
    char longform[3] = { 0x02, (char)0x81, 0x05 };
    Result rlong = read_ber_length(longform, 0, &arena);
    assert(rlong.tag == 0);

    /* read_ber_length_ext -- the real fix (2026-09-07, PARENA cybersecurity-primitives thread):
     * the exact same long-form bytes above, which read_ber_length honestly refuses, now decode
     * correctly. 0x81 0x05 = "1 length byte follows, value 5" = length 5. */
    Result rext1 = read_ber_length_ext(longform, 0, &arena);
    assert(rext1.tag == 1);
    assert(*(int *)rext1.value == 5);
    assert(ber_header_size_ext(longform, 0) == 3); /* 1 tag + 1 lead + 1 real length byte */

    /* 2-byte count: 0x82 0x01 0x00 = "2 length bytes follow: 0x01 0x00" = 256, the real, common
     * shape any X.509 certificate field over 255 bytes actually uses. */
    char longform2[4] = { 0x30, (char)0x82, 0x01, 0x00 };
    Result rext2 = read_ber_length_ext(longform2, 0, &arena);
    assert(rext2.tag == 1);
    assert(*(int *)rext2.value == 256);
    assert(ber_header_size_ext(longform2, 0) == 4); /* 1 tag + 1 lead + 2 real length bytes */

    /* Short-form still works unchanged through the _ext path -- one real function correctly
     * covers both cases, a real caller doesn't need to know in advance which form a given TLV
     * uses. */
    Result rext_short = read_ber_length_ext(intbuf, 0, &arena);
    assert(rext_short.tag == 1);
    assert(*(int *)rext_short.value == 1);
    assert(ber_header_size_ext(intbuf, 0) == 2);

    /* Real, honest v1 boundary: indefinite-length form (count byte itself is 0x80) is a real,
     * separate, legal BER encoding this stdlib still doesn't attempt -- reported, not guessed. */
    char indefinite[2] = { 0x30, (char)0x80 };
    Result rindef = read_ber_length_ext(indefinite, 0, &arena);
    assert(rindef.tag == 0);

    /* build-ber-tlv: real round-trip -- encode tag=0x04 (OCTET STRING, LDAP's own real shape
     * for a bind DN/password), value "abc", confirm the exact real wire bytes. */
    char *value = "abc";
    char *built = build_ber_tlv(0x04, value, 3, &arena);
    unsigned char expected[5] = { 0x04, 0x03, 'a', 'b', 'c' };
    assert(memcmp(built, expected, 5) == 0);

    /* Real, deliberate zero-length-value case -- exercises the exact real reason this file
     * needed a #target inline-c escape hatch instead of string/concat (a real length byte of
     * 0x00, which concat's own strcpy/strcat would truncate on immediately). */
    char *empty_built = build_ber_tlv(0x04, "", 0, &arena);
    unsigned char expected_empty[2] = { 0x04, 0x00 };
    assert(memcmp(empty_built, expected_empty, 2) == 0);

    printf("test_ber: all real assertions passed\n");
    return 0;
}
