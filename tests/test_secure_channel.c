/* tests/test_secure_channel.c -- net/secure_channel.prn end to end: a real ML-KEM-768 handshake,
 * per-direction keys, then LZ4+XChaCha20-Poly1305 frames both ways, plus every failure mode. */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include "test_secure_channel_gen.c"

static int failures = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); failures++; } else printf("PASS: %s\n", m); } while (0)
static int same(Bytes a, Bytes b) { return a.len == b.len && memcmp(a.data, b.data, (size_t)a.len) == 0; }
static Bytes mk(Arena *a, const void *p, int n) { Bytes b = bytes_alloc_impl(a, n); if (n) memcpy(b.data, p, (size_t)n); return b; }

int main(void) {
    Arena a; arena_init(&a);
    /* ---- handshake ---- */
    KemKeyPair srv = mlkem_keygen(&a);
    Bytes pin = sc_fingerprint(srv.encapsulation_key, &a);
    CHECK(pin.len == 32, "fingerprint is 32 bytes");
    CHECK(same(pin, sc_fingerprint(srv.encapsulation_key, &a)), "fingerprint is deterministic");
    KemKeyPair other = mlkem_keygen(&a);
    CHECK(!same(pin, sc_fingerprint(other.encapsulation_key, &a)), "different server key -> different fingerprint (pin mismatch detectable)");
    KemEncaps enc = mlkem_encaps(srv.encapsulation_key, &a);
    Bytes ss_srv = mlkem_decaps(enc.ciphertext, srv.decapsulation_key, &a);
    Bytes c2s_c = sc_derive_key(enc.shared_secret, 1, &a), c2s_s = sc_derive_key(ss_srv, 1, &a);
    Bytes s2c_c = sc_derive_key(enc.shared_secret, 2, &a), s2c_s = sc_derive_key(ss_srv, 2, &a);
    CHECK(same(c2s_c, c2s_s) && same(s2c_c, s2c_s), "both sides derive identical per-direction keys");
    CHECK(!same(c2s_c, s2c_c), "the two directions get independent keys");

    /* ---- frames: client -> server ---- */
    const char *cmd = "{\"id\":\"c1\",\"type\":\"usb_probe\",\"payload\":{}}";
    Bytes m1 = mk(&a, cmd, (int)strlen(cmd));
    Bytes w1 = sc_frame_seal(c2s_c, 1, 0, m1, &a);
    CHECK(w1.len > 0, "seals a command");
    Bytes r1 = sc_frame_open(c2s_s, 1, 0, w1, &a);
    CHECK(same(r1, m1), "server opens client's frame");
    CHECK(sc_frame_open(c2s_s, 1, 0, w1, &a).len > 0 && sc_frame_open(c2s_s, 1, 1, w1, &a).len == 0, "replayed/out-of-order counter rejected");
    CHECK(sc_frame_open(s2c_s, 2, 0, w1, &a).len == 0, "frame can't be opened with the other direction's key");
    CHECK(sc_frame_open(c2s_s, 2, 0, w1, &a).len == 0, "frame can't be reflected as the other direction");

    /* ---- frames: server -> client, compressible reply is actually smaller ---- */
    char big[4000]; for (int i = 0; i < 4000; i++) big[i] = "{\"devices\":[],\"feather\":null} "[i % 31];
    Bytes m2 = mk(&a, big, 4000);
    Bytes w2 = sc_frame_seal(s2c_s, 2, 0, m2, &a);
    CHECK(w2.len > 0 && w2.len < 600, "4000 repetitive bytes -> well under 600 on the wire (LZ4 engaged)");
    CHECK(same(sc_frame_open(s2c_c, 2, 0, w2, &a), m2), "client opens the compressed reply");

    /* incompressible data stored raw but still round-trips */
    unsigned char rnd[200]; unsigned x = 12345; for (int i = 0; i < 200; i++) { x = x * 1103515245u + 12345u; rnd[i] = (unsigned char)(x >> 16); }
    Bytes m3 = mk(&a, rnd, 200);
    Bytes w3 = sc_frame_seal(c2s_c, 1, 1, m3, &a);
    CHECK(w3.len == 4 + 5 + 200 + 16, "incompressible: stored raw (4 ctr + 5 hdr + 200 + 16 mac)");
    CHECK(same(sc_frame_open(c2s_s, 1, 1, w3, &a), m3), "raw-stored frame round-trips");

    /* ---- tamper / malformed ---- */
    { int bad = 0;
      for (int i = 0; i < w1.len; i++) {
          Bytes t = mk(&a, w1.data, w1.len); t.data[i] ^= 1;
          if (sc_frame_open(c2s_s, 1, 0, t, &a).len != 0) bad++;
      }
      CHECK(bad == 0, "every single-bit flip anywhere in a frame is rejected"); }
    { int bad = 0;
      for (int n = 0; n < w1.len; n++) if (sc_frame_open(c2s_s, 1, 0, mk(&a, w1.data, n), &a).len != 0) bad++;
      CHECK(bad == 0, "every truncation is rejected"); }
    CHECK(sc_frame_seal(c2s_c, 1, 0, mk(&a, "", 0), &a).len == 0, "empty message refused");
    { unsigned char huge[60001]; memset(huge, 7, sizeof(huge));
      CHECK(sc_frame_seal(c2s_c, 1, 0, mk(&a, huge, 60001), &a).len == 0, "oversize message refused"); }
    { unsigned char mx[60000]; memset(mx, 9, sizeof(mx));
      Bytes mm = mk(&a, mx, 60000);
      Bytes wm = sc_frame_seal(c2s_c, 1, 5, mm, &a);
      CHECK(same(sc_frame_open(c2s_s, 1, 5, wm, &a), mm), "max-size (60000) message round-trips"); }
    CHECK(sc_frame_seal(mk(&a, "short", 5), 1, 0, m1, &a).len == 0, "short key refused");

    /* ---- wrong ciphertext at handshake: implicit rejection -> keys differ -> frames fail ---- */
    Bytes ct2 = mk(&a, enc.ciphertext.data, enc.ciphertext.len); ct2.data[10] ^= 1;
    Bytes ss_bad = mlkem_decaps(ct2, srv.decapsulation_key, &a);
    Bytes k_bad = sc_derive_key(ss_bad, 1, &a);
    CHECK(sc_frame_open(k_bad, 1, 0, w1, &a).len == 0, "tampered handshake ct -> server key differs -> first frame fails");

    arena_free_all(&a);
    printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
