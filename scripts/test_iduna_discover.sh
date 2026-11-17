#!/usr/bin/env bash
# scripts/test_iduna_discover.sh -- the IDUNA discovery suites: stdlib/iduna/discover.prn (the rules, every target) and
# stdlib/net/http_probe.prn (the bounded probe, C target).
#
#   bash scripts/test_iduna_discover.sh [outdir]          (outdir default: build/iduna-discover-test)
#
#   C    the parity fixture, then the probe against a scripted server on 127.0.0.1 (tests/iduna/discover_test.c)
#   Lua  the parity fixture under lua5.1 (tests/iduna/discover_test.lua); skipped, and said so, without lua5.1
#   JS   writes <outdir>/discover_test.html: the generated JavaScript and the same fixture in one self-contained page.
#        There is no node on the dev box, so open it in a browser; its title reads "iduna discover (JS): N passed, M
#        failed" when done.
# Needs PARENA_BIN (default ./.parena-bootstrap, from `make .parena-bootstrap`) and a C compiler (CC, default gcc).
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:-build/iduna-discover-test}"
PARENA_BIN="${PARENA_BIN:-./.parena-bootstrap}"
CC="${CC:-gcc}"
FIXTURE=stdlib/iduna/discover_parity.txt
mkdir -p "$OUT"

# ---- C -----------------------------------------------------------------------------------------------------------------
"$PARENA_BIN" build stdlib/string.prn stdlib/net/http_probe.prn stdlib/iduna/discover.prn stdlib/iduna/discover_native.prn \
  -o "$OUT/native_gen.c" >/dev/null
cat "$OUT/native_gen.c" tests/iduna/discover_test.c > "$OUT/discover_test_full.c"
LIBS="-lpthread"
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*|UCRT*|CLANG*) LIBS="$LIBS -lws2_32" ;; esac
# shellcheck disable=SC2086
"$CC" -std=c99 -Wall -Wextra -DPARENA_NO_GRAPHICS -I runtime -o "$OUT/discover_test" "$OUT/discover_test_full.c" runtime/parena_runtime.c $LIBS
"$OUT/discover_test" "$FIXTURE"

# ---- Lua ---------------------------------------------------------------------------------------------------------------
LUA="${LUA51:-$(command -v lua5.1 || true)}"
"$PARENA_BIN" build stdlib/iduna/discover.prn -o "$OUT/discover.lua" >/dev/null
if [ -n "$LUA" ]; then
  "$LUA" tests/iduna/discover_test.lua "$OUT/discover.lua" "$FIXTURE"
else
  echo "iduna discover (Lua): skipped (no lua5.1 on PATH; set LUA51)"
fi

# ---- JS (a page to open) -----------------------------------------------------------------------------------------------
"$PARENA_BIN" build stdlib/iduna/discover.prn -o "$OUT/discover.js" >/dev/null
names=$(grep -v '^#' "$FIXTURE" | awk 'NF { print $1 }' | sort -u | sed -E 's/-(.)/\U\1/g' | paste -sd, -)
{
  printf '<!doctype html>\n<meta charset="utf-8">\n<title>iduna discover (JS): running</title>\n'
  printf '<pre id="out"></pre>\n<script type="module">\n'
  cat "$OUT/discover.js"
  printf '\nconst RULES = {%s};\n' "$names"
  printf 'const FIXTURE = %s;\n' "$(sed 's/\\/\\\\/g; s/"/\\"/g; s/\r$//; s/.*/"&\\n"/' "$FIXTURE" | paste -sd+ -)"
  cat <<'EOF'
const camel = (n) => n.replace(/-(.)/g, (_, c) => c.toUpperCase());
let pass = 0, fail = 0, rows = 0;
const out = [];
for (const raw of FIXTURE.split('\n')) {
  const line = raw.trim();
  if (!line || line.startsWith('#')) continue;
  rows++;
  const words = line.split(/\s+/), eq = words.indexOf('=');
  const fn = RULES[camel(words[0])];
  const args = words.slice(1, eq).map(Number), want = Number(words[eq + 1]);
  const got = fn ? fn(...args) : undefined;
  if (got === want) pass++; else { fail++; out.push('FAIL parity: ' + line + ' (got ' + got + ')'); }
}
if (rows >= 40) pass++; else { fail++; out.push('FAIL parity: the fixture has its rows'); }
out.push('iduna discover (JS): ' + pass + ' passed, ' + fail + ' failed');
document.getElementById('out').textContent = out.join('\n');
document.title = out[out.length - 1];
EOF
  printf '</script>\n'
} > "$OUT/discover_test.html"
echo "iduna discover (JS): open $OUT/discover_test.html in a browser"
