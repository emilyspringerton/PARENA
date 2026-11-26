#!/usr/bin/env bash
# Builds netkit.wasm from stdlib/netkit/*.prn and runs every netkit test against the built artifact:
#   - terminal     the SSH terminal emulator (xterm-style, raw keys) vs pyte and exact checks
#   - DES          vs an independent implementation (pycryptodome) + the NIST vector
#   - RFB (VNC)    a scripted fake server, every scenario fed whole / byte-by-byte / in random chunks
#   - SSH          a PARENA SSH client vs asyncssh, and vs the real OpenSSH sshd (unprivileged, libnss_wrapper)
#   - interop      a REAL VNC server (libvncserver, TightVNC's lineage; tests/netkit/vncd.c): real auth, real encoders,
#                  final screen compared byte for byte. Skipped (loudly) if libvncserver headers are not installed.
# Needs: the LLVM toolchain (see build_netkit_wasm.sh), node 24+ (SCARAB suites), python3 with wasmtime, asyncssh, pyte (the rest);
#        openssh-server + libnss-wrapper for the real-OpenSSH leg (skipped, loudly, without them).
#   VNC_ROOT=<dir>   an extracted libvncserver1+libvncserver-dev (dpkg -x) when it is not installed system-wide
set -euo pipefail
cd "$(dirname "$0")/.."
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
scripts/build_netkit_wasm.sh "$W/netkit.wasm"
scripts/build_netkit_term_wasm.sh "$W/nkterm.wasm"
# ---- SCARAB/LADYBUG suites (PARENA specs compiled INTO the module they test; stdlib/scarab/wasm.prn, scripts/scarab_wasm.sh) ----
echo "== scarab/ladybug self-test"
tests/scarab/run_self_test.sh | tail -1
scripts/build_netkit_oui.sh "$W/oui.bin" >/dev/null
echo "== names_spec.prn (nk_names.prn)"
scripts/scarab_wasm.sh tests/netkit/names_spec.prn --modules stdlib/netkit/MODULES --file "$W/oui.bin" --file stdlib/netkit/data/oui_brands.tsv | tail -3
echo "== scan_spec.prn (nk_scan.prn)"
scripts/scarab_wasm.sh tests/netkit/scan_spec.prn --modules stdlib/netkit/MODULES | tail -3
V=tests/netkit/vectors   # frozen answers from independent implementations (vectors/make_vectors.py records where they came from)
echo "== des_spec.prn (nk_des.prn)"
scripts/scarab_wasm.sh tests/netkit/des_spec.prn --modules stdlib/netkit/MODULES --file $V/des.vec | tail -3
echo "== crypto_spec.prn (nk_crypto.prn: SHA-256, ChaCha20, Poly1305)"
scripts/scarab_wasm.sh tests/netkit/crypto_spec.prn --modules stdlib/netkit/MODULES --file $V/sha256.vec --file $V/chacha.vec --file $V/poly.vec | tail -3
echo "== x25519_spec.prn (nk_x25519.prn)"
scripts/scarab_wasm.sh tests/netkit/x25519_spec.prn --modules stdlib/netkit/MODULES --file $V/x25519.vec | tail -3
echo "== ed25519_spec.prn (nk_sha512.prn + nk_ed25519.prn)"
scripts/scarab_wasm.sh tests/netkit/ed25519_spec.prn --modules stdlib/netkit/MODULES --file $V/sha512.vec --file $V/modl.vec --file $V/ed25519.vec | tail -3
echo "== garage_spec.prn (nk_garage.prn)"
scripts/scarab_wasm.sh tests/netkit/garage_spec.prn --modules stdlib/netkit/MODULES | tail -3
echo "== term_spec.prn (nk_term.prn, the SSH terminal)"
scripts/scarab_wasm.sh tests/netkit/term_spec.prn --modules stdlib/netkit/term/MODULES --file $V/term.vec | tail -3
echo "== oui_gen_spec.prn (nk_oui_gen.prn)"
scripts/scarab_wasm.sh tests/netkit/oui_gen_spec.prn --module stdlib/netkit/nk_oui_gen.prn --file stdlib/netkit/data/oui_brands.tsv --file "$W/oui.bin" | tail -3
for t in tests/netkit/test_*.py; do
  case "$t" in */test_rfb_interop.py|*/test_host_e2e.py|*/test_browser_e2e.py|*/test_browser_garage.py) continue;; esac
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

# ---- the generated page, end to end (needs node 24+, the real index.html from the SHANKPIT repo, and the VNC test server) ----
echo "== test_host_e2e.py (the generated page in Node vs a real SSH server and a real VNC server)"
HTML="${NETKIT_INDEX_HTML:-../SHANKPIT/apps/netkit/index.html}"
if ! command -v node >/dev/null 2>&1; then echo "SKIP: node not found (24+ needed)";
elif [ ! -f "$HTML" ]; then echo "SKIP: index.html not found at $HTML (check out SHANKPIT beside PARENA, or set NETKIT_INDEX_HTML)";
elif [ ! -x "$W/vncd" ]; then echo "SKIP: the VNC test server could not be built (libvncserver headers missing)";
else
  scripts/build_netkit_web.sh "$W/web" | tail -1
  python3 tests/netkit/test_host_e2e.py "$W/web" "$HTML" "$W/vncd" ${LD:+"$LD"} | tail -4
fi

# ---- the page in a REAL browser engine, served with the Android shell's Content-Security-Policy (needs playwright + aiohttp) ----
echo "== test_browser_e2e.py (headless Chromium, real clicks and keys, pixel-exact canvas checks)"
if [ ! -f "$HTML" ] || [ ! -x "$W/vncd" ] || [ ! -d "$W/web" ]; then echo "SKIP: needs index.html, the VNC test server and the web build (see above)";
elif ! python3 -c "import playwright, aiohttp" 2>/dev/null; then echo "SKIP: needs python playwright + aiohttp (pip install playwright aiohttp; python3 -m playwright install --with-deps chromium)";
else python3 tests/netkit/test_browser_e2e.py "$W/web" "$(dirname "$HTML")" "$W/vncd" ${LD:+"$LD"} "${NETKIT_SHOTS:-$W/shots}" | tail -5; fi

# ---- the garage and the arcade (BRAWLPIT inside the page) in headless Chromium, under the app's CSP (no SSH/VNC server needed) ----
echo "== test_browser_garage.py (garage + arcade in Chromium under the Android CSP)"
if command -v node >/dev/null 2>&1 && [ -f "$HTML" ] && [ ! -d "$W/web" ]; then scripts/build_netkit_web.sh "$W/web" | tail -1; fi
if [ -d "$W/web" ] && python3 -c "import playwright" 2>/dev/null; then python3 tests/netkit/test_browser_garage.py "$W/web" "$(dirname "$HTML")" | tail -4
else echo "SKIP: needs node (to build the page), playwright and the SHANKPIT checkout"; fi
