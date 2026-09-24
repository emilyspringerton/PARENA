/* tests/test_mldsa.c -- real end-to-end verification for stdlib/crypto/mldsa.prn
 * (EMILY/BACKLOG.md SECTION 536 follow-up, BIG_O/NORTHSTAR.md §29), PARENA's first real,
 * actually-working crypto binding (crypto/ed25519.prn, same directory, is a design-only stub --
 * see mldsa.prn's own header comment).
 *
 * Same "call the real, generated PARENA call chain, not the vendored C directly" discipline
 * test_shell.c/test_bytes.c already establish -- this drives mldsa-keygen/mldsa-sign/
 * mldsa-verify exactly as a real PARENA caller would, through the actual generated
 * mldsa_gen.c/bytes_gen.c, linked against the real, vendored, independently-verified
 * CRYSTALS-Dilithium reference (runtime/mldsa/), not a mock or a hand-rolled stand-in.
 *
 * Coverage mirrors the pre-pivot BIG_O bigo_mldsa_test.c (now superseded/deleted once this
 * PARENA binding replaced it): non-zero keygen output, sign->verify roundtrip success, tampered-
 * signature rejection, tampered-message rejection, and cross-key rejection.
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>

#include "test_mldsa_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static int bytes_all_zero(Bytes b) {
    for (int i = 0; i < b.len; i++) {
        if (bytes_get(b, i) != 0) return 0;
    }
    return 1;
}

int main(void) {
    Arena a;
    arena_init(&a);

    /* --- mldsa-keygen: real, correctly-sized, non-degenerate key material. */
    KeyPair kp = mldsa_keygen(&a);
    CHECK(bytes_len(kp.public_key) == mldsa_pubkey_bytes(),
          "mldsa-keygen produces a public key of exactly the real, fixed ML-DSA-44 size (1312 bytes)");
    CHECK(bytes_len(kp.secret_key) == mldsa_seckey_bytes(),
          "mldsa-keygen produces a secret key of exactly the real, fixed ML-DSA-44 size (2560 bytes)");
    CHECK(!bytes_all_zero(kp.public_key),
          "mldsa-keygen's public key is real, non-degenerate output, not an all-zero buffer");
    CHECK(!bytes_all_zero(kp.secret_key),
          "mldsa-keygen's secret key is real, non-degenerate output, not an all-zero buffer");

    /* --- a second, independent keygen call produces a genuinely different key -- proves this
     * isn't reading stale/static memory. */
    KeyPair kp2 = mldsa_keygen(&a);
    CHECK(memcmp(kp.public_key.data, kp2.public_key.data, (size_t)bytes_len(kp.public_key)) != 0,
          "two independent mldsa-keygen calls produce two genuinely different public keys");

    /* --- sign -> verify roundtrip: the real, central case. */
    Bytes msg = bytes_from_string("real ML-DSA-44 test message, PARENA crypto/mldsa.prn", &a);
    Bytes sig = mldsa_sign(msg, &kp, &a);
    CHECK(bytes_len(sig) == mldsa_sig_bytes(),
          "mldsa-sign produces a signature of exactly the real, fixed ML-DSA-44 size (2420 bytes)");
    CHECK(mldsa_verify(sig, msg, kp.public_key),
          "a real signature over a real message verifies successfully against the real, matching public key");

    /* --- tampered signature: flip one byte, must reject. */
    {
        Bytes tampered = bytes_slice(sig, 0, bytes_len(sig), &a);
        bytes_set_(tampered, 0, bytes_get(tampered, 0) ^ 0xFF);
        CHECK(!mldsa_verify(tampered, msg, kp.public_key),
              "a signature tampered by a single flipped byte is honestly rejected, not falsely accepted");
    }

    /* --- tampered message: verify the original signature against a different message, must
     * reject. */
    {
        Bytes other_msg = bytes_from_string("a different message the signature was never over", &a);
        CHECK(!mldsa_verify(sig, other_msg, kp.public_key),
              "the real signature does not verify against a different, tampered message");
    }

    /* --- cross-key rejection: verify against an unrelated public key, must reject. */
    CHECK(!mldsa_verify(sig, msg, kp2.public_key),
          "the real signature does not verify against an unrelated, mismatched public key");

    /* --- hedged/randomized signing (DILITHIUM_RANDOMIZED_SIGNING, runtime/mldsa/config.h's own
     * real, unchanged-from-upstream default): two real signatures over the identical message
     * under the identical key are not byte-identical, but both independently verify. */
    {
        Bytes sig2 = mldsa_sign(msg, &kp, &a);
        CHECK(bytes_len(sig2) == bytes_len(sig) &&
              memcmp(sig.data, sig2.data, (size_t)bytes_len(sig)) != 0,
              "two signatures over the same message under the same key are not byte-identical -- "
              "real, hedged/randomized signing, not deterministic");
        CHECK(mldsa_verify(sig2, msg, kp.public_key),
              "the second, independently-signed signature over the same message also verifies correctly");
    }

    arena_free_all(&a);

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
