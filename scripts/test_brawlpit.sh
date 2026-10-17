#!/usr/bin/env bash
# Builds the BRAWLPIT web game from PARENA source and runs every test against the built artifacts:
#   - emitter tests (wasm + TypeScript backends)
#   - parity: the wasm engine vs BRAWLPIT's C engine (physics.h + local_game.h), bit for bit
#   - engine: module hygiene (0 imports, no allocator, fixed memory), menus, rules, rendering, CPU brain
#   - host: the generated page host (PARENA -> TS -> JS) driving the real wasm under a DOM stub
# Needs node, gcc, and the LLVM toolchain (see build_brawlpit_wasm.sh). The C engine is read from
# $BRAWLPIT_DIR (default ../BRAWLPIT).
set -euo pipefail
cd "$(dirname "$0")/.."
BP="${BRAWLPIT_DIR:-../BRAWLPIT}"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
make test-emit-llvm test-emit-ts 2>&1 | grep -E "FAIL|passed,"
scripts/build_brawlpit_web.sh "$W" 2>&1 | grep -E "^built|FAIL"
gcc -O2 -ffp-contract=off -Wall -I"$BP/packages" -o "$W/parity_ref" tests/brawlpit_parity_ref.c -lm
echo "== parity (wasm vs the C engine)";  node tests/brawlpit_parity_test.mjs "$W/brawlpit.wasm" "$W/parity_ref" "$BP" | tail -1
echo "== engine";                          node tests/brawlpit_engine_test.mjs "$W/brawlpit.wasm" | tail -1
echo "== duck moveset + atlas";           node tests/brawlpit_duck_test.mjs "$W/brawlpit.wasm" "${WOTAN_DIR:-../WOTAN}/BRAWLPIT/duck.png" | tail -1
echo "== host (generated JS + wasm)";      node tests/brawlpit_host_test.mjs "$W" | tail -1
