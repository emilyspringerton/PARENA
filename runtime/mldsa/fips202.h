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
#ifndef FIPS202_H
#define FIPS202_H

#include <stddef.h>
#include <stdint.h>

#define SHAKE128_RATE 168
#define SHAKE256_RATE 136
#define SHA3_256_RATE 136
#define SHA3_512_RATE 72

#define FIPS202_NAMESPACE(s) pqcrystals_dilithium_fips202_ref_##s

typedef struct {
  uint64_t s[25];
  unsigned int pos;
} keccak_state;

#define KeccakF_RoundConstants FIPS202_NAMESPACE(KeccakF_RoundConstants)
extern const uint64_t KeccakF_RoundConstants[];

#define shake128_init FIPS202_NAMESPACE(shake128_init)
void shake128_init(keccak_state *state);
#define shake128_absorb FIPS202_NAMESPACE(shake128_absorb)
void shake128_absorb(keccak_state *state, const uint8_t *in, size_t inlen);
#define shake128_finalize FIPS202_NAMESPACE(shake128_finalize)
void shake128_finalize(keccak_state *state);
#define shake128_squeeze FIPS202_NAMESPACE(shake128_squeeze)
void shake128_squeeze(uint8_t *out, size_t outlen, keccak_state *state);
#define shake128_absorb_once FIPS202_NAMESPACE(shake128_absorb_once)
void shake128_absorb_once(keccak_state *state, const uint8_t *in, size_t inlen);
#define shake128_squeezeblocks FIPS202_NAMESPACE(shake128_squeezeblocks)
void shake128_squeezeblocks(uint8_t *out, size_t nblocks, keccak_state *state);

#define shake256_init FIPS202_NAMESPACE(shake256_init)
void shake256_init(keccak_state *state);
#define shake256_absorb FIPS202_NAMESPACE(shake256_absorb)
void shake256_absorb(keccak_state *state, const uint8_t *in, size_t inlen);
#define shake256_finalize FIPS202_NAMESPACE(shake256_finalize)
void shake256_finalize(keccak_state *state);
#define shake256_squeeze FIPS202_NAMESPACE(shake256_squeeze)
void shake256_squeeze(uint8_t *out, size_t outlen, keccak_state *state);
#define shake256_absorb_once FIPS202_NAMESPACE(shake256_absorb_once)
void shake256_absorb_once(keccak_state *state, const uint8_t *in, size_t inlen);
#define shake256_squeezeblocks FIPS202_NAMESPACE(shake256_squeezeblocks)
void shake256_squeezeblocks(uint8_t *out, size_t nblocks,  keccak_state *state);

#define shake128 FIPS202_NAMESPACE(shake128)
void shake128(uint8_t *out, size_t outlen, const uint8_t *in, size_t inlen);
#define shake256 FIPS202_NAMESPACE(shake256)
void shake256(uint8_t *out, size_t outlen, const uint8_t *in, size_t inlen);
#define sha3_256 FIPS202_NAMESPACE(sha3_256)
void sha3_256(uint8_t h[32], const uint8_t *in, size_t inlen);
#define sha3_512 FIPS202_NAMESPACE(sha3_512)
void sha3_512(uint8_t h[64], const uint8_t *in, size_t inlen);

#endif
