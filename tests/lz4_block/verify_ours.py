#!/usr/bin/env python3
"""Have liblz4 decode the .bin files our PARENA compressor wrote. usage: verify_ours.py <dir>"""
import sys, lz4.block as B
sys.path.insert(0, ".")
import re
src = open("tests/lz4_block/kat.h").read()
raws = [bytes(int(x) for x in m.group(1).split(",")) for m in re.finditer(r"kat_raw_\d+\[\] = \{([^}]*)\}", src)]
bad = 0
for i, raw in enumerate(raws):
    ours = open("%s/ours_%d.bin" % (sys.argv[1], i), "rb").read()
    try:
        ok = B.decompress(ours, uncompressed_size=len(raw)) == raw
    except Exception as e:
        ok = False; print("  liblz4 error:", e)
    print(("PASS" if ok else "FAIL") + ": liblz4 decodes our output for vector %d (%d bytes)" % (i, len(ours)))
    bad += 0 if ok else 1
print("ALL PASSED" if not bad else "FAILED")
sys.exit(1 if bad else 0)
