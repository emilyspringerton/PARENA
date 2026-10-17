#!/usr/bin/env bash
# Builds everything the BEDROCK RACERS web page ships, from PARENA source only:
#   <outdir>/bedrockracers.wasm   the game (stdlib/bedrockracers/web/br_*.prn -> LLVM -> wasm32, no malloc, 0 imports)
#   <outdir>/br-host.js           the page's host code (br_host.prn -> TypeScript -> JS)
#   <outdir>/br-host.ts           the TypeScript the JS was stripped from
# There is no hand-written JavaScript in the product: the .js is generated.
#   scripts/build_bedrockracers_web.sh <outdir>
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:?usage: build_bedrockracers_web.sh <outdir>}"
mkdir -p "$OUT"
PARENA="${PARENA_BIN:-./.parena-bootstrap}"
[ -x "$PARENA" ] || make .parena-bootstrap >/dev/null
scripts/build_bedrockracers_wasm.sh "$OUT/bedrockracers.wasm"
if [ -f stdlib/bedrockracers/web/br_host.prn ]; then
  "$PARENA" build stdlib/bedrockracers/web/br_host.prn -o "$OUT/br-host.ts"
  node scripts/ts_strip.mjs "$OUT/br-host.ts" "$OUT/br-host.js"
  echo "built $OUT/br-host.js ($(wc -c < "$OUT/br-host.js") bytes of generated JS, from PARENA via TypeScript)"
fi
