/* tests/test_sha2.c -- crypto/sha2.prn against published vectors only:
 *   SHA-256      FIPS 180-4 examples / NIST CAVS short messages ("", "abc", the 448-bit message,
 *                one million 'a'), which between them cross every padding case
 *   HMAC-SHA256  RFC 4231 test cases 1, 2, 3, 6, 7
 *   HKDF         RFC 5869 A.1 (with salt/info) and A.3 (empty salt and info)
 *   TLS 1.3      RFC 8448 s3: the early secret and Derive-Secret(early, "derived", "") */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "test_sha2_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } else { printf("PASS: %s\n", msg); } } while (0)

static Bytes mk(Arena *a, const void *p, int n) { Bytes b = bytes_alloc_impl(a, n); if (n) memcpy(b.data, p, (size_t)n); return b; }
static Bytes S(Arena *a, const char *s) { return mk(a, s, (int)strlen(s)); }
static Bytes H(Arena *a, const char *hex) {
    int n = (int)strlen(hex) / 2; Bytes b = bytes_alloc_impl(a, n);
    for (int i = 0; i < n; i++) { unsigned v; sscanf(hex + 2 * i, "%2x", &v); b.data[i] = (unsigned char)v; }
    return b;
}
static Bytes fill(Arena *a, int n, int v) { Bytes b = bytes_alloc_impl(a, n); memset(b.data, v, (size_t)n); return b; }
static int eq(Arena *a, Bytes got, const char *hex) {
    Bytes w = H(a, hex);
    int ok = got.len == w.len && memcmp(got.data, w.data, w.len) == 0;
    if (!ok) { printf("  got: "); for (size_t i = 0; i < got.len; i++) printf("%02x", got.data[i]); printf("\n"); }
    return ok;
}

int main(void) {
    Arena a; arena_init(&a);

    /* SHA-256 */
    CHECK(eq(&a, sha256(S(&a, ""), &a), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "SHA-256 \"\"");
    CHECK(eq(&a, sha256(S(&a, "abc"), &a), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "SHA-256 \"abc\"");
    CHECK(eq(&a, sha256(S(&a, "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"), &a),
             "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"), "SHA-256 448-bit message (two blocks)");
    CHECK(eq(&a, sha256(S(&a, "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"), &a),
             "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"), "SHA-256 896-bit message");
    { Arena big; arena_init(&big);
      CHECK(eq(&big, sha256(fill(&big, 1000000, 'a'), &big), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"),
            "SHA-256 one million 'a'"); }
    /* a range inside a larger buffer hashes only the range */
    CHECK(eq(&a, sha256_range(S(&a, "xxabcyy"), 2, 3, &a), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
          "sha256-range over the middle of a buffer");
    CHECK(sha256_range(S(&a, "abc"), 2, 5, &a).len == 0, "a range past the end hashes nothing");

    /* HMAC-SHA256, RFC 4231 */
    CHECK(eq(&a, hmac_sha256(fill(&a, 20, 0x0b), S(&a, "Hi There"), &a),
             "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"), "RFC 4231 case 1");
    CHECK(eq(&a, hmac_sha256(S(&a, "Jefe"), S(&a, "what do ya want for nothing?"), &a),
             "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"), "RFC 4231 case 2");
    CHECK(eq(&a, hmac_sha256(fill(&a, 20, 0xaa), fill(&a, 50, 0xdd), &a),
             "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe"), "RFC 4231 case 3");
    CHECK(eq(&a, hmac_sha256(fill(&a, 131, 0xaa), S(&a, "Test Using Larger Than Block-Size Key - Hash Key First"), &a),
             "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"), "RFC 4231 case 6 (key > block)");
    CHECK(eq(&a, hmac_sha256(fill(&a, 131, 0xaa),
             S(&a, "This is a test using a larger than block-size key and a larger than block-size data. "
                   "The key needs to be hashed before being used by the HMAC algorithm."), &a),
             "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2"), "RFC 4231 case 7");

    /* HKDF, RFC 5869 */
    { Bytes ikm = fill(&a, 22, 0x0b), salt = H(&a, "000102030405060708090a0b0c"), info = H(&a, "f0f1f2f3f4f5f6f7f8f9");
      Bytes prk = hkdf_extract(salt, ikm, &a);
      CHECK(eq(&a, prk, "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5"), "RFC 5869 A.1 PRK");
      CHECK(eq(&a, hkdf_expand(prk, info, 42, &a),
               "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865"), "RFC 5869 A.1 OKM");
      Bytes prk3 = hkdf_extract(bytes_alloc_impl(&a, 0), ikm, &a);
      CHECK(eq(&a, prk3, "19ef24a32c717b167f33a91d6f648bdf96596776afdb6377ac434c1c293ccb04"), "RFC 5869 A.3 PRK (no salt)");
      CHECK(eq(&a, hkdf_expand(prk3, bytes_alloc_impl(&a, 0), 42, &a),
               "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d9d201395faa4b61a96c8"), "RFC 5869 A.3 OKM");
      CHECK(hkdf_expand(prk, info, 8161, &a).len == 0, "an OKM over 255 blocks is refused"); }

    /* TLS 1.3 key schedule, RFC 8448 s3 */
    { Bytes zero = fill(&a, 32, 0);
      Bytes early = hkdf_extract(fill(&a, 0, 0), zero, &a);
      CHECK(eq(&a, early, "33ad0a1c607ec03b09e6cd9893680ce210adf300aa1f2660e1b22e10f170f92a"), "RFC 8448 early secret");
      Bytes empty_hash = sha256(bytes_alloc_impl(&a, 0), &a);
      CHECK(eq(&a, hkdf_expand_label(early, "derived", empty_hash, 32, &a),
               "6f2615a108c702c5678f54fc9dbab69716c076189c48250cebeac3576c3611ba"), "RFC 8448 Derive-Secret(early, \"derived\")"); }

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}
