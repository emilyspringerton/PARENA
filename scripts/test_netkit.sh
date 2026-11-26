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
#   SAMBA_ROOT=<dir> an extracted samba + samba-libs + libldb2/libtalloc2/libtdb1/libtevent0t64/libwbclient0 + samba-common(-bin)
#                    for the live SMB probe leg when samba is not installed
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
echo "== gfd_spec.prn (nk_gfd.prn: the GoblinFoxDragon client)"
scripts/scarab_wasm.sh tests/netkit/gfd_spec.prn --modules stdlib/netkit/MODULES | tail -3
echo "== test_gfd_ws_live.py (the signed-in GFD route from the app origin, production; GFD_SMOKE_IDENTITY)"
python3 tests/netkit/test_gfd_ws_live.py | tail -4
echo "== garage_spec.prn (nk_garage.prn)"
scripts/scarab_wasm.sh tests/netkit/garage_spec.prn --modules stdlib/netkit/MODULES | tail -3
echo "== term_spec.prn (nk_term.prn, the SSH terminal)"
scripts/scarab_wasm.sh tests/netkit/term_spec.prn --modules stdlib/netkit/term/MODULES --file $V/term.vec | tail -3
echo "== rfb_spec.prn (nk_rfb.prn vs a scripted server; whole / byte / random-chunk delivery)"
scripts/scarab_wasm.sh tests/netkit/rfb_spec.prn --modules stdlib/netkit/MODULES | tail -3
echo "== oui_gen_spec.prn (nk_oui_gen.prn)"
scripts/scarab_wasm.sh tests/netkit/oui_gen_spec.prn --module stdlib/netkit/nk_oui_gen.prn --file stdlib/netkit/data/oui_brands.tsv --file "$W/oui.bin" | tail -3
echo "== smb_spec.prn (nk_smb.prn: the credential-free SMB2 probe; built replies + one recorded from a real Samba 4.19)"
scripts/scarab_wasm.sh tests/netkit/smb_spec.prn --modules stdlib/netkit/MODULES --file $V/smb_samba419.hex | tail -3
echo "== edit_spec.prn (nk_edit.prn: the Drive editor's document; nk_kit.prn: the VNC kit for a USB drive)"
scripts/scarab_wasm.sh tests/netkit/edit_spec.prn --modules stdlib/netkit/MODULES | tail -3
echo "== md_spec.prn (nk_md.prn: MD4, MD5, HMAC-MD5, HMAC-SHA256 vs the RFC vectors)"
scripts/scarab_wasm.sh tests/netkit/md_spec.prn --modules stdlib/netkit/MODULES | tail -3
echo "== smb_files_spec.prn (nk_smb_files.prn: NTLMv2 per MS-NLMP 4.2.4, recorded Samba + impacket sessions, built replies)"
scripts/scarab_wasm.sh tests/netkit/smb_files_spec.prn --modules stdlib/netkit/MODULES --file $V/smb_samba419_ls.hex \
  --file $V/smb_samba419_cat.hex --file $V/smb_impacket_shares.hex --file $V/smb_impacket_ls.hex --file $V/smb_samba419_badpw.hex | tail -3
