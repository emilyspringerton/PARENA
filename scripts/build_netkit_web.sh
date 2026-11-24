#!/usr/bin/env bash
# Builds everything the SHANKPIT Netkit page ships, from PARENA source only:
#   <outdir>/netkit.wasm        scanner + VNC (RFB) + SSH + crypto, 64 MB memory (one instance per VNC session)
#   <outdir>/netkit-lite.wasm   the same code with 8 MB memory (the scanner, and one instance per SSH session)
#   <outdir>/nkterm.wasm        the SSH terminal emulator
#   <outdir>/nk-host.js         the page host (stdlib/netkit/host/*.prn -> TypeScript -> JavaScript)
#   <outdir>/nk-host.ts         the TypeScript the JS was stripped from (kept for review / tsc)
# There is no hand-written JavaScript in the product: the .js is generated. Needs the LLVM toolchain (see build_netkit_wasm.sh)
# and `node` (24+, for node:module stripTypeScriptTypes).
#   scripts/build_netkit_web.sh <outdir>
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:?usage: build_netkit_web.sh <outdir>}"
mkdir -p "$OUT"
PARENA="${PARENA_BIN:-./.parena-bootstrap}"
[ -x "$PARENA" ] || make .parena-bootstrap >/dev/null
scripts/build_netkit_wasm.sh "$OUT/netkit.wasm"
NETKIT_MEM=8388608 scripts/build_netkit_wasm.sh "$OUT/netkit-lite.wasm"
scripts/build_netkit_term_wasm.sh "$OUT/nkterm.wasm"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
mapfile -t SRC < <(grep -v '^#' stdlib/netkit/host/MODULES | grep -v '^$')
cat "${SRC[@]}" > "$W/nk_host_all.prn"
"$PARENA" build "$W/nk_host_all.prn" -o "$OUT/nk-host.ts"
node scripts/ts_strip.mjs "$OUT/nk-host.ts" "$OUT/nk-host.js"
echo "built $OUT/nk-host.js ($(wc -c < "$OUT/nk-host.js") bytes of generated JS, from PARENA via TypeScript)"
