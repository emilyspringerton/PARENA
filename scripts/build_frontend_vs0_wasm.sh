#!/usr/bin/env bash
# Builds the GFD-frontend VS0 rule surface to a native wasm32 module.
#
#   scripts/build_frontend_vs0_wasm.sh <out.wasm>
#
# What "VS0" means here, precisely: the RULES a 1700s-start SHANKPIT-frontend/GFD-MMO world runs on --
# the era/train tech tree, traditional mining, the citizens' own day, and TRAPX's citizen contacts. It is
# not the 3D frontend in wasm (that is the C engine, a much larger lift, named in
# SHANKPIT/docs2/GFD_FRONTEND_VS0_NORTHSTAR.md as not built). What this proves is the seam the frontend
# depends on: the SAME .prn files the C host compiles are the ones the browser runs, so a train cannot
# unlock in one and stay locked in the other.
#
# Toolchain, same promises as the other web games (brawlpit, shankpit-460, d2, empire, trapx):
# parena build -> opt -> llc -> wasm-ld, no Emscripten, no malloc, zero imports, fixed 8 MB.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:?usage: build_frontend_vs0_wasm.sh <out.wasm>}"

TC="${LLVM_TOOLCHAIN_ROOT:-$HOME/.local/opt/llvm-toolchain}"
BIN="$TC/usr/lib/llvm-18/bin"
if [ ! -x "$BIN/llc" ] && [ -x /usr/lib/llvm-18/bin/llc ]; then
  # The distro package (apt install llvm-18 lld-18) puts the same binaries here; the standalone
  # toolchain layout above stays the default so an existing box does not change behaviour.
  BIN=/usr/lib/llvm-18/bin
fi
export LD_LIBRARY_PATH="$TC/usr/lib/x86_64-linux-gnu:$TC/usr/lib/llvm-18/lib:${LD_LIBRARY_PATH:-}"
for t in opt llc wasm-ld llvm-nm llvm-objdump; do
  [ -x "$BIN/$t" ] || { echo "build-frontend-vs0-wasm: missing $BIN/$t (install llvm-18 + lld-18)" >&2; exit 1; }
done

PARENA="${PARENA_BIN:-./.parena-bootstrap}"
[ -x "$PARENA" ] || make .parena-bootstrap >/dev/null

W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT

# One build, four modules: multi-file is what lets each rules file keep its own (module ...) declaration
# while the emitted object carries all of them.
"$PARENA" build \
  stdlib/shankpit/era_tech_rules.prn \
  stdlib/shankpit/mining_rules.prn \
  stdlib/shankpit/citizen_day_rules.prn \
  stdlib/trapx/citizen_contact_rules.prn \
  -o "$W/frontend_vs0.ll"

"$BIN/opt" -O2 -S "$W/frontend_vs0.ll" -o "$W/frontend_vs0.opt.ll"
"$BIN/llc" -O2 -mtriple=wasm32-unknown-unknown -filetype=obj "$W/frontend_vs0.opt.ll" -o "$W/frontend_vs0.o"

# The two failures that would otherwise only show up at page load.
if "$BIN/llvm-nm" "$W/frontend_vs0.o" | grep -Eqw '(malloc|calloc|realloc|free|sbrk|brk|memalign|posix_memalign|aligned_alloc)'; then
  echo "FAIL: an allocator symbol is present in the object code" >&2; exit 1
fi
if "$BIN/llvm-objdump" -d "$W/frontend_vs0.o" | grep -Eq 'memory\.grow'; then
  echo "FAIL: the module contains a memory.grow instruction" >&2; exit 1
fi

"$BIN/wasm-ld" --no-entry --export-all --allow-undefined --export-memory \
  --initial-memory=8388608 --max-memory=8388608 -z stack-size=65536 -o "$OUT" "$W/frontend_vs0.o"

if command -v node >/dev/null 2>&1; then
  node --input-type=module -e "
import fs from 'node:fs';
const m = new WebAssembly.Module(fs.readFileSync(process.argv[1]));
const imp = WebAssembly.Module.imports(m);
if (imp.length) { console.error('FAIL: module has imports (a libcall leaked in): ' + JSON.stringify(imp)); process.exit(1); }
const need = ['era_year','tech_can_buy','train_unlocked','train_capacity','strike_yield','ore_from_variant',
              'cd_activity','cd_phase','cc_contact_chance_milli','cc_service_price_milli'];
const have = WebAssembly.Module.exports(m).map(e => e.name);
const missing = need.filter(n => !have.includes(n));
if (missing.length) { console.error('FAIL: missing exports: ' + missing.join(', ')); process.exit(1); }
" "$OUT"
fi
echo "built $OUT ($(wc -c < "$OUT") bytes, 0 imports, fixed 8 MB memory, no allocator)"
