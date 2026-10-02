/* tests/test_mlkem.c -- end-to-end verification of stdlib/crypto/mlkem.prn through its real
 * generated call chain, linked against the vendored pq-crystals ML-KEM-768 reference. Independent
 * cross-implementation checking lives in tests/mlkem_interop/ (Go crypto/mlkem). */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include "test_mlkem_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } else { printf("PASS: %s\n", msg); } } while (0)

static int same(Bytes a, Bytes b) { return a.len == b.len && memcmp(a.data, b.data, (size_t)a.len) == 0; }

int main(void) {
    Arena a; arena_init(&a);
    KemKeyPair kp = mlkem_keygen(&a);
    CHECK(bytes_len(kp.encapsulation_key) == 1184, "ek is 1184 bytes (FIPS 203 ML-KEM-768)");
    CHECK(bytes_len(kp.decapsulation_key) == 2400, "dk is 2400 bytes (FIPS 203 ML-KEM-768)");

    KemEncaps e = mlkem_encaps(kp.encapsulation_key, &a);
    CHECK(bytes_len(e.ciphertext) == 1088, "ciphertext is 1088 bytes");
    CHECK(bytes_len(e.shared_secret) == 32, "shared secret is 32 bytes");
    Bytes ss = mlkem_decaps(e.ciphertext, kp.decapsulation_key, &a);
    CHECK(same(ss, e.shared_secret), "decaps recovers the encapsulated shared secret");

    KemEncaps e2 = mlkem_encaps(kp.encapsulation_key, &a);
    CHECK(!same(e.shared_secret, e2.shared_secret), "two encapsulations give different secrets");

    KemKeyPair kp2 = mlkem_keygen(&a);
    Bytes wrong = mlkem_decaps(e.ciphertext, kp2.decapsulation_key, &a);
    CHECK(bytes_len(wrong) == 32 && !same(wrong, e.shared_secret), "wrong dk: implicit rejection, different secret");

    Bytes ct2 = bytes_slice(e.ciphertext, 0, 1088, &a);
    ct2.data[7] ^= 1;
    Bytes t = mlkem_decaps(ct2, kp.decapsulation_key, &a);
    CHECK(bytes_len(t) == 32 && !same(t, e.shared_secret), "tampered ciphertext: implicit rejection");

    Bytes short_ek = bytes_slice(kp.encapsulation_key, 0, 1183, &a);
    KemEncaps bad = mlkem_encaps(short_ek, &a);
    CHECK(bytes_len(bad.ciphertext) == 0 && bytes_len(bad.shared_secret) == 0, "short ek: zero-length result, no OOB read");
    Bytes shortct = bytes_slice(e.ciphertext, 0, 100, &a);
    CHECK(bytes_len(mlkem_decaps(shortct, kp.decapsulation_key, &a)) == 0, "short ct: zero-length result");

    arena_free_all(&a);
    printf(failures ? "FAILED (%d)\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
