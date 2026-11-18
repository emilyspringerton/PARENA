#!/usr/bin/env bash
# scripts/build_iduna_discover.sh [out] -- the native `iduna-discover` CLI (C target): prints the IDUNA base URL a
# native tool should use, the local IDUNA first (docs/IDUNA_DISCOVERY.md).
#
#   bash scripts/build_iduna_discover.sh build/iduna-discover
#   export IDUNA_BASE_URL="$(build/iduna-discover)"
#
# PARENA: stdlib/iduna/discover.prn (rules), stdlib/iduna/discover_native.prn + stdlib/net/http_probe.prn (the probe).
# C host: tools/iduna_discover_host.c (environment, printing, exit code). Needs PARENA_BIN (default ./.parena-bootstrap)
# and CC (default gcc). On Windows (MSYS2/MinGW) it links ws2_32.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:-build/iduna-discover}"
PARENA_BIN="${PARENA_BIN:-./.parena-bootstrap}"
CC="${CC:-gcc}"
GEN="${OUT}_gen.c"
mkdir -p "$(dirname "$OUT")"
"$PARENA_BIN" build stdlib/string.prn stdlib/net/http_probe.prn stdlib/iduna/discover.prn stdlib/iduna/discover_native.prn \
  -o "$GEN" >/dev/null
cat "$GEN" tools/iduna_discover_host.c > "${OUT}_full.c"
LIBS=""
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*|UCRT*|CLANG*) LIBS="-lws2_32" ;; esac
# shellcheck disable=SC2086
"$CC" -std=c99 -O2 -Wall -Wextra -DPARENA_NO_GRAPHICS -I runtime -o "$OUT" "${OUT}_full.c" runtime/parena_runtime.c $LIBS
echo "built $OUT"
