#!/usr/bin/env bash
# Builds the shankpit-460 web game from PARENA source and runs every test against the built artifacts:
#   - parity: the wasm engine vs shankpit-460's own C engine (physics.h + net_sim.h + local_game.h), tick for tick
#   - engine: module hygiene (0 imports, fixed memory), screens, input, bot matches, the wire protocol against hand-built frames
#   - soak:   eight bots at each level in the arena: they fight, and they keep moving
# Needs node, gcc, and the LLVM toolchain (see build_shankpit460_wasm.sh). The C engine is read from $SHANKPIT460_DIR
# (default ../shankpit-460). The matchmaker and the browser end-to-end live in IDUNA (go test ./internal/shankpit460) and
# WOTAN (scripts/shankpit460-net-test.mjs, scripts/shankpit460-browser-test.mjs).
set -euo pipefail
cd "$(dirname "$0")/.."
SP="${SHANKPIT460_DIR:-../shankpit-460}"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
scripts/build_shankpit460_web.sh "$W" 2>&1 | grep -E "^built|FAIL"
gcc -O2 -ffp-contract=off -w -I"$SP/packages" -o "$W/parity_ref" tests/shankpit460_parity_ref.c -lm
echo "== parity (wasm vs the C engine)";  node tests/shankpit460_parity_test.mjs "$W/shankpit460.wasm" "$W/parity_ref" "$SP/maps/v0_shankpit.map" | tail -2
echo "== engine";                          node tests/shankpit460_engine_test.mjs "$W/shankpit460.wasm" | tail -1
echo "== bot soak";                        node tests/shankpit460_bot_soak.mjs "$W/shankpit460.wasm" 12000 | tail -1
