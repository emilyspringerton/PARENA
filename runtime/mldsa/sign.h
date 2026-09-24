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
#ifndef SIGN_H
#define SIGN_H

#include <stddef.h>
#include <stdint.h>
#include "params.h"
#include "polyvec.h"
#include "poly.h"

#define crypto_sign_keypair DILITHIUM_NAMESPACE(keypair)
int crypto_sign_keypair(uint8_t *pk, uint8_t *sk);

#define crypto_sign_signature_internal DILITHIUM_NAMESPACE(signature_internal)
int crypto_sign_signature_internal(uint8_t *sig,
                                   size_t *siglen,
                                   const uint8_t *m,
                                   size_t mlen,
                                   const uint8_t *pre,
                                   size_t prelen,
                                   const uint8_t rnd[RNDBYTES],
                                   const uint8_t *sk);

#define crypto_sign_signature DILITHIUM_NAMESPACE(signature)
int crypto_sign_signature(uint8_t *sig, size_t *siglen,
                          const uint8_t *m, size_t mlen,
                          const uint8_t *ctx, size_t ctxlen,
                          const uint8_t *sk);

#define crypto_sign DILITHIUM_NAMESPACETOP
int crypto_sign(uint8_t *sm, size_t *smlen,
                const uint8_t *m, size_t mlen,
                const uint8_t *ctx, size_t ctxlen,
                const uint8_t *sk);

#define crypto_sign_verify_internal DILITHIUM_NAMESPACE(verify_internal)
int crypto_sign_verify_internal(const uint8_t *sig,
                                size_t siglen,
                                const uint8_t *m,
                                size_t mlen,
                                const uint8_t *pre,
                                size_t prelen,
                                const uint8_t *pk);

#define crypto_sign_verify DILITHIUM_NAMESPACE(verify)
int crypto_sign_verify(const uint8_t *sig, size_t siglen,
                       const uint8_t *m, size_t mlen,
                       const uint8_t *ctx, size_t ctxlen,
                       const uint8_t *pk);

#define crypto_sign_open DILITHIUM_NAMESPACE(open)
int crypto_sign_open(uint8_t *m, size_t *mlen,
                     const uint8_t *sm, size_t smlen,
                     const uint8_t *ctx, size_t ctxlen,
                     const uint8_t *pk);

#endif
