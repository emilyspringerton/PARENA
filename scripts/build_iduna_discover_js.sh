#!/usr/bin/env bash
# scripts/build_iduna_discover_js.sh <out.js> -- the browser's IDUNA discovery module, straight from PARENA to
# JavaScript (the `-o x.js` target: no TypeScript toolchain, no Node).
#
#   bash scripts/build_iduna_discover_js.sh ../WOTAN/js/iduna-discover.js
#
# stdlib/iduna/discover.prn (the rules every target shares) + stdlib/iduna/discover_web.prn (the fetch-based prober).
# WOTAN serves the result as /js/iduna-discover.js and js/iduna-sso.js imports it.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:?usage: build_iduna_discover_js.sh <out.js>}"
PARENA_BIN="${PARENA_BIN:-./.parena-bootstrap}"
"$PARENA_BIN" build stdlib/iduna/discover.prn stdlib/iduna/discover_web.prn -o "$OUT" >/dev/null
echo "built $OUT ($(wc -c < "$OUT") bytes of generated JS, from PARENA stdlib/iduna/discover*.prn)"
