#!/usr/bin/env bash
# The Bedrock Racers gate: build the static bundle from source, then every test, then the soak line against the committed baseline.
#   make test-bedrockracers
# Needs node and the LLVM toolchain (LLVM_TOOLCHAIN_ROOT=/ where system LLVM 18 is installed). The real-browser test also needs
# Playwright: set NODE_PATH to a directory with node_modules/playwright and PLAYWRIGHT_BROWSERS_PATH to its browsers, or it is
# skipped with a message (never silently).
set -euo pipefail
cd "$(dirname "$0")/.."
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
scripts/build_bedrockracers_web.sh "$W/web"
WASM="$W/web/bedrockracers.wasm"
for t in physics magnet fire engine; do
  echo "== br_${t}_test"; node "tests/br_${t}_test.mjs" "$WASM"
done
echo "== soak (seeds 1-6, level 3, 15000 ticks) against tests/br_soak_baseline.txt"
node tests/br_bot_soak.mjs "$WASM" 6 3 | tail -1 > "$W/soak.txt"
if cmp -s tests/br_soak_baseline.txt "$W/soak.txt"; then echo "PASS: the soak line is byte-identical to the baseline"
else echo "FAIL: the soak line moved (a render-only change must not move a goal; a sim change needs a new baseline and a reason)"; diff <(tr ',' '\n' < tests/br_soak_baseline.txt) <(tr ',' '\n' < "$W/soak.txt") | head -20; exit 1; fi
if [ -n "${NODE_PATH:-}" ] && [ -n "${PLAYWRIGHT_BROWSERS_PATH:-}" ]; then
  echo "== br_browser_test (headless Chromium)"; node tests/br_browser_test.mjs "$W/web" "$W/shots"
else
  echo "SKIPPED: tests/br_browser_test.mjs (set NODE_PATH and PLAYWRIGHT_BROWSERS_PATH to run it in a real browser)"
fi
echo "bedrockracers: all green"
