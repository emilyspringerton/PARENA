/* tests/test_quic_crypto.c -- crypto/quic_crypto.prn. Constants: RFC 9001 Appendix A.1 (Initial keys for
 * DCID 8394c8f03e515708). Full protected packets: produced by aioquic's CryptoPair, an independent QUIC
 * implementation (tests/quic_crypto_vectors.h); we must decode theirs AND they must decode ours. */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "test_quic_crypto_gen.c"
#include "quic_crypto_vectors.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } else { printf("PASS: %s\n", msg); } } while (0)

static Bytes H(Arena *a, const char *h) { return quic_from_hex((char *)h, a); }
static int eqhex(Bytes b, const char *h) {
    if ((size_t)b.len * 2 != strlen(h)) return 0;
    for (int i = 0; i < b.len; i++) { char t[3]; sprintf(t, "%02x", b.data[i]); if (strncmp(t, h + 2 * i, 2)) return 0; }
    return 1;
}

int main(void) {
    Arena a; arena_init(&a);
    Bytes dcid = H(&a, "8394c8f03e515708");

    /* RFC 9001 A.1 */
    Bytes cs = quic_initial_client_secret(dcid, &a), ss = quic_initial_server_secret(dcid, &a);
    CHECK(eqhex(cs, "c00cf151ca5be075ed0ebfb5c80323c42d6b7db67881289af4008f1f6c357aea"), "A.1 client_initial_secret");
    CHECK(eqhex(quic_key_from_secret(cs, &a), "1f369613dd76d5467730efcbe3b1a22d"), "A.1 client key");
    CHECK(eqhex(quic_iv_from_secret(cs, &a), "fa044b2f42a3fd3b46fb255c"), "A.1 client iv");
    CHECK(eqhex(quic_hp_from_secret(cs, &a), "9f50449e04a0e810283a1e9933adedd2"), "A.1 client hp");
    CHECK(eqhex(ss, "3c199828fd139efd216c155ad844cc81fb82fa8d7446fa7d78be803acdda951b"), "A.1 server_initial_secret");
    CHECK(eqhex(quic_key_from_secret(ss, &a), "cf3a5331653c364c88f0f379b6067e37"), "A.1 server key");
    CHECK(eqhex(quic_iv_from_secret(ss, &a), "0ac1493ca1905853b0bba03e"), "A.1 server iv");
    CHECK(eqhex(quic_hp_from_secret(ss, &a), "c206b8d9b9f0f37644430b490eeaa314"), "A.1 server hp");
    CHECK(eqhex(H(&a, QV_CS), QV_CS) && eqhex(cs, QV_CS) && eqhex(ss, QV_SS), "secrets agree with python hmac/hashlib HKDF");

    /* nonce */
    { Bytes n = quic_nonce(H(&a, "fa044b2f42a3fd3b46fb255c"), 2, &a);
      CHECK(eqhex(n, "fa044b2f42a3fd3b46fb255e"), "nonce = iv xor pn(2)"); }
    CHECK(quic_nonce(H(&a, "00"), 1, &a).len == 0, "nonce wrong iv size -> empty");

    /* pn decode: RFC 9000 A.3 */
    CHECK(quic_decode_pn(0xa82f30ea, 0x9b32, 16) == (int)0xa82f9b32, "RFC 9000 A.3 pn decode 0xa82f9b32");
    CHECK(quic_decode_pn(-1, 0, 8) == 0, "first packet pn 0");
    CHECK(quic_decode_pn(255, 0, 8) == 256, "wrap forward");

    /* client Initial: ours must byte-equal aioquic's */
    Bytes ckey = quic_key_from_secret(cs, &a), civ = quic_iv_from_secret(cs, &a), chp = quic_hp_from_secret(cs, &a);
    Bytes hdr1 = H(&a, QV_HDR1), pay1 = H(&a, QV_PAY1), pkt1 = H(&a, QV_PKT1);
    Bytes ours = quic_protect(ckey, civ, chp, hdr1, 18, 2, pay1, &a);
    CHECK(ours.len == 1200 && memcmp(ours.data, pkt1.data, 1200) == 0, "protect: byte-identical to aioquic client Initial");
    CHECK(quic_unprotect_pn(pkt1, 18, chp, -1, &a) == 2, "unprotect_pn recovers pn 2 from aioquic packet");
    Bytes dec = quic_unprotect(pkt1, 18, ckey, civ, chp, -1, &a);
    CHECK(dec.len == pay1.len && memcmp(dec.data, pay1.data, (size_t)pay1.len) == 0, "unprotect: aioquic packet -> original payload");

    /* tamper */
    { Bytes bad = bytes_alloc_impl(&a, pkt1.len); memcpy(bad.data, pkt1.data, (size_t)pkt1.len);
      bad.data[600] ^= 1;
      CHECK(quic_unprotect(bad, 18, ckey, civ, chp, -1, &a).len == 0, "tampered ciphertext rejected");
      memcpy(bad.data, pkt1.data, (size_t)pkt1.len); bad.data[0] ^= 0x01;
      CHECK(quic_unprotect(bad, 18, ckey, civ, chp, -1, &a).len == 0, "tampered header (AAD) rejected");
      memcpy(bad.data, pkt1.data, (size_t)pkt1.len); bad.data[pkt1.len - 1] ^= 0x80;
      CHECK(quic_unprotect(bad, 18, ckey, civ, chp, -1, &a).len == 0, "tampered tag rejected"); }
    CHECK(quic_unprotect(pkt1, 18, quic_key_from_secret(ss, &a), civ, chp, -1, &a).len == 0, "wrong key rejected");

    /* server Initial (short payload, 2-byte pn, long header with 8-byte SCID) */
    Bytes skey = quic_key_from_secret(ss, &a), siv = quic_iv_from_secret(ss, &a), shp = quic_hp_from_secret(ss, &a);
    Bytes hdr2 = H(&a, QV_HDR2), pay2 = H(&a, QV_PAY2), pkt2 = H(&a, QV_PKT2);
    Bytes o2 = quic_protect(skey, siv, shp, hdr2, 18, 1, pay2, &a);
    CHECK(o2.len == pkt2.len && memcmp(o2.data, pkt2.data, (size_t)pkt2.len) == 0, "protect: byte-identical to aioquic server Initial");
    Bytes d2 = quic_unprotect(pkt2, 18, skey, siv, shp, 0, &a);
    CHECK(d2.len == pay2.len && memcmp(d2.data, pay2.data, (size_t)pay2.len) == 0, "unprotect: aioquic server packet");

    /* round trip, 1-byte pn and short header */
    { Bytes hdr = H(&a, "4100112233445566") ;  /* short header: 0x41 => pn len 2? low bits=01 -> 2 bytes */
      Bytes h = bytes_alloc_impl(&a, 1 + 8 + 2); memcpy(h.data, "\x41\x01\x02\x03\x04\x05\x06\x07\x08\x00\x07", 11);
      Bytes pl = H(&a, "0102030405060708090a0b0c0d0e0f10");
      (void)hdr;
      Bytes p = quic_protect(ckey, civ, chp, h, 9, 7, pl, &a);
      CHECK(p.len == 11 + 16 + 16, "short header packet length");
      CHECK((p.data[0] & 0xe0) == 0x40, "short header top bits (form/fixed) unmasked");
      Bytes q = quic_unprotect(p, 9, ckey, civ, chp, 3, &a);
      CHECK(q.len == 16 && memcmp(q.data, pl.data, 16) == 0, "short-header round trip");
      CHECK(quic_unprotect_pn(p, 9, chp, 3, &a) == 7, "short-header pn");
      Bytes tiny = H(&a, "01");
      CHECK(quic_protect(ckey, civ, chp, h, 9, 7, tiny, &a).len == 0, "payload too short to sample -> empty"); }

    /* fuzz: garbage packets never crash, never authenticate */
    srand(4242);
    for (int it = 0; it < 3000; it++) {
        int n = rand() % 80; Bytes g = bytes_alloc_impl(&a, n);
        for (int i = 0; i < n; i++) g.data[i] = (unsigned char)rand();
        if (quic_unprotect(g, rand() % 24, ckey, civ, chp, 0, &a).len != 0) { CHECK(0, "garbage authenticated!?"); break; }
        (void)quic_unprotect_pn(g, rand() % 24, chp, 0, &a);
    }
    CHECK(1, "3000 garbage packets: no crash, none authenticate");

    arena_free_all(&a);
    printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