echo "== the SMB probe vs a live Samba (scripts/netkit_smb_probe.sh, the PARENA -> TS node host pumping a real socket)"
SMBD="${SAMBA_ROOT:+$SAMBA_ROOT/usr/sbin/smbd}"; SMBD="${SMBD:-$(command -v smbd || true)}"
if [ -z "$SMBD" ] || [ ! -x "$SMBD" ]; then echo "SKIP: smbd not found (apt install samba, or set SAMBA_ROOT to an extracted samba + samba-libs + deps)";
else
  SL=""; [ -n "${SAMBA_ROOT:-}" ] && SL="$SAMBA_ROOT/usr/lib/x86_64-linux-gnu:$SAMBA_ROOT/usr/lib/x86_64-linux-gnu/samba"
  SP=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
  mkdir -p "$W/smb"/{lock,state,cache,private,pid,log,ncalrpc}
  mkdir -p "$W/smb/docs/Sub Folder"; printf 'netkit live check\n' > "$W/smb/docs/Sub Folder/note.txt"; head -c 5000 /dev/zero > "$W/smb/docs/zeros.bin"
  printf '[global]\n workgroup = NETKITLAB\n netbios name = NETKITTEST\n smb ports = %s\n interfaces = 127.0.0.1\n bind interfaces only = yes\n lock directory = %s/lock\n state directory = %s/state\n cache directory = %s/cache\n private dir = %s/private\n pid directory = %s/pid\n ncalrpc dir = %s/ncalrpc\n log file = %s/log/log.%%m\n passdb backend = tdbsam:%s/private/passdb.tdb\n load printers = no\n disable spoolss = yes\n[docs]\n path = %s/docs\n read only = yes\n valid users = %s\n' \
    "$SP" "$W/smb" "$W/smb" "$W/smb" "$W/smb" "$W/smb" "$W/smb" "$W/smb" "$W/smb" "$W/smb" "$(id -un)" > "$W/smb/smb.conf"
  PDB="${SAMBA_ROOT:+$SAMBA_ROOT/usr/bin/pdbedit}"; PDB="${PDB:-$(command -v pdbedit || true)}"
  printf 'netkit-live-1\nnetkit-live-1\n' | LD_LIBRARY_PATH="$SL${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$PDB" -s "$W/smb/smb.conf" -a -u "$(id -un)" -t >/dev/null
  LD_LIBRARY_PATH="$SL${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$SMBD" -F --debug-stdout -d0 -s "$W/smb/smb.conf" > "$W/smb/smbd.out" 2>&1 & SMBPID=$!
  for _ in $(seq 50); do (exec 3<>/dev/tcp/127.0.0.1/$SP) 2>/dev/null && break; sleep 0.1; done
  if scripts/netkit_smb_probe.sh 127.0.0.1 "$SP" > "$W/smb/report.txt" && grep -qx 'Computer name: NETKITTEST' "$W/smb/report.txt" \
     && grep -qx 'File sharing: SMB 3.1.1' "$W/smb/report.txt" && grep -q '^Its clock: ' "$W/smb/report.txt"; then
    echo "live Samba probe: OK ($(grep -c . "$W/smb/report.txt") report lines)"
    B=(scripts/netkit_smb_browse.sh 127.0.0.1 --port "$SP" --user "$(id -un)")
    if NETKIT_SMB_PASSWORD=netkit-live-1 "${B[@]}" ls docs > "$W/smb/ls.txt" && grep -q '\[folder\].*Sub Folder$' "$W/smb/ls.txt" \
       && grep -q '^4 KB .*zeros.bin$' "$W/smb/ls.txt" && grep -q '^signed in to 127.0.0.1 (SMB 2.1, signed)$' "$W/smb/ls.txt" \
       && NETKIT_SMB_PASSWORD=netkit-live-1 "${B[@]}" cat docs "Sub Folder/note.txt" | grep -qx 'netkit live check' \
       && ! NETKIT_SMB_PASSWORD=wrong "${B[@]}" ls docs > "$W/smb/bad.txt" && grep -q 'Wrong username or password' "$W/smb/bad.txt"; then
      echo "live Samba sign-in + folder + preview + wrong password: OK"
    else echo "FAIL: browsing the live Samba"; cat "$W/smb/ls.txt" "$W/smb/bad.txt" 2>/dev/null; kill $SMBPID; exit 1; fi
  else echo "FAIL: the SMB probe against a live Samba"; cat "$W/smb/report.txt" "$W/smb/smbd.out"; kill $SMBPID; exit 1; fi
  kill $SMBPID 2>/dev/null || true
fi
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
