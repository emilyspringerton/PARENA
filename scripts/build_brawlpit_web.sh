#!/usr/bin/env bash
# Builds everything the BRAWLPIT web page ships, from PARENA source only:
#   <outdir>/brawlpit.wasm   the game  (stdlib/brawlpit/bp_*.prn  -> LLVM -> wasm32, no malloc, 0 imports)
#   <outdir>/bp-host.js      the page's host code (stdlib/brawlpit/bp_host.prn -> TypeScript -> JS)
#   <outdir>/bp-host.ts      the TypeScript the JS was stripped from (kept for review / tsc)
# There is no hand-written JavaScript in the product: the .js is generated.
#   scripts/build_brawlpit_web.sh <outdir>
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:?usage: build_brawlpit_web.sh <outdir>}"
mkdir -p "$OUT"
PARENA="${PARENA_BIN:-./.parena-bootstrap}"
[ -x "$PARENA" ] || make .parena-bootstrap >/dev/null
scripts/build_brawlpit_wasm.sh "$OUT/brawlpit.wasm"
"$PARENA" build stdlib/brawlpit/bp_host.prn -o "$OUT/bp-host.ts"
node scripts/ts_strip.mjs "$OUT/bp-host.ts" "$OUT/bp-host.js"
echo "built $OUT/bp-host.js ($(wc -c < "$OUT/bp-host.js") bytes of generated JS, from PARENA via TypeScript)"
