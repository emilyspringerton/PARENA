"""stdlib/netkit/nk_crypto.prn: SHA-256, ChaCha20 (OpenSSH's 64-bit-counter layout) and Poly1305 against published
vectors and against independent implementations (hashlib, pyca/cryptography) on random inputs of every length class."""
import hashlib, os, random, sys
sys.path.insert(0, os.path.dirname(__file__))
from nkharness import *
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms
from cryptography.hazmat.primitives.poly1305 import Poly1305

nk = NK(sys.argv[1]); R = random.Random(7)
A, B, C = IN, IN + 0x40000, IN + 0x80000   # three scratch areas inside the IN region (this test never uses RFB)

def sha(data):
    nk.write(A, data); nk.nk_sha256(A, len(data), B); return nk.read(B, 32)

# ---- SHA-256 -----------------------------------------------------------------------------------------------------------------------
check(sha(b"") .hex() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 empty message (FIPS 180-4)")
check(sha(b"abc").hex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 'abc' (FIPS 180-4)")
check(sha(b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").hex() == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "SHA-256 448-bit message (FIPS 180-4)")
bad = [n for n in range(0, 300) if sha(bytes(R.randrange(256) for _ in range(n))) != None and False]
bad = []
for n in list(range(0, 260)) + [511, 512, 513, 1000, 4096, 9999]:
    d = bytes(R.randrange(256) for _ in range(n))
    if sha(d) != hashlib.sha256(d).digest(): bad.append(n)
check(not bad, f"SHA-256 matches hashlib for every length 0..259 and 511/512/513/1000/4096/9999 (mismatches: {bad[:5]})")
nk.write(FB, b"a" * 1000000); nk.nk_sha256(FB, 1000000, B)
check(nk.read(B, 32).hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "SHA-256 of one million 'a' (FIPS 180-4)")

# ---- ChaCha20 -------------------------------------------------------------------------------------------------------------------------------
def cc_oracle(key, ctr, nonce8, data):
    n16 = (ctr & 0xFFFFFFFF).to_bytes(4, "little") + (ctr >> 32).to_bytes(4, "little") + nonce8
    return Cipher(algorithms.ChaCha20(key, n16), mode=None).encryptor().update(data)
def cc(key, ctr, nonce8, data):
    nk.write(A, key); nk.write(A + 64, nonce8); nk.write(B, data)
    nk.nk_chacha20_xor(A, A + 64, ctr, B, C, len(data)); return nk.read(C, len(data))
key = bytes(range(32)); nonce = bytes.fromhex("0000004a00000000")   # RFC nonce 00000000 0000004a 00000000: word 0 is our counter_hi (0), words 1-2 are nonce8
pt = b"Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it."
ct = cc(key, 1, nonce, pt)
check(ct.hex().startswith("6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0bf91b65c5524733ab8f593dabcd62b3571639d624e65152ab8f530c359f0861d807ca0dbf500d6a6156a38e088a22b65e52bc514d16ccf806818ce91ab77937365af90bbf74a35be6b40b8eedf2785e42874d"),
      "ChaCha20 RFC 8439 s2.4.2 'sunscreen' vector (IETF counter=1 maps onto our counter_lo=1, nonce word 0 = counter_hi)")
bad = []
for _ in range(120):
    k = bytes(R.randrange(256) for _ in range(32)); n8 = bytes(R.randrange(256) for _ in range(8))
    ln = R.choice([0, 1, 3, 4, 63, 64, 65, 127, 128, 129, 200, 1000]); d = bytes(R.randrange(256) for _ in range(ln))
    c0 = R.choice([0, 1, 2, 255, 70000])
    if cc(k, c0, n8, d) != cc_oracle(k, c0, n8, d): bad.append((ln, c0))
check(not bad, f"ChaCha20 matches pyca/cryptography on 120 random key/nonce/counter/length cases ({bad[:3]})")
k = bytes(R.randrange(256) for _ in range(32)); n8 = bytes(8)
cross = b"".join(cc_oracle(k, c, n8, bytes(64 if c < (1 << 32) + 2 else 8)) for c in ((1 << 32) - 1, 1 << 32, (1 << 32) + 1, (1 << 32) + 2))
check(cc(k, (1 << 32) - 1, n8, bytes(200)) == cross, "ChaCha20 block counter carries into the high word within one message (64-bit counter, as OpenSSH)")
nk.write(A, k); nk.write(A + 64, n8); nk.nk_chacha20_polykey(A, A + 64, B)
check(nk.read(B, 32) == cc_oracle(k, 0, n8, bytes(32)), "Poly1305 one-time key = first 32 bytes of keystream block 0")
nk.write(B, pt); nk.nk_chacha20_xor(A, A + 64, 5, B, B, len(pt))
check(nk.read(B, len(pt)) == cc_oracle(k, 5, n8, pt), "ChaCha20 in-place (out == in) works")

# ---- Poly1305 ---------------------------------------------------------------------------------------------------------------------------------
def pl(key, msg):
    nk.write(A, key); nk.write(B, msg); nk.nk_poly1305(C, B, len(msg), A); return nk.read(C, 16)
k0 = bytes.fromhex("85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b")
check(pl(k0, b"Cryptographic Forum Research Group").hex() == "a8061dc1305136c6c22b8baf0c0127a9", "Poly1305 RFC 8439 s2.5.2 vector")
bad = []
for _ in range(300):
    kk = bytes(R.randrange(256) for _ in range(32)); ln = R.choice(list(range(0, 70)) + [127, 128, 129, 1000])
    m = bytes(R.randrange(256) for _ in range(ln))
    if pl(kk, m) != Poly1305.generate_tag(kk, m): bad.append(ln)
check(not bad, f"Poly1305 matches pyca/cryptography on 300 random key/message pairs, lengths 0..69,127..129,1000 ({bad[:3]})")
bad = []
for ln in (1, 15, 16, 17, 31, 32, 33, 64, 100):
    for fill in (0x00, 0xFF):                      # all-ones messages and keys push every limb to its carry extremes
        for kfill in (0x00, 0xFF):
            kk = bytes([kfill]) * 32; m = bytes([fill]) * ln
            if pl(kk, m) != Poly1305.generate_tag(kk, m): bad.append((ln, fill, kfill))
check(not bad, f"Poly1305 carry extremes (all-0x00 / all-0xFF keys and messages) match ({bad[:3]})")
# tag edge: messages engineered so that h lands in [p, 2^130): m = 2^128 repeated blocks of 0xFF with r = clamped max
bad = []
for _ in range(200):
    kk = bytes(R.choice([0, 255, R.randrange(256)]) for _ in range(32)); ln = R.choice([16, 32, 48, 64])
    m = bytes(R.choice([0, 255]) for _ in range(ln))
    if pl(kk, m) != Poly1305.generate_tag(kk, m): bad.append(ln)
check(not bad, f"Poly1305 structured 0x00/0xFF stress (final-reduction edge cases) matches ({bad[:3]})")
done()
