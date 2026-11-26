"""NOT A TEST, and not run by any test: the record of where tests/netkit/vectors/*.vec came from. The SCARAB specs
(tests/netkit/*_spec.prn) check PARENA's crypto against these frozen answers from independent implementations -- hashlib,
pycryptodome, pyca/cryptography, Python big integers -- on the same seeded inputs the old Python tests drew. Regenerate only to
widen coverage:  python3 tests/netkit/vectors/make_vectors.py tests/netkit/vectors   (deterministic: same seeds, same files)
Formats (all integers u32 little-endian):
  des.vec      key8 block8 encrypt8 decrypt8                           (8 weak/edge keys x 2 blocks, then 500 random)
  sha256.vec   n data[n] digest32                                     (lengths 0..259, 511, 512, 513, 1000, 4096, 9999)
  chacha.vec   key32 nonce8 ctr_lo ctr_hi n data[n] out[n]             (120 random; then the carry case, the poly key, in place)
  poly.vec     key32 n msg[n] tag16                                   (300 random, 36 carry extremes, 200 structured)
  x25519.vec   k32 u32 out32                                          (60 random, 40 x basepoint, extremes, 7 low-order)
  sha512.vec   n data[n] digest64                                     (lengths 0..299, 511, 512, 1000, 4095)
  modl.vec     x64 r32                                                (406 cases)
  ed25519.vec  pk32 n msg[n] sig64 expect                             (expect 1 = must verify, 0 = must be rejected)
"""
import hashlib, os, random, struct, sys
from Crypto.Cipher import DES
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms
from cryptography.hazmat.primitives.poly1305 import Poly1305
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
out = sys.argv[1]; u32 = lambda v: struct.pack("<I", v & 0xFFFFFFFF)
def put(name, recs): open(os.path.join(out, name), "wb").write(b"".join(recs)); print(name, len(recs))

R = random.Random(1975); recs = []
for k in (b"\0" * 8, b"\xff" * 8, bytes.fromhex("0101010101010101"), bytes.fromhex("FEFEFEFEFEFEFEFE")):
    for m in (b"\0" * 8, b"\xff" * 8): recs.append(k + m + DES.new(k, DES.MODE_ECB).encrypt(m) + DES.new(k, DES.MODE_ECB).decrypt(m))
for _ in range(500):
    k = bytes(R.randrange(256) for _ in range(8)); m = bytes(R.randrange(256) for _ in range(8))
    recs.append(k + m + DES.new(k, DES.MODE_ECB).encrypt(m) + DES.new(k, DES.MODE_ECB).decrypt(m))
put("des.vec", recs)

R = random.Random(7); recs = []
for n in list(range(0, 260)) + [511, 512, 513, 1000, 4096, 9999]:
    d = bytes(R.randrange(256) for _ in range(n)); recs.append(u32(n) + d + hashlib.sha256(d).digest())
put("sha256.vec", recs)

def cc(key, ctr, n8, data):
    return Cipher(algorithms.ChaCha20(key, u32(ctr) + u32(ctr >> 32) + n8), mode=None).encryptor().update(data)
def ccrec(k, c, n8, d): return k + n8 + u32(c) + u32(c >> 32) + u32(len(d)) + d + cc(k, c, n8, d)
recs = []
for _ in range(120):
    k = bytes(R.randrange(256) for _ in range(32)); n8 = bytes(R.randrange(256) for _ in range(8))
    ln = R.choice([0, 1, 3, 4, 63, 64, 65, 127, 128, 129, 200, 1000]); d = bytes(R.randrange(256) for _ in range(ln)); c0 = R.choice([0, 1, 2, 255, 70000])
    recs.append(ccrec(k, c0, n8, d))
k = bytes(R.randrange(256) for _ in range(32)); n8 = bytes(8)
cross = b"".join(cc(k, c, n8, bytes(64 if c < (1 << 32) + 2 else 8)) for c in ((1 << 32) - 1, 1 << 32, (1 << 32) + 1, (1 << 32) + 2))
recs.append(k + n8 + u32((1 << 32) - 1) + u32(0) + u32(200) + bytes(200) + cross)       # the counter carries into the high word
recs.append(ccrec(k, 0, n8, bytes(32)))                                                  # the Poly1305 one-time key
pt = b"Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it."
recs.append(ccrec(k, 5, n8, pt))                                                         # in place
put("chacha.vec", recs)

recs = []
for _ in range(300):
    kk = bytes(R.randrange(256) for _ in range(32)); ln = R.choice(list(range(0, 70)) + [127, 128, 129, 1000]); m = bytes(R.randrange(256) for _ in range(ln))
    recs.append(kk + u32(ln) + m + Poly1305.generate_tag(kk, m))
for ln in (1, 15, 16, 17, 31, 32, 33, 64, 100):
    for fill in (0x00, 0xFF):
        for kfill in (0x00, 0xFF):
            kk = bytes([kfill]) * 32; m = bytes([fill]) * ln; recs.append(kk + u32(ln) + m + Poly1305.generate_tag(kk, m))
