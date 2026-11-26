#!/usr/bin/env bash
# SCARAB/LADYBUG (wasm) checking themselves: smoke_fail_spec.prn passes and fails every matcher once; its report must match
# smoke_fail_expected.txt byte for byte, and the run must exit 1 (a failed spec), not 0 or 2.
set -uo pipefail
cd "$(dirname "$0")/../.."
out="$(scripts/scarab_wasm.sh tests/scarab/smoke_fail_spec.prn)"; rc=$?
if [ "$rc" != 1 ]; then echo "FAIL: exit status $rc, expected 1"; exit 1; fi
if [ "$out" != "$(cat tests/scarab/smoke_fail_expected.txt)" ]; then
  echo "FAIL: the report differs from tests/scarab/smoke_fail_expected.txt"; diff <(echo "$out") tests/scarab/smoke_fail_expected.txt; exit 1
fi
echo "PASS: scarab/ladybug self-test (9 specs, 17 expectations, 4 deliberate failures reported exactly)"
echo; echo "1 passed, 0 failed"
