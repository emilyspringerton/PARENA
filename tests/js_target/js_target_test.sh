#!/usr/bin/env bash
# tests/js_target/js_target_test.sh -- `parena build x.prn -o x.js` (emit_js): the TypeScript emitter's program with
# every type annotation left out, so a browser loads it with no TypeScript toolchain (no Node).
#   bash tests/js_target/js_target_test.sh [<parena compiler>] [<workdir>]
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
parena="${1:-$here/../../.parena-bootstrap}"
work="${2:-${TMPDIR:-/tmp}/js_target_test}"
mkdir -p "$work"
pass=0 fail=0
ok() { pass=$((pass + 1)); }
bad() { fail=$((fail + 1)); echo "FAIL: $*"; }

"$parena" build "$here/sample.prn" -o "$work/sample.ts" >/dev/null || bad "the .ts build failed"
"$parena" build "$here/sample.prn" -o "$work/sample.js" >/dev/null || bad "the .js build failed"
ts="$(tr -d '\r' < "$work/sample.ts")"; js="$(tr -d '\r' < "$work/sample.js")"

case "$js" in *"JavaScript target"*) ok ;; *) bad "the .js header does not name the JavaScript target" ;; esac
# no annotation survives in the .js: no `: number`, `: string`, `: any`, `: boolean`, `: void`
if printf '%s' "$js" | grep -qE ': (number|string|any|boolean|void)\b'; then bad "a type annotation is left in the .js"; else ok; fi
grep -q 'export function sumLengths(xs) {' <<<"$js" && ok || bad "params/return are not unannotated"
grep -q 'let total = 0;' <<<"$js" && ok || bad "module state is not unannotated"
grep -q 'let k = (n + 1);' <<<"$js" && ok || bad "a local let is not unannotated"
grep -q 'addEventListener("click", onClick)' <<<"$js" && ok || bad "the callback is not the named function"
# the dotimes limit temporary has its real generated name (mk_sym used to point at a dead stack buffer)
grep -qE 'let __dn[0-9]+ = len\(xs\);' <<<"$js" && ok || bad "the dotimes limit temporary is misnamed"
# erasing the TypeScript annotations gives exactly the JavaScript (only the header comment differs)
strip() { sed -E '1d; s/: (number|string|any|boolean|void)//g'; }
if [ "$(printf '%s\n' "$ts" | strip)" = "$(printf '%s\n' "$js" | sed '1d')" ]; then ok; else bad "the .js is not the .ts minus its annotations"; fi

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
