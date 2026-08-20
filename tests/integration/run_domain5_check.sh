#!/usr/bin/env bash
# run_domain5_check.sh — VS0 domain 5's real, automated verification.
# NORTHSTAR.md's own DoD, literal wording: "Single executable driving
# the pipeline: `./parena build input.prn -o output.c`" + "Exit code 0
# on success, 1 on region error." Two real checks, not one, same "prove
# the check has teeth" discipline run_domain4_check.sh already applies:
#   1. A real valid file (examples/valid_only.prn) exits 0 and actually
#      produces a real, non-empty output file at the requested path.
#   2. A real file with a genuine region-safety violation
#      (examples/test.prn, the DoD's own literal example -- "Escaping
#      region pointer from :region/scratch to :region/buffer") exits 1
#      -- and does NOT leave a stale/partial output file behind at the
#      requested path (cmd_build in main.c only ever opens the output
#      file after every earlier stage already succeeded, checked
#      directly against the real source, not assumed).
set -euo pipefail
cd "$(dirname "$0")/../.."   # repo root

PARENA_BIN="./parena"
if [ ! -x "$PARENA_BIN" ]; then
    echo "run_domain5_check.sh: building parena first" >&2
    make build >/dev/null
fi

WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

echo "== domain 5, check 1: a real valid file exits 0 and produces real output =="
OUT_VALID="$WORKDIR/valid_only.c"
set +e
"$PARENA_BIN" build examples/valid_only.prn -o "$OUT_VALID"
CODE=$?
set -e
if [ "$CODE" -ne 0 ]; then
    echo "FAIL: a real valid .prn file exited $CODE, want 0" >&2
    exit 1
fi
if [ ! -s "$OUT_VALID" ]; then
    echo "FAIL: exit code 0 but no real, non-empty output file was written to $OUT_VALID" >&2
    exit 1
fi
echo "PASS: examples/valid_only.prn -> exit 0, real non-empty output.c written"

echo
echo "== domain 5, check 2: a real region-safety violation exits 1, no stale output written =="
OUT_INVALID="$WORKDIR/test_invalid.c"
set +e
"$PARENA_BIN" build examples/test.prn -o "$OUT_INVALID" >"$WORKDIR/invalid_out.txt" 2>&1
CODE=$?
set -e
if [ "$CODE" -ne 1 ]; then
    echo "FAIL: examples/test.prn (a real region-escape violation, the DoD's own literal example)" >&2
    echo "exited $CODE, want 1" >&2
    cat "$WORKDIR/invalid_out.txt" >&2
    exit 1
fi
if [ -e "$OUT_INVALID" ]; then
    echo "FAIL: exit code 1 but a stale output file was still written to $OUT_INVALID" >&2
    exit 1
fi
grep -qi "escap" "$WORKDIR/invalid_out.txt" || {
    echo "FAIL: exit 1 but the real error message doesn't mention the real escaping-pointer violation" >&2
    cat "$WORKDIR/invalid_out.txt" >&2
    exit 1
}
echo "PASS: examples/test.prn -> exit 1, no stale output.c left behind, real error message"

echo
echo "domain 5 check: all real checks passed"
