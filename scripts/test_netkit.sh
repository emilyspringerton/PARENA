#!/usr/bin/env bash
# Builds netkit.wasm from stdlib/netkit/*.prn and runs every netkit test against the built artifact:
#   - DES          vs an independent implementation (pycryptodome) + the NIST vector
#   - RFB (VNC)    a scripted fake server, every scenario fed whole / byte-by-byte / in random chunks
#   - SSH          a PARENA SSH client vs asyncssh, and vs the real OpenSSH sshd (unprivileged, libnss_wrapper)
#   - interop      a REAL VNC server (libvncserver, TightVNC's lineage; tests/netkit/vncd.c): real auth, real encoders,
#                  final screen compared byte for byte. Skipped (loudly) if libvncserver headers are not installed.
# Needs: the LLVM toolchain (see build_netkit_wasm.sh), python3 with wasmtime, pycryptodome, cryptography, asyncssh;
#        openssh-server + libnss-wrapper for the real-OpenSSH leg (skipped, loudly, without them).
#   VNC_ROOT=<dir>   an extracted libvncserver1+libvncserver-dev (dpkg -x) when it is not installed system-wide
set -euo pipefail
cd "$(dirname "$0")/.."
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
scripts/build_netkit_wasm.sh "$W/netkit.wasm"
for t in tests/netkit/test_*.py; do
  case "$t" in */test_rfb_interop.py) continue;; esac
  echo "== $(basename "$t")"
  python3 "$t" "$W/netkit.wasm" | tail -3
done
echo "== test_rfb_interop.py (real VNC server)"
INC=(); LIB=(); LD=""
if [ -n "${VNC_ROOT:-}" ]; then INC=(-I"$VNC_ROOT/usr/include"); LIB=(-L"$VNC_ROOT/usr/lib/x86_64-linux-gnu" -Wl,-rpath,"$VNC_ROOT/usr/lib/x86_64-linux-gnu"); LD="$VNC_ROOT/usr/lib/x86_64-linux-gnu"; fi
if gcc -O1 -o "$W/vncd" tests/netkit/vncd.c "${INC[@]}" "${LIB[@]}" -lvncserver 2>/dev/null; then
  python3 tests/netkit/test_rfb_interop.py "$W/netkit.wasm" "$W/vncd" ${LD:+"$LD"} | tail -3
else
  echo "SKIP: libvncserver headers not found (apt install libvncserver-dev, or set VNC_ROOT)"
fi
