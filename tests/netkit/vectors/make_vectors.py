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
  term.vec     kind name_len name n stream screen_len screen             (pyte screens; see the term section at the end)
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

# ---- the terminal: pyte's screens (an independent VT100/xterm emulator), 34 rows rstripped and joined by "\n", UTF-8 -------------
#   term.vec     kind name_len name stream_len stream screen_len screen
#                kind 0 = a named case, 1/2 = random stream group A/B, 3 = garbage (no screen: only "no trap, in bounds"),
#                4 = the bash transcript (only its first 3 rows are compared)
import pyte
def py(data, rows=34):
    sc = pyte.Screen(100, 34); st = pyte.ByteStream(sc); st.feed(data); return "\n".join(l.rstrip() for l in sc.display[:rows]).encode()
def trec(kind, name, data, screen): return u32(kind) + u32(len(name)) + name + u32(len(data)) + data + u32(len(screen)) + screen
recs = []
cases = {
  "cup": b"\x1b[5;10Hxy\x1b[1;1Hab", "cuu/cud/cuf/cub": b"\x1b[10;10H\x1b[3Axx\x1b[2By\x1b[4Cz\x1b[6Dw", "clamp": b"\x1b[999;999Hq\x1b[0;0Hr\x1b[100Ds",
  "cha/vpa": b"abc\x1b[20Gd\x1b[12;1He\x1b[3dfg", "ed0": b"line1\r\nline2\r\nline3\x1b[2;3H\x1b[J", "ed1": b"line1\r\nline2\r\nline3\x1b[2;3H\x1b[1J",
  "ed2": b"line1\r\nline2\x1b[2J", "el0/1/2": b"0123456789\x1b[1;5H\x1b[K\r\n0123456789\x1b[2;5H\x1b[1K\r\n0123456789\x1b[3;5H\x1b[2K",
  "ich": b"abcdef\x1b[1;3H\x1b[2@XY", "dch": b"abcdef\x1b[1;3H\x1b[2P", "ech": b"abcdef\x1b[1;3H\x1b[3X",
  "il": b"1\r\n2\r\n3\r\n4\r\n5\x1b[2;1H\x1b[2L", "dl": b"1\r\n2\r\n3\r\n4\r\n5\x1b[2;1H\x1b[2M",
  "region": b"\x1b[1;1H" + b"".join(b"row%d\r\n" % i for i in range(30)) + b"\x1b[5;10r\x1b[10;1H\n\n\nbottom\x1b[5;1H\x1bMtop",
  "region+il": b"\x1b[1;34r" + b"".join(b"r%d\r\n" % i for i in range(12)) + b"\x1b[3;8r\x1b[4;1H\x1b[2L\x1b[5;1H\x1b[1M",
  "sgr": b"\x1b[1;31mred\x1b[0m plain \x1b[4munder\x1b[24m \x1b[7mrev\x1b[27m", "tabs": b"a\tb\t\tc", "bs": b"abc\b\bX\bY",
  "scroll": b"".join(b"line%02d\r\n" % i for i in range(60)), "save/restore": b"ab\x1b7\x1b[10;10Hcd\x1b8ef",
}
for name, data in cases.items(): recs.append(trec(0, name.encode(), data, py(data)))
R = random.Random(2026)
common = [b"\x1b[%d;%dH", b"\x1b[%dG", b"\x1b[%dd", b"\x1b[%dP", b"\x1b[%d@", b"\x1b[%dX", b"\x1b[K", b"\x1b[1K", b"\x1b[2K", b"\x1b[J", b"\x1b[1J",
          b"\x1b[m", b"\x1b[1m", b"\x1b[31m", b"\r", b"\n", b"\r\n", b"\r\n", b"\b", b"\t"]
group_a = common + [b"\x1b[%dA", b"\x1b[%dB", b"\x1b[%dC", b"\x1b[%dD", b"\x1b7", b"\x1bM", b"\x1bD"]
group_b = [b"\x1b[%dP", b"\x1b[%d@", b"\x1b[%dX", b"\x1b[K", b"\x1b[1K", b"\x1b[2K", b"\r", b"\n", b"\r\n", b"\r\n", b"\b", b"\x1bM"]
def rand_stream(seqset, n, region=None):
    out = b"\x1b[%d;%dr\x1b[%d;1H" % (region[0], region[1], region[0]) if region else b""
    for i in range(n):
        if i % 5 == 4: out += b"\r\n"
        if R.random() < 0.55: out += bytes(R.choice(b"abcdefghijklmnopqrstuvwxyz0123456789 .,:;-_") for _ in range(R.randint(1, 8)))
        else:
            s = R.choice(seqset); k = s.count(b"%d")
            out += s % tuple(R.randint(0, 12) for _ in range(k)) if k else s
    return out
for kind, gset, regioned in ((1, group_a, False), (2, group_b, True)):
    for i in range(150):
        region = None
        if regioned: t = R.randint(2, 18); region = (t, t + R.randint(2, 12))
        data = rand_stream(gset, R.randint(20, 120), region); recs.append(trec(kind, b"", data, py(data)))
for i in range(300):
    junk = bytes(R.randrange(256) for _ in range(R.randint(1, 400))) if i % 2 else b"".join(R.choice([b"\x1b[", b"\x1b", b"\x1b]", b";", b"?", b"99999", b"\x1b[%dr" % R.randint(0, 99), b"\x1b[%d;%dH" % (R.randint(0, 999), R.randint(0, 999)),
        b"\x1b[%dL" % R.randint(0, 9999), b"\x1b[%dM" % R.randint(0, 9999), b"\x1b[%d@" % R.randint(0, 9999), b"\x1b[%dP" % R.randint(0, 9999), b"\x1b[%dS" % R.randint(0, 9999),
        b"\x1b[?1049h", b"\x1b[?1049l", b"\x1b[?47h", b"\x1b[?47l", b"\n" * R.randint(1, 80), b"abc", "é".encode(), b"\xff", b"\xe2\x94"]) for _ in range(R.randint(1, 60)))
    recs.append(trec(3, b"", junk, b""))
bash = (b"ubuntu@pi:~$ ls\r\n\x1b[0m\x1b[01;34mDocuments\x1b[0m  \x1b[01;32mscript.sh\x1b[0m  notes.txt\r\nubuntu@pi:~$ echo helo"
        b"\x08\x1b[Klo wor\x08\x08\x08\x1b[P\x1b[1@x\x1b[3C")
recs.append(trec(4, b"a realistic bash transcript (colours, backspace + EL, DCH/ICH line editing)", bash, py(bash, 3)))
put("term.vec", recs)
