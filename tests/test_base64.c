/* tests/test_base64.c -- stdlib/base64.prn against RFC 4648 section 10's test vectors, plus whitespace, padding-less
 * input, a non-base64 input, and a byte value that is 0 (the reason the result is Bytes). Build: make test-base64 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>

#include "test_base64_gen.c"

static int failures = 0, passes = 0;
#define CHECK(cond, msg) do { if (cond) passes++; else { printf("FAIL: %s\n", msg); failures++; } } while (0)

static int same(Bytes b, const char *want, int n) {
    if (bytes_len(b) != n) return 0;
    for (int i = 0; i < n; i++) if (bytes_get(b, i) != (int)(unsigned char)want[i]) return 0;
    return 1;
}

int main(void) {
    Arena a;
    arena_init(&a);
    /* RFC 4648 section 10 */
    CHECK(same(base64_decode("", &a), "", 0), "\"\"");
    CHECK(same(base64_decode("Zg==", &a), "f", 1), "Zg== -> f");
    CHECK(same(base64_decode("Zm8=", &a), "fo", 2), "Zm8= -> fo");
    CHECK(same(base64_decode("Zm9v", &a), "foo", 3), "Zm9v -> foo");
    CHECK(same(base64_decode("Zm9vYg==", &a), "foob", 4), "Zm9vYg== -> foob");
    CHECK(same(base64_decode("Zm9vYmE=", &a), "fooba", 5), "Zm9vYmE= -> fooba");
    CHECK(same(base64_decode("Zm9vYmFy", &a), "foobar", 6), "Zm9vYmFy -> foobar");
    /* lenient, but not a guess */
    CHECK(same(base64_decode("Zm9v\r\nYmFy", &a), "foobar", 6), "line breaks are skipped");
    CHECK(same(base64_decode("Zm8", &a), "fo", 2), "missing padding still decodes");
    CHECK(bytes_len(base64_decode("Zm9v*mFy", &a)) == 0, "a character outside the alphabet gives nothing");
    /* binary: the PNG signature, with a 0 byte in the middle of the next chunk length */
    CHECK(same(base64_decode("iVBORw0KGgoAAAAN", &a), "\x89PNG\r\n\x1a\n\0\0\0\r", 12), "a PNG signature, zeros kept");
    CHECK(same(base64_decode("+/+/", &a), "\xfb\xff\xbf", 3), "+ and / are 62 and 63");
    CHECK(base64_decoded_len("Zm9vYmE=") == 5, "decoded length");
    printf("base64: %d passed, %d failed\n", passes, failures);
    arena_free_all(&a);
    return failures ? 1 : 0;
}
