#!/usr/bin/env bash
# run_multifile_check.sh — real, automated verification of VS0's new
# multi-file `parena build a.prn b.prn -o out.c` support (2026-08-20),
# same "prove the check has teeth" discipline run_domain4/5_check.sh
# already apply. Two real checks:
#   1. examples/multifile_a.prn + examples/multifile_b.prn (b.prn uses
#      T, defined only in a.prn) built TOGETHER exit 0 and produce real,
#      gcc-clean C -- the whole real point of this feature, closing the
#      "T isn't visible when compiled standalone" gap that kept blocking
#      firefly/ladybug.prn and scarab.prn against firefly.prn.
#   2. examples/multifile_b.prn built ALONE still fails honestly (T
#      genuinely isn't visible) -- proving check 1's own success is real
#      cross-file resolution, not a coincidence of some other fix.
set -euo pipefail
cd "$(dirname "$0")/../.."   # repo root

PARENA_BIN="./parena"
if [ ! -x "$PARENA_BIN" ]; then
    echo "run_multifile_check.sh: building parena first" >&2
    make build >/dev/null
fi

WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

echo "== multi-file check 1: a.prn + b.prn together exit 0 and produce real, gcc-clean C =="
OUT_COMBINED="$WORKDIR/combined.c"
"$PARENA_BIN" build examples/multifile_a.prn examples/multifile_b.prn -o "$OUT_COMBINED"
if [ ! -s "$OUT_COMBINED" ]; then
    echo "FAIL: exit code 0 but no real, non-empty output file was written to $OUT_COMBINED" >&2
    exit 1
fi
gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -c "$OUT_COMBINED" -o "$WORKDIR/combined.o"
echo "PASS: examples/multifile_a.prn + examples/multifile_b.prn -> real, gcc-clean C"

echo
echo "== multi-file check 2: b.prn built ALONE still fails honestly (T genuinely not visible) =="
set +e
"$PARENA_BIN" build examples/multifile_b.prn -o "$WORKDIR/b_alone.c" >"$WORKDIR/b_alone_out.txt" 2>&1
CODE=$?
set -e
if [ "$CODE" -eq 0 ]; then
    echo "FAIL: examples/multifile_b.prn built alone exited 0 -- T shouldn't be visible without a.prn" >&2
    exit 1
fi
echo "PASS: examples/multifile_b.prn alone still fails -- check 1's success is real cross-file resolution"

echo
echo "multi-file check: all real checks passed"
