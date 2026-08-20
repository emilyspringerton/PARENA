#!/usr/bin/env bash
# run_domain4_check.sh — VS0 domain 4's real, automated verification.
# NORTHSTAR.md's own DoD: "Compiled C output has zero runtime leaks or
# use-after-free... Runs clean under Valgrind (0 bytes leaked) and
# AddressSanitizer." Two real checks, not one:
#   1. The real emitted program (examples/valid_only.prn's own
#      load-config) runs clean under ASan+UBSan (and Valgrind, if
#      installed -- see sudo-queue/20-valgrind.sh).
#   2. A deliberately-broken fixture (real leak + real UAF) is
#      confirmed to actually get CAUGHT by the same sanitizers --
#      proving this check has teeth, not just that the happy path
#      passes. Same "prove the check can fail" discipline
#      tests/test_region.c already applies to region_analyze.
set -euo pipefail
cd "$(dirname "$0")/../.."   # repo root

PARENA_BIN="./parena"
if [ ! -x "$PARENA_BIN" ]; then
    echo "run_domain4_check.sh: building parena first" >&2
    make build >/dev/null
fi

WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

echo "== domain 4, check 1: the real emitted program runs clean under ASan+UBSan =="
"$PARENA_BIN" build examples/valid_only.prn -o "$WORKDIR/valid_only.c"
gcc -std=c99 -Wall -Wextra -pedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I runtime -c -o "$WORKDIR/valid_only.o" "$WORKDIR/valid_only.c"
gcc -std=c99 -g -fsanitize=address,undefined -I runtime \
    -o "$WORKDIR/driver_asan" tests/integration/driver_valid_only.c "$WORKDIR/valid_only.o" \
    runtime/parena_runtime.c
"$WORKDIR/driver_asan"
echo "PASS: emitted program is clean under ASan+UBSan"

echo
echo "== domain 4, check 2: a deliberately-broken fixture IS caught (the check has teeth) =="
gcc -std=c99 -g -fsanitize=address -I runtime \
    -o "$WORKDIR/broken_asan" tests/integration/deliberately_broken.c runtime/parena_runtime.c
if "$WORKDIR/broken_asan" >"$WORKDIR/broken_out.txt" 2>&1; then
    echo "FAIL: deliberately_broken.c's real use-after-free/leak was NOT caught -- the domain 4 " >&2
    echo "check has no teeth, this is a real problem with the verification itself, not a pass." >&2
    cat "$WORKDIR/broken_out.txt" >&2
    exit 1
fi
grep -q "AddressSanitizer" "$WORKDIR/broken_out.txt"
echo "PASS: the deliberately-broken fixture's real use-after-free was caught by ASan"

if command -v valgrind >/dev/null 2>&1; then
    echo
    echo "== domain 4, check 3: Valgrind (0 bytes leaked), the DoD's own literal tool =="
    # Real, separate, non-instrumented build -- ASan and Valgrind are
    # fundamentally incompatible (ASan's own shadow-memory/redzone runtime
    # conflicts with Valgrind's own memory-shadowing), so this can't reuse
    # check 1's $WORKDIR/valid_only.o, which was compiled with
    # -fsanitize=address,undefined. A first version of this script tried
    # to reuse it and that's exactly what broke real CI here -- fixed by
    # building a real, clean object file for this check specifically.
    gcc -std=c99 -Wall -Wextra -pedantic -g -I runtime \
        -c -o "$WORKDIR/valid_only_plain.o" "$WORKDIR/valid_only.c"
    gcc -std=c99 -g -I runtime -o "$WORKDIR/driver_plain" \
        tests/integration/driver_valid_only.c "$WORKDIR/valid_only_plain.o" runtime/parena_runtime.c
    valgrind --leak-check=full --error-exitcode=1 "$WORKDIR/driver_plain"
    echo "PASS: clean under Valgrind"
else
    echo
    echo "SKIP: Valgrind not installed (sudo-queue/20-valgrind.sh not run yet) -- ASan/UBSan above" >&2
    echo "already cover the same real property this environment can currently check." >&2
fi

echo
echo "domain 4 check: all real checks passed"
