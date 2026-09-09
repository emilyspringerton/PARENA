/* tests/test_bytes.c -- real end-to-end verification for stdlib/bytes.prn
 * (docs/BYTES_NORTHSTAR.md), PARENA's real second core-language base
 * type alongside String.
 *
 * The one real thing worth proving, above everything else: a genuine
 * 0x00 byte survives intact in the middle of a Bytes buffer, and the
 * buffer's own reported length is unaffected by it -- the entire real
 * reason this type exists. Built via bytes-alloc/bytes-set! directly
 * (never through a NUL-terminated C string first), since a String
 * already truncated upstream can't be un-truncated by anything this
 * type does -- that's a real, honest, named, NOT-solved limitation
 * (see this file's own header comment in stdlib/bytes.prn), not an
 * oversight in this test.
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>

#include "test_bytes_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

int main(void) {
    Arena a;
    arena_init(&a);

    /* --- the real, central case: a genuine embedded 0x00 byte,
     * survives intact, length unaffected. */
    {
        Bytes b = bytes_alloc(11, &a);
        CHECK(bytes_len(b) == 11, "bytes-alloc allocates the exact requested length");

        const char *src = "hello_world";
        for (int i = 0; i < 11; i++) bytes_set_(b, i, (int)(unsigned char)src[i]);
        bytes_set_(b, 5, 0); /* the real embedded 0x00 byte, where '_' was */

        CHECK(bytes_get(b, 5) == 0, "a real embedded 0x00 byte set via bytes-set! reads back correctly");
        CHECK(bytes_get(b, 6) == 'w', "the byte immediately after the embedded 0x00 is not lost");
        CHECK(bytes_get(b, 10) == 'd', "the last byte, well past the embedded 0x00, survives intact");
        CHECK(bytes_len(b) == 11,
              "the buffer's own length is unaffected by the embedded 0x00 -- no strlen-style truncation, "
              "the entire real reason this type exists");
    }

    /* --- real, honest out-of-bounds handling, matching vec-get/
     * vec-set-at!'s own established convention exactly. */
    {
        Bytes b = bytes_alloc(4, &a);
        for (int i = 0; i < 4; i++) bytes_set_(b, i, i + 1);

        CHECK(bytes_get(b, -1) == -1, "bytes-get on a negative index returns the honest -1 sentinel");
        CHECK(bytes_get(b, 4) == -1, "bytes-get exactly at len returns the honest -1 sentinel");
        CHECK(bytes_get(b, 999) == -1, "bytes-get well past len returns the honest -1 sentinel");

        bytes_set_(b, -1, 42);
        bytes_set_(b, 999, 42);
        CHECK(bytes_get(b, 0) == 1 && bytes_get(b, 1) == 2 && bytes_get(b, 2) == 3 && bytes_get(b, 3) == 4,
              "out-of-bounds bytes-set! calls are honest silent no-ops -- real, valid data untouched, no crash");
    }

    /* --- real, honest zero-length edge case. */
    {
        Bytes b = bytes_alloc(0, &a);
        CHECK(bytes_len(b) == 0, "bytes-alloc with length 0 reports length 0");
        CHECK(bytes_get(b, 0) == -1, "reading index 0 of a zero-length buffer is an honest out-of-bounds");

        Bytes neg = bytes_alloc(-5, &a);
        CHECK(bytes_len(neg) == 0, "bytes-alloc with a negative length honestly clamps to 0, not undefined behavior");
    }

    /* --- bytes-from-string: real, byte-perfect copy of an ordinary
     * (no embedded NUL) String's bytes -- the real, common case this
     * exists to support (e.g. converting a known-safe ASCII command
     * string into Bytes before a future byte-oriented transfer
     * primitive). */
    {
        Bytes b = bytes_from_string("hello", &a);
        CHECK(bytes_len(b) == 5, "bytes-from-string on an ordinary ASCII string gets the exact real length");
        CHECK(bytes_get(b, 0) == 'h' && bytes_get(b, 4) == 'o',
              "bytes-from-string copies the real, exact bytes of an ordinary string");

        Bytes empty = bytes_from_string("", &a);
        CHECK(bytes_len(empty) == 0, "bytes-from-string on an empty string gets length 0, not a crash");
    }

    /* --- bytes-from-string's own real, honestly-documented limitation:
     * a C string literal itself is already NUL-truncated by the C
     * language before this function ever sees it, so strlen() alone
     * can't recover bytes past an embedded 0x00 that arrived via a
     * plain string literal -- proving the documented boundary is real,
     * not just written down. */
    {
        Bytes b = bytes_from_string("hello\x00world", &a);
        CHECK(bytes_len(b) == 5,
              "bytes-from-string honestly inherits C's own string-literal truncation at an embedded 0x00 -- "
              "documented as a real, named boundary, not silently pretended solved");
    }

    /* --- bytes-to-string-lossy: real, correct on an ordinary buffer
     * with no embedded 0x00. */
    {
        Bytes b = bytes_alloc(5, &a);
        const char *src = "world";
        for (int i = 0; i < 5; i++) bytes_set_(b, i, (int)(unsigned char)src[i]);
        char *s = bytes_to_string_lossy(b, &a);
        CHECK(s != NULL && strcmp(s, "world") == 0,
              "bytes-to-string-lossy round-trips an ordinary, NUL-free buffer exactly");
    }

    /* --- bytes-to-string-lossy's own real, honestly-named limitation:
     * a buffer WITH a genuine embedded 0x00 truncates on the way back
     * out to String -- the same real boundary this type exists to let
     * binary DATA avoid, but which still applies the moment a caller
     * asks for an ordinary String back. */
    {
        Bytes b = bytes_alloc(11, &a);
        const char *src = "hello_world";
        for (int i = 0; i < 11; i++) bytes_set_(b, i, (int)(unsigned char)src[i]);
        bytes_set_(b, 5, 0);
        char *s = bytes_to_string_lossy(b, &a);
        CHECK(s != NULL && strcmp(s, "hello") == 0,
              "bytes-to-string-lossy honestly truncates at a real embedded 0x00 -- named in its own "
              "function name, not silently lossy under an innocent-looking name");
    }

    arena_free_all(&a);

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
