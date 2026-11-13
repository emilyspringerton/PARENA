#!/usr/bin/env bash
# tests/reflux_wow/run-suites.sh -- run the suite fragments, tests/reflux_wow/suite.d/NN-<agent>.sh (BOARD.md plug-in
# slots), in sorted (C locale) order. `make test-reflux-wow` calls it last, after packaging the full client.
#
# Each fragment runs with bash from the repo root, with these exported:
#   LUA51              the Lua 5.1 interpreter                       (required)
#   REFLUX_WOW_OUT     the suite's output dir; write under $REFLUX_WOW_OUT/<agent>/ only   (required)
#   REFLUX_WOW_CLIENT  the packaged client to test                   (default $REFLUX_WOW_OUT/client)
#   PARENA_BIN         the compiler                                  (default ./.parena-bootstrap)
# A fragment exits non-zero on any failed check. The first one that does stops the run with its exit code.
#
# usage: tests/reflux_wow/run-suites.sh [NAME-GLOB]     e.g. '20-*' runs only fishing's; default: every fragment
set -euo pipefail
cd "$(dirname "$0")/../.."
: "${LUA51:?run-suites: LUA51 must name a Lua 5.1 interpreter}"
: "${REFLUX_WOW_OUT:?run-suites: REFLUX_WOW_OUT must be set}"
export LUA51 REFLUX_WOW_OUT
export REFLUX_WOW_CLIENT="${REFLUX_WOW_CLIENT:-$REFLUX_WOW_OUT/client}"
export PARENA_BIN="${PARENA_BIN:-./.parena-bootstrap}"
glob="${1:-*}"

frags=()
while IFS= read -r f; do
  case "$f" in $glob.sh) frags+=("tests/reflux_wow/suite.d/$f") ;; esac
done < <(ls -1 tests/reflux_wow/suite.d 2>/dev/null | grep '\.sh$' | LC_ALL=C sort)
if [ ${#frags[@]} -eq 0 ]; then echo "suite fragments: none match '$glob'"; exit 0; fi
[ -d "$REFLUX_WOW_CLIENT/AddOns" ] || { echo "run-suites: no packaged client at $REFLUX_WOW_CLIENT" >&2; exit 2; }

n=0
for f in "${frags[@]}"; do
  echo "== $f"
  rc=0
  bash "$f" || rc=$?
  if [ "$rc" -ne 0 ]; then echo "FAIL: $f exited $rc"; exit "$rc"; fi
  n=$((n + 1))
done
echo "suite fragments: $n passed"
