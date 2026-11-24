"""stdlib/netkit/nk_x25519.prn against RFC 7748 and pyca/cryptography (random scalars/points, edge points)."""
import os, random, sys, time
sys.path.insert(0, os.path.dirname(__file__))
from nkharness import *
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey

nk = NK(sys.argv[1]); R = random.Random(25519)
A, B, C = IN, IN + 64, IN + 128
def x25519(k, u):
    nk.write(A, k); nk.write(B, u); nk.nk_x25519(C, A, B); return nk.read(C, 32)
def oracle(k, u):
    return X25519PrivateKey.from_private_bytes(k).exchange(X25519PublicKey.from_public_bytes(u))
H = bytes.fromhex
BASE = bytes([9]) + bytes(31)

t = time.time(); x25519(H("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4"), H("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c")); dt = time.time() - t
check(x25519(H("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4"), H("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c")).hex()
      == "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552", "RFC 7748 s5.2 vector 1")
check(x25519(H("4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d"), H("e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493")).hex()
      == "95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957", "RFC 7748 s5.2 vector 2 (u has its top bit set: it must be masked)")
print(f"      one X25519 scalar multiplication: {dt*1000:.1f} ms under wasmtime")

ap = H("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a"); bp = H("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb")
apub, bpub = x25519(ap, BASE), x25519(bp, BASE)
check(apub.hex() == "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", "RFC 7748 s6.1 Alice's public key")
check(bpub.hex() == "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", "RFC 7748 s6.1 Bob's public key")
check(x25519(ap, bpub) == x25519(bp, apub) and x25519(ap, bpub).hex() == "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", "RFC 7748 s6.1 shared secret, both directions")

bad = 0
for _ in range(60):
    k = bytes(R.randrange(256) for _ in range(32)); u = bytes(R.randrange(256) for _ in range(32))
    try: want = oracle(k, u)
    except ValueError: continue
    bad += (x25519(k, u) != want)
check(bad == 0, "60 random scalars x random points match pyca/cryptography")
bad = 0
for _ in range(40):
    k = bytes(R.randrange(256) for _ in range(32)); pubw = X25519PrivateKey.from_private_bytes(k).public_key().public_bytes_raw()
    bad += (x25519(k, BASE) != pubw)
check(bad == 0, "40 random public keys (scalar x basepoint 9) match")
# extreme scalars and points: all-ones / all-zero scalars, u near p, u = 0 and 1 give low-order outputs (cryptography refuses them; compare to RFC math instead)
for k in (bytes([255]) * 32, bytes(32), bytes([0xF8]) + bytes(30) + bytes([0x7F])):
    for u in (BASE, bytes([255]) * 31 + bytes([127]), H("ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f"), bytes([2]) + bytes(31)):
        try: want = oracle(k, u)
        except ValueError: continue
        check(x25519(k, u) == want, f"extreme case k={k[:2].hex()}.. u={u[:2].hex()}.. matches")
# low-order points. pyca/cryptography refuses them, so the reference here is RFC 7748's own pure-Python ladder (below), which
# is itself checked against cryptography on random inputs first. With a clamped (multiple-of-8) scalar every point of order
# dividing 8 maps to the all-zero output, which the SSH layer must reject (RFC 8731, OpenSSH do).
P = 2 ** 255 - 19
def ref(k, u):
    kk = bytearray(k); kk[0] &= 248; kk[31] &= 127; kk[31] |= 64
    kn = int.from_bytes(kk, "little"); x1 = int.from_bytes(u, "little") & ((1 << 255) - 1)
    x2, z2, x3, z3, swap = 1, 0, x1, 1, 0
    for t in range(254, -1, -1):
        kt = (kn >> t) & 1; swap ^= kt
        if swap: x2, x3, z2, z3 = x3, x2, z3, z2
        swap = kt
        A = (x2 + z2) % P; AA = A * A % P; B = (x2 - z2) % P; BB = B * B % P; E = (AA - BB) % P
        C = (x3 + z3) % P; D = (x3 - z3) % P; DA = D * A % P; CB = C * B % P
        x3 = (DA + CB) ** 2 % P; z3 = x1 * (DA - CB) ** 2 % P; x2 = AA * BB % P; z2 = E * (AA + 121665 * E) % P
    if swap: x2, x3, z2, z3 = x3, x2, z3, z2
    return (x2 * pow(z2, P - 2, P) % P).to_bytes(32, "little")
for _ in range(5):
    k = bytes(R.randrange(256) for _ in range(32)); u = bytes(R.randrange(256) for _ in range(32))
    check(ref(k, u) == oracle(k, u), "(the pure-Python RFC 7748 reference agrees with pyca/cryptography)")
kk = H("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4")
lows = [bytes(32), bytes([1]) + bytes(31), (P - 1).to_bytes(32, "little"), P.to_bytes(32, "little"), (P + 1).to_bytes(32, "little"),
        H("e0eb7a7c3b41b8ae1656e3faf19fc46ada098deb9c32b1fd866205165f49b800"),
        H("5f9c95bca3508c24b1d0b1559c83ef5b04445cc4581c8e86d8224eddd09f1157")]
for ub in lows:
    got = x25519(kk, ub)
    check(got == ref(kk, ub) == bytes(32), f"low-order point u={ub[:4].hex()}.. yields the all-zero output in wasm and in the reference (SSH rejects it)")
# in-place
nk.write(A, ap); nk.write(B, BASE); nk.nk_x25519(A, A, B)
check(nk.read(A, 32) == apub, "out may alias the scalar input")
# the chain: 1 and 1000 iterations (RFC 7748 s5.2)
k = u = BASE
for it in range(1000):
    k, u = x25519(k, u), k
    if it == 0: check(k.hex() == "422c8e7a6227d7bca1350b3e2bb7279f7897b87bb6854b783c60e80311ae3079", "RFC 7748 s5.2 iterated, 1 iteration")
check(k.hex() == "684cf59ba83309552800ef566f2f4d3c1c3887c49360e3875f2eb94d99532c51", "RFC 7748 s5.2 iterated, 1000 iterations")
done()
