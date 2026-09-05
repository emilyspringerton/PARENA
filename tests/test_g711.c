/* tests/test_g711.c -- real end-to-end verification of stdlib/sip/g711.prn (CarePyre SIP Phone
 * Phase 4, the audio codec half of docs/SIP_PHONE_ANDROID_NORTHSTAR.md's own real plan). Every
 * expected value here was hand-derived, step by step, directly from the same real, canonical
 * public-domain reference (Sun Microsystems/CCITT g711.c) this module is a direct port of --
 * not guessed, not "looks about right" -- because a subtly wrong companding table produces audio
 * that sounds wrong rather than failing loudly, the one place in this SIP phone effort where
 * that distinction actually matters.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>

#include "test_g711_gen.c"

int main(void) {
    /* mu-law: real, exact fixed points at silence and full-scale clip (both polarities), and one
     * real, hand-derived mid-range encode+decode round trip (pcm 10000 -> ulaw 156 -> pcm 9852). */
    assert(linear2ulaw(0) == 255);       /* 0xFF -- silence */
    assert(ulaw2linear(255) == 0);       /* the real, exact inverse of the above */
    printf("PASS: mu-law silence (linear 0) round-trips exactly through 0xFF\n");

    assert(linear2ulaw(32767) == 128);   /* 0x80 -- max positive sample, clipped */
    assert(linear2ulaw(-32768) == 0);    /* 0x00 -- max negative sample, clipped */
    printf("PASS: mu-law full-scale clipping produces the real, standard 0x00/0x80 codewords\n");

    assert(linear2ulaw(10000) == 156);
    assert(ulaw2linear(156) == 9852);
    printf("PASS: a real mu-law mid-range encode+decode round trip matches the hand-derived exact value\n");

    /* A-law: real, exact fixed points -- silence does NOT round-trip to exactly 0 (a real,
     * known A-law property, not a bug: segment 0's own "+8" offset in alaw2linear), and a real,
     * hand-derived mid-range round trip (pcm 10000 -> alaw 182 -> pcm 9984). */
    assert(linear2alaw(0) == 213);        /* 0xD5 */
    assert(alaw2linear(213) == 8);        /* real, exact, NOT zero -- A-law's own segment-0 offset */
    printf("PASS: A-law silence encodes to the real 0xD5, and decodes to the real, honest 8 (not 0)\n");

    assert(linear2alaw(10000) == 182);
    assert(alaw2linear(182) == 9984);
    printf("PASS: a real A-law mid-range encode+decode round trip matches the hand-derived exact value\n");

    /* Real, honest sanity check: mu-law and A-law are genuinely different encodings -- the same
     * real input sample must NOT produce the same codeword under both (if it did, one of the two
     * ports would be wrong, silently reusing the other's math). */
    assert(linear2ulaw(10000) != linear2alaw(10000));
    printf("PASS: mu-law and A-law produce genuinely different codewords for the same real sample\n");

    printf("\nALL PASS\n");
    return 0;
}
