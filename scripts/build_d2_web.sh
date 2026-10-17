#!/usr/bin/env bash
# Builds everything the D2 web page ships, from PARENA source only:
#   <outdir>/d2.wasm   the game client  (stdlib/d2/web/d2_*.prn -> LLVM -> wasm32, no malloc, 0 imports)
#   <outdir>/d2-host.js       the page's host code (stdlib/d2/web/d2_host.prn -> TypeScript -> JS)
#   <outdir>/d2-host.ts       the TypeScript the JS was stripped from (kept for review / tsc)
# There is no hand-written JavaScript in the product: the .js is generated.
#   scripts/build_d2_web.sh <outdir>
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:?usage: build_d2_web.sh <outdir>}"
mkdir -p "$OUT"
PARENA="${PARENA_BIN:-./.parena-bootstrap}"
[ -x "$PARENA" ] || make .parena-bootstrap >/dev/null
scripts/build_d2_wasm.sh "$OUT/d2.wasm"
"$PARENA" build stdlib/d2/web/d2_host.prn -o "$OUT/d2-host.ts"
node scripts/ts_strip.mjs "$OUT/d2-host.ts" "$OUT/d2-host.js"
echo "built $OUT/d2-host.js ($(wc -c < "$OUT/d2-host.js") bytes of generated JS, from PARENA via TypeScript)"
