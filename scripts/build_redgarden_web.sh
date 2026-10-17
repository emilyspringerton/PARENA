#!/usr/bin/env bash
# Builds everything the REDGARDEN web page ships, from PARENA source only:
#   <outdir>/redgarden.wasm   the game client  (stdlib/redgarden/web/rg_*.prn -> LLVM -> wasm32, no malloc, 0 imports)
#   <outdir>/rg-host.js       the page's host code (stdlib/redgarden/web/rg_host.prn -> TypeScript -> JS)
#   <outdir>/rg-host.ts       the TypeScript the JS was stripped from (kept for review / tsc)
# There is no hand-written JavaScript in the product: the .js is generated.
#   scripts/build_redgarden_web.sh <outdir>
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:?usage: build_redgarden_web.sh <outdir>}"
mkdir -p "$OUT"
PARENA="${PARENA_BIN:-./.parena-bootstrap}"
[ -x "$PARENA" ] || make .parena-bootstrap >/dev/null
scripts/build_redgarden_wasm.sh "$OUT/redgarden.wasm"
"$PARENA" build stdlib/redgarden/web/rg_host.prn -o "$OUT/rg-host.ts"
node scripts/ts_strip.mjs "$OUT/rg-host.ts" "$OUT/rg-host.js"
echo "built $OUT/rg-host.js ($(wc -c < "$OUT/rg-host.js") bytes of generated JS, from PARENA via TypeScript)"