for _ in range(200):
    kk = bytes(R.choice([0, 255, R.randrange(256)]) for _ in range(32)); ln = R.choice([16, 32, 48, 64]); m = bytes(R.choice([0, 255]) for _ in range(ln))
    recs.append(kk + u32(ln) + m + Poly1305.generate_tag(kk, m))
put("poly.vec", recs)

R = random.Random(25519); recs = []; H = bytes.fromhex; BASE = bytes([9]) + bytes(31); P = 2 ** 255 - 19
def xo(k, u): return X25519PrivateKey.from_private_bytes(k).exchange(X25519PublicKey.from_public_bytes(u))
for _ in range(60):
    k = bytes(R.randrange(256) for _ in range(32)); u = bytes(R.randrange(256) for _ in range(32))
    try: recs.append(k + u + xo(k, u))
    except ValueError: pass
for _ in range(40):
    k = bytes(R.randrange(256) for _ in range(32)); recs.append(k + BASE + X25519PrivateKey.from_private_bytes(k).public_key().public_bytes_raw())
for k in (bytes([255]) * 32, bytes(32), bytes([0xF8]) + bytes(30) + bytes([0x7F])):
    for u in (BASE, bytes([255]) * 31 + bytes([127]), H("ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f"), bytes([2]) + bytes(31)):
        try: recs.append(k + u + xo(k, u))
        except ValueError: pass
kk = H("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4")
for ub in [bytes(32), bytes([1]) + bytes(31), (P - 1).to_bytes(32, "little"), P.to_bytes(32, "little"), (P + 1).to_bytes(32, "little"),
           H("e0eb7a7c3b41b8ae1656e3faf19fc46ada098deb9c32b1fd866205165f49b800"), H("5f9c95bca3508c24b1d0b1559c83ef5b04445cc4581c8e86d8224eddd09f1157")]:
    recs.append(kk + ub + bytes(32))     # low-order points: the all-zero output (RFC 7748 math; cryptography refuses them)
put("x25519.vec", recs)

R = random.Random(8032); recs = []; Lq = 2 ** 252 + 27742317777372353535851937790883648493
for n in list(range(0, 300)) + [511, 512, 1000, 4095]:
    d = bytes(R.randrange(256) for _ in range(n)); recs.append(u32(n) + d + hashlib.sha512(d).digest())
put("sha512.vec", recs)
cases = [bytes(64), bytes([255]) * 64, Lq.to_bytes(64, "little"), (Lq * 12345).to_bytes(64, "little"), (Lq - 1).to_bytes(64, "little"), (2 ** 512 - 1 - Lq).to_bytes(64, "little")]
cases += [bytes(R.randrange(256) for _ in range(64)) for _ in range(300)] + [bytes([R.choice([0, 255]) for _ in range(64)]) for _ in range(100)]
put("modl.vec", [x + (int.from_bytes(x, "little") % Lq).to_bytes(32, "little") for x in cases])

recs = []
def ed(pk, msg, sig, ok): recs.append(pk + u32(len(msg)) + msg + sig + u32(ok))
def key(): return Ed25519PrivateKey.from_private_bytes(bytes(R.randrange(256) for _ in range(32)))
for n in (0, 1, 31, 32, 33, 64, 100, 200):
    for _ in range(3):
        sk = key(); pk = sk.public_key().public_bytes_raw(); msg = bytes(R.randrange(256) for _ in range(n)); sig = sk.sign(msg)
        ed(pk, msg, sig, 1)
        for off in (R.randrange(32), R.randrange(32), 32 + R.randrange(31), 32 + R.randrange(31)):
            s2 = bytearray(sig); s2[off] ^= 1 << R.randrange(8); ed(pk, msg, bytes(s2), 0)
        if n:
            m2 = bytearray(msg); m2[R.randrange(n)] ^= 1 << R.randrange(8); ed(pk, bytes(m2), sig, 0)
        p2 = bytearray(pk); p2[R.randrange(31)] ^= 1 << R.randrange(8); ed(bytes(p2), msg, sig, 0)
sk = key(); pk = sk.public_key().public_bytes_raw(); msg = b"malleability"; sig = sk.sign(msg); s_int = int.from_bytes(sig[32:], "little")
ed(pk, msg, sig, 1); ed(pk, msg, sig[:32] + (s_int + Lq).to_bytes(32, "little"), 0); ed(pk, msg, sig[:32] + Lq.to_bytes(32, "little"), 0)
for bad in (P.to_bytes(32, "little"), (P + 3).to_bytes(32, "little"), bytes([2]) + bytes(31), bytes([0]) + bytes(30) + bytes([0x80]), bytes(32),
            key().public_key().public_bytes_raw()):
    ed(bad, msg, sig, 0)
put("ed25519.vec", recs)
