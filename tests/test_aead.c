/* tests/test_aead.c -- crypto/aead.prn (XChaCha20-Poly1305 via vendored Monocypher 4.0.2).
 * The known-answer vector KAT_HEX was produced by Go's independent
 * golang.org/x/crypto/chacha20poly1305 NewX (key=00..1f, nonce=40..57, ad="edge-ad",
 * plaintext="EDGE.GAME \0 binary-safe payload"), so a pass means two unrelated implementations agree. */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include "test_aead_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } else { printf("PASS: %s\n", msg); } } while (0)

static const char *KAT_HEX = "917d4235fea7385bcad4879ecdf50bf3e0c380b7723f36ba1a508429666547e7826dd7f13435c1ea851ead282c291b";

static Bytes mk(Arena *a, const unsigned char *p, int n) {
    Bytes b = bytes_alloc_impl(a, n);
    if (n) memcpy(b.data, p, (size_t)n);
    return b;
}

int main(void) {
    Arena a; arena_init(&a);
    unsigned char key[32], nonce[24];
    for (int i = 0; i < 32; i++) key[i] = (unsigned char)i;
    for (int i = 0; i < 24; i++) nonce[i] = (unsigned char)(0x40 + i);
    const unsigned char pt[] = "EDGE.GAME \0 binary-safe payload";
    int ptn = (int)sizeof(pt) - 1; /* includes the embedded NUL, drops the terminator */
    Bytes k = mk(&a, key, 32), n = mk(&a, nonce, 24);
    Bytes ad = mk(&a, (const unsigned char *)"edge-ad", 7);
    Bytes p = mk(&a, pt, ptn);

    Bytes sealed = aead_seal(k, n, ad, p, &a);
    CHECK(sealed.len == ptn + 16, "sealed = plaintext + 16-byte mac");
    char hex[256] = {0};
    for (int i = 0; i < sealed.len; i++) sprintf(hex + 2 * i, "%02x", sealed.data[i]);
    CHECK(strcmp(hex, KAT_HEX) == 0, "matches Go x/crypto NewX known-answer vector");

    Bytes opened = aead_open(k, n, ad, sealed, &a);
    CHECK(opened.len == ptn && memcmp(opened.data, pt, (size_t)ptn) == 0, "open recovers plaintext (NUL preserved)");

    Bytes bad = mk(&a, sealed.data, sealed.len);
    bad.data[3] ^= 1;
    CHECK(aead_open(k, n, ad, bad, &a).len == 0, "tampered ciphertext rejected");
    Bytes badtag = mk(&a, sealed.data, sealed.len);
    badtag.data[sealed.len - 1] ^= 1;
    CHECK(aead_open(k, n, ad, badtag, &a).len == 0, "tampered tag rejected");
    Bytes ad2 = mk(&a, (const unsigned char *)"edge-aD", 7);
    CHECK(aead_open(k, n, ad2, sealed, &a).len == 0, "wrong associated data rejected");
    Bytes n2 = mk(&a, nonce, 24); n2.data[0] ^= 1;
    CHECK(aead_open(k, n2, ad, sealed, &a).len == 0, "wrong nonce rejected");
    Bytes shortk = mk(&a, key, 31);
    CHECK(aead_seal(shortk, n, ad, p, &a).len == 0, "short key -> zero-length, no OOB");
    Bytes shortn = mk(&a, nonce, 23);
    CHECK(aead_open(k, shortn, ad, sealed, &a).len == 0, "short nonce -> zero-length, no OOB");
    CHECK(aead_open(k, n, ad, mk(&a, sealed.data, 16), &a).len == 0, "mac-only input -> zero-length");
    arena_free_all(&a);
    printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
