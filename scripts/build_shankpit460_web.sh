#!/usr/bin/env bash
# Builds everything the shankpit-460 web page ships, from PARENA source only:
#   <outdir>/shankpit460.wasm   the game  (stdlib/shankpit460/sp_*.prn -> LLVM -> wasm32, no malloc, 0 imports)
#   <outdir>/sp-host.js         the page's host code (stdlib/shankpit460/sp_host.prn -> TypeScript -> JS)
#   <outdir>/sp-host.ts         the TypeScript the JS was stripped from (kept for review / tsc)
# There is no hand-written JavaScript in the product: the .js is generated.
#   scripts/build_shankpit460_web.sh <outdir>
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:?usage: build_shankpit460_web.sh <outdir>}"
mkdir -p "$OUT"
PARENA="${PARENA_BIN:-./.parena-bootstrap}"
[ -x "$PARENA" ] || make .parena-bootstrap >/dev/null
scripts/build_shankpit460_wasm.sh "$OUT/shankpit460.wasm"
"$PARENA" build stdlib/shankpit460/sp_host.prn -o "$OUT/sp-host.ts"
node scripts/ts_strip.mjs "$OUT/sp-host.ts" "$OUT/sp-host.js"
echo "built $OUT/sp-host.js ($(wc -c < "$OUT/sp-host.js") bytes of generated JS, from PARENA via TypeScript)"
