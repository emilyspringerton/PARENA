#!/usr/bin/env bash
# Builds everything the EMPIRE web pages ship, from PARENA source only:
#   <outdir>/empire.wasm      the game: rules, AIs, protocol, screens (stdlib/empire/web/em_*.prn -> LLVM -> wasm32, no malloc, 0 imports)
#   <outdir>/empire-host.js   the page's host code (stdlib/empire/web/em_host.prn -> TypeScript -> JS)
#   <outdir>/empire-host.ts   the TypeScript the JS was stripped from (kept for review / tsc)
# IDUNA embeds the same empire.wasm (internal/empire/empire.wasm) as the referee. There is no hand-written JavaScript in the game: the .js is generated.
#   scripts/build_empire_web.sh <outdir>
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:?usage: build_empire_web.sh <outdir>}"
mkdir -p "$OUT"
PARENA="${PARENA_BIN:-./.parena-bootstrap}"
[ -x "$PARENA" ] || make .parena-bootstrap >/dev/null
scripts/build_empire_wasm.sh "$OUT/empire.wasm"
"$PARENA" build stdlib/empire/web/em_host.prn -o "$OUT/empire-host.ts"
node scripts/ts_strip.mjs "$OUT/empire-host.ts" "$OUT/empire-host.js" 2>/dev/null
echo "built $OUT/empire-host.js ($(wc -c < "$OUT/empire-host.js") bytes of generated JS, from PARENA via TypeScript)"
