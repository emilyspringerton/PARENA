/* Vendored, byte-for-byte unmodified from the official CRYSTALS-Dilithium reference
 * implementation (https://github.com/pq-crystals/dilithium, `ref/` directory, `master` branch --
 * commit d35ba3f, the FIPS-204-aligned revision, checked directly via TRBYTES/RNDBYTES presence
 * in params.h, which only exist post-standardization). Public Domain / CC0 (see that repo's own
 * LICENSE file) -- Roberto Avanzi, Joppe Bos, Leo Ducas, Eike Kiltz, Tancrede Lepoint, Vadim
 * Lyubashevsky, John M. Schanck, Peter Schwabe, Gregor Seiler, Damien Stehle.
 *
 * EMILY/BACKLOG.md SECTION 536 follow-up, BIG_O/NORTHSTAR.md §29. Founder real-time (2026-09-24):
 * "add real SSH key generation" -> (after checking GFD's real server) "upgrade to quantum safe
 * encryption" -> "scope a real ML-DSA/Dilithium implementation" (explicit AskUserQuestion choice,
 * with the real, named tradeoff already surfaced: nothing this generates is usable against GFD's
 * real SSH server yet -- Go's golang.org/x/crypto/ssh has no post-quantum public-key AUTH
 * algorithm, checked directly, not assumed). ML-DSA-44 (NIST security category 2, the smallest of
 * the three FIPS 204 parameter sets) is the real v0 scope -- DILITHIUM_MODE is fixed to 2 in
 * config.h; the code is already generic over the other two sizes if that's ever needed, a real
 * config change, not a new implementation.
 *
 * Deliberately vendored, not hand-reimplemented from the FIPS 204 spec: this is lattice-based
 * cryptography (NTT-based polynomial ring arithmetic, rejection sampling, Fiat-Shamir with
 * aborts) -- a hand-written, unvalidated implementation of exactly this class of algorithm is a
 * real, serious way to introduce an exploitable vulnerability while believing it's "quantum
 * safe." Using the original team's own public-domain reference, verified byte-for-byte identical
 * to a fresh, unmodified checkout (see BIG_O/NORTHSTAR.md §29's own verification section), is the
 * responsible choice. randombytes.c is the one file whose entropy SOURCE matters most for real
 * security -- kept as-is (the upstream real getrandom()/CryptGenRandom()/`/dev/urandom` logic is
 * already correct and minimal; rewriting it would only add risk, not remove it).
 */
#ifndef POLY_H
#define POLY_H

#include <stdint.h>
#include "params.h"

typedef struct {
  int32_t coeffs[N];
} poly;

#define poly_reduce DILITHIUM_NAMESPACE(poly_reduce)
void poly_reduce(poly *a);
#define poly_caddq DILITHIUM_NAMESPACE(poly_caddq)
void poly_caddq(poly *a);

#define poly_add DILITHIUM_NAMESPACE(poly_add)
void poly_add(poly *c, const poly *a, const poly *b);
#define poly_sub DILITHIUM_NAMESPACE(poly_sub)
void poly_sub(poly *c, const poly *a, const poly *b);
#define poly_shiftl DILITHIUM_NAMESPACE(poly_shiftl)
void poly_shiftl(poly *a);

#define poly_ntt DILITHIUM_NAMESPACE(poly_ntt)
void poly_ntt(poly *a);
#define poly_invntt_tomont DILITHIUM_NAMESPACE(poly_invntt_tomont)
void poly_invntt_tomont(poly *a);
#define poly_pointwise_montgomery DILITHIUM_NAMESPACE(poly_pointwise_montgomery)
void poly_pointwise_montgomery(poly *c, const poly *a, const poly *b);

#define poly_power2round DILITHIUM_NAMESPACE(poly_power2round)
void poly_power2round(poly *a1, poly *a0, const poly *a);
#define poly_decompose DILITHIUM_NAMESPACE(poly_decompose)
void poly_decompose(poly *a1, poly *a0, const poly *a);
#define poly_make_hint DILITHIUM_NAMESPACE(poly_make_hint)
unsigned int poly_make_hint(poly *h, const poly *a0, const poly *a1);
#define poly_use_hint DILITHIUM_NAMESPACE(poly_use_hint)
void poly_use_hint(poly *b, const poly *a, const poly *h);

#define poly_chknorm DILITHIUM_NAMESPACE(poly_chknorm)
int poly_chknorm(const poly *a, int32_t B);
#define poly_uniform DILITHIUM_NAMESPACE(poly_uniform)
void poly_uniform(poly *a,
                  const uint8_t seed[SEEDBYTES],
                  uint16_t nonce);
#define poly_uniform_eta DILITHIUM_NAMESPACE(poly_uniform_eta)
void poly_uniform_eta(poly *a,
                      const uint8_t seed[CRHBYTES],
                      uint16_t nonce);
#define poly_uniform_gamma1 DILITHIUM_NAMESPACE(poly_uniform_gamma1)
void poly_uniform_gamma1(poly *a,
                         const uint8_t seed[CRHBYTES],
                         uint16_t nonce);
#define poly_challenge DILITHIUM_NAMESPACE(poly_challenge)
void poly_challenge(poly *c, const uint8_t seed[CTILDEBYTES]);

#define polyeta_pack DILITHIUM_NAMESPACE(polyeta_pack)
void polyeta_pack(uint8_t *r, const poly *a);
#define polyeta_unpack DILITHIUM_NAMESPACE(polyeta_unpack)
void polyeta_unpack(poly *r, const uint8_t *a);

#define polyt1_pack DILITHIUM_NAMESPACE(polyt1_pack)
void polyt1_pack(uint8_t *r, const poly *a);
#define polyt1_unpack DILITHIUM_NAMESPACE(polyt1_unpack)
void polyt1_unpack(poly *r, const uint8_t *a);

#define polyt0_pack DILITHIUM_NAMESPACE(polyt0_pack)
void polyt0_pack(uint8_t *r, const poly *a);
#define polyt0_unpack DILITHIUM_NAMESPACE(polyt0_unpack)
void polyt0_unpack(poly *r, const uint8_t *a);

#define polyz_pack DILITHIUM_NAMESPACE(polyz_pack)
void polyz_pack(uint8_t *r, const poly *a);
#define polyz_unpack DILITHIUM_NAMESPACE(polyz_unpack)
void polyz_unpack(poly *r, const uint8_t *a);

#define polyw1_pack DILITHIUM_NAMESPACE(polyw1_pack)
void polyw1_pack(uint8_t *r, const poly *a);

#endif
