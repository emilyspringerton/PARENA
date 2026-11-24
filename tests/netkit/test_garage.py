"""nk_garage.prn against the real wasm build (the lite module). usage: test_garage.py <netkit.wasm>"""
import sys
from nkharness import NK, check, done
from garage_checks import run
nk = NK(sys.argv[1])
run(lambda n, *a: getattr(nk, n)(*a), lambda addr: nk.cstr(addr), check)
done()
