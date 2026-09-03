#!/usr/bin/env bash
# run_new_check.sh — real, automated verification for `parena new` (kanban priority-queue card
# PXCL-9311, "make parena cli tool do the same thing burrow new does but for C instead of for
# go"). Real, live checks, not assumed from a code read:
#   1. `parena new <name>` scaffolds a real project and reports success (it already
#      compiles+runs the scaffold internally before returning 0 -- this script's own real job is
#      confirming that whole pipeline actually worked, not re-deriving it).
#   2. The real produced binary, run standalone (a second, independent invocation, not the one
#      `parena new` already ran internally), prints the real, correct output.
#   3. `parena new` on an ALREADY-EXISTING directory refuses (real exit code 1), proving this
#      check has teeth -- same "prove the check can fail" discipline run_domain4_check.sh already
#      established.
set -euo pipefail
cd "$(dirname "$0")/../.."   # repo root

PARENA_BIN="./parena"
if [ ! -x "$PARENA_BIN" ]; then
    echo "run_new_check.sh: building parena first" >&2
    make build >/dev/null
fi

NAME="new_check_scratch"
rm -rf "$NAME"
trap 'rm -rf "$NAME"' EXIT

"$PARENA_BIN" new "$NAME"

OUT="$("$NAME/${NAME}_bin")"
if [ "$OUT" != "Hello from ${NAME}!" ]; then
    echo "run_new_check.sh: FAIL -- expected 'Hello from ${NAME}!', got '$OUT'" >&2
    exit 1
fi

if "$PARENA_BIN" new "$NAME" 2>/dev/null; then
    echo "run_new_check.sh: FAIL -- 'parena new' should refuse to overwrite an existing directory" >&2
    exit 1
fi

echo "run_new_check.sh: PASS"
