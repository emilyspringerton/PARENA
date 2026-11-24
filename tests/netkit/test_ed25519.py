"""stdlib/netkit/nk_sha512.prn + nk_ed25519.prn: SHA-512 vs hashlib; the mod-L reduction vs Python big integers;
Ed25519 verification vs RFC 8032 and pyca/cryptography -- valid signatures accepted; every tampering rejected."""
import hashlib, os, random, sys, time
sys.path.insert(0, os.path.dirname(__file__))
from nkharness import *
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

nk = NK(sys.argv[1]); R = random.Random(8032)
A, B, C, D = IN, IN + 0x10000, IN + 0x20000, IN + 0x30000
Lq = 2 ** 252 + 27742317777372353535851937790883648493

# ---- SHA-512 ------------------------------------------------------------------------------------------------------------------------------------
def sha512(d): nk.write(A, d); nk.nk_sha512(A, len(d), B); return nk.read(B, 64)
bad = [n for n in list(range(0, 300)) + [511, 512, 1000, 4095] if sha512(bytes(R.randrange(256) for _ in range(n))) != None and False]
bad = []
for n in list(range(0, 300)) + [511, 512, 1000, 4095]:
    d = bytes(R.randrange(256) for _ in range(n))
    if sha512(d) != hashlib.sha512(d).digest(): bad.append(n)
check(not bad, f"SHA-512 matches hashlib for every length 0..299 and 511/512/1000/4095 ({bad[:4]})")
check(sha512(b"abc").hex() == "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f", "SHA-512 'abc' (FIPS 180-4)")

# ---- mod L ------------------------------------------------------------------------------------------------------------------------------------------
def modl(x64): nk.write(A, x64); nk.nk_ed25519_modl(B, A); return int.from_bytes(nk.read(B, 32), "little")
bad = 0
cases = [bytes(64), bytes([255]) * 64, (Lq).to_bytes(64, "little"), (Lq * 12345).to_bytes(64, "little"), (Lq - 1).to_bytes(64, "little"), (2 ** 512 - 1 - Lq).to_bytes(64, "little")]
cases += [bytes(R.randrange(256) for _ in range(64)) for _ in range(300)]
cases += [bytes([R.choice([0, 255]) for _ in range(64)]) for _ in range(100)]
for x in cases:
    if modl(x) != int.from_bytes(x, "little") % Lq: bad += 1
check(bad == 0, f"mod-L reduction matches Python big integers on {len(cases)} inputs (0, all-ones, multiples of L, L-1, random, 0x00/0xFF mixes)")

# ---- Ed25519 -------------------------------------------------------------------------------------------------------------------------------------------
def verify(pk, msg, sig):
    nk.write(A, pk); nk.write(B, msg); nk.write(C, sig); return nk.nk_ed25519_verify(A, B, len(msg), C)
pk1 = bytes.fromhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")
sig1 = bytes.fromhex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b")
t = time.time(); ok1 = verify(pk1, b"", sig1); dt = time.time() - t
check(ok1 == 1, "RFC 8032 test vector 1 (empty message) verifies")
print(f"      one Ed25519 verification: {dt*1000:.0f} ms under wasmtime")
pk2 = bytes.fromhex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c"); sig2 = bytes.fromhex("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00")
check(verify(pk2, bytes.fromhex("72"), sig2) == 1, "RFC 8032 test vector 2 (one byte message) verifies")
check(verify(pk1, b"", sig1[:63] + bytes([sig1[63] ^ 1])) == 0, "RFC 8032 vector 1 with one bit of S flipped is rejected")

bad_valid = 0; bad_tamper = 0; ntamper = 0
for n in (0, 1, 31, 32, 33, 64, 100, 200):
    for _ in range(3):
        sk = Ed25519PrivateKey.generate(); pk = sk.public_key().public_bytes_raw(); msg = bytes(R.randrange(256) for _ in range(n)); sig = sk.sign(msg)
        if verify(pk, msg, sig) != 1: bad_valid += 1
        # tamper with every region: R (a few bits), S (a few bits), the message, the key
        for region, off in (("R", R.randrange(32)), ("R", R.randrange(32)), ("S", 32 + R.randrange(31)), ("S", 32 + R.randrange(31))):
            s2 = bytearray(sig); s2[off] ^= 1 << R.randrange(8); ntamper += 1
            if verify(pk, msg, bytes(s2)) != 0: bad_tamper += 1
        if n:
            m2 = bytearray(msg); m2[R.randrange(n)] ^= 1 << R.randrange(8); ntamper += 1
            if verify(pk, bytes(m2), sig) != 0: bad_tamper += 1
        pk2b = bytearray(pk); pk2b[R.randrange(31)] ^= 1 << R.randrange(8); ntamper += 1
        if verify(bytes(pk2b), msg, sig) != 0: bad_tamper += 1
check(bad_valid == 0, "24 random valid signatures (message lengths 0..200) verify")
check(bad_tamper == 0, f"{ntamper} single-bit tamperings of R, S, the message and the public key are all rejected")
# malleability: S + L is the same signature mod L but not canonical -- RFC 8032 s5.1.7 says reject
sk = Ed25519PrivateKey.generate(); pk = sk.public_key().public_bytes_raw(); msg = b"malleability"; sig = sk.sign(msg)
s_int = int.from_bytes(sig[32:], "little"); mall = sig[:32] + (s_int + Lq).to_bytes(32, "little")
check(verify(pk, msg, sig) == 1 and verify(pk, msg, mall) == 0, "S + L (a malleable twin of a valid signature) is rejected")
check(verify(pk, msg, sig[:32] + (Lq).to_bytes(32, "little")) == 0, "S = L exactly is rejected")
# invalid public-key encodings
p = 2 ** 255 - 19
check(verify((p).to_bytes(32, "little"), msg, sig) == 0, "a public key with y = p (non-canonical) is rejected")
check(verify((p + 3).to_bytes(32, "little"), msg, sig) == 0, "a public key with y >= p is rejected")
check(verify(bytes([2]) + bytes(31), msg, sig) == 0, "a y that is not on the curve (no square root) is rejected")
check(verify(bytes([0]) + bytes(30) + bytes([0x80]), msg, sig) == 0, "x = 0 encoded with the sign bit set is rejected")
check(verify(bytes(32), msg, sig) == 0, "the all-zero key is rejected")
# wrong key for a valid signature
sk2 = Ed25519PrivateKey.generate(); check(verify(sk2.public_key().public_bytes_raw(), msg, sig) == 0, "a valid signature does not verify under a different key")
done()
