import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from nkharness import *
from Crypto.Cipher import DES

nk = NK(sys.argv[1])
KEY, INP, OUTP = SCR + 2048, SCR + 2112, SCR + 2176

def des(key, block, enc):
    nk.write(KEY, key); nk.write(INP, block)
    nk.nk_des_test(KEY, INP, OUTP, enc)
    return nk.read(OUTP, 8)

nist_k, nist_p, nist_c = bytes.fromhex("133457799BBCDFF1"), bytes.fromhex("0123456789ABCDEF"), bytes.fromhex("85E813540F0AB405")
check(des(nist_k, nist_p, 1) == nist_c, "DES encrypt: NIST vector 133457799BBCDFF1 / 0123456789ABCDEF -> 85E813540F0AB405")
check(des(nist_k, nist_c, 0) == nist_p, "DES decrypt: NIST vector round-trips")
# in-place (out == in)
nk.write(KEY, nist_k); nk.write(INP, nist_p); nk.nk_des_test(KEY, INP, INP, 1)
check(nk.read(INP, 8) == nist_c, "DES works in place (out == in)")
# weak/odd keys and all-zero/all-one blocks
for k in (b"\0" * 8, b"\xff" * 8, bytes.fromhex("0101010101010101"), bytes.fromhex("FEFEFEFEFEFEFEFE")):
    for m in (b"\0" * 8, b"\xff" * 8):
        check(des(k, m, 1) == DES.new(k, DES.MODE_ECB).encrypt(m), f"DES matches pycryptodome for key {k.hex()} block {m.hex()}")
bad = 0
for _ in range(500):
    k, m = os.urandom(8), os.urandom(8)
    ok = des(k, m, 1) == DES.new(k, DES.MODE_ECB).encrypt(m) and des(k, m, 0) == DES.new(k, DES.MODE_ECB).decrypt(m)
    bad += (not ok)
check(bad == 0, "DES matches pycryptodome on 500 random key/block pairs, both directions")
done()
