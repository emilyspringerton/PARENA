#!/usr/bin/env bash
# Builds the TRAPX VS0 sandbox (stdlib/trapx/web/*.prn + the rules it calls) to a native wasm32 module.
#
# Same toolchain and same promises as the other web games (brawlpit, shankpit-460, d2, empire):
# parena build -> opt -> llc -> wasm-ld, no Emscripten, no malloc, zero imports, fixed 8 MB.
#
#   scripts/build_trapx_wasm.sh <out.wasm>
#
# Why a wasm city at all: the city is integer rules and a fixed 14-word-per-district record, which is
# exactly what a data-driven page does badly and a fixed-memory module does well. The rules file is
# compiled IN as a source, not re-implemented, so a threshold can never drift between this and the C.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:?usage: build_trapx_wasm.sh <out.wasm>}"
TC="${LLVM_TOOLCHAIN_ROOT:-$HOME/.local/opt/llvm-toolchain}"
BIN="$TC/usr/lib/llvm-18/bin"
export LD_LIBRARY_PATH="$TC/usr/lib/x86_64-linux-gnu:$TC/usr/lib/llvm-18/lib:${LD_LIBRARY_PATH:-}"
for t in opt llc wasm-ld llvm-nm llvm-objdump; do
  [ -x "$BIN/$t" ] || { echo "build-trapx-wasm: missing $BIN/$t (install llvm-18 + lld-18)" >&2; exit 1; }
done
PARENA="${PARENA_BIN:-./.parena-bootstrap}"
[ -x "$PARENA" ] || make .parena-bootstrap >/dev/null
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT

mapfile -t SRC < <(grep -v '^#' stdlib/trapx/MODULES | grep -v '^$')
cat "${SRC[@]}" > "$W/trapx_all.prn"
"$PARENA" build "$W/trapx_all.prn" -o "$W/trapx.ll"
"$BIN/opt" -O2 -S "$W/trapx.ll" -o "$W/trapx.opt.ll"
"$BIN/llc" -O2 -mtriple=wasm32-unknown-unknown -filetype=obj "$W/trapx.opt.ll" -o "$W/trapx.o"

# The two failures that would otherwise only show up at page load.
if "$BIN/llvm-nm" "$W/trapx.o" | grep -Eqw '(malloc|calloc|realloc|free|sbrk|brk|memalign|posix_memalign|aligned_alloc)'; then
  echo "FAIL: an allocator symbol is present in the object code" >&2; exit 1
fi
if "$BIN/llvm-objdump" -d "$W/trapx.o" | grep -Eq 'memory\.grow'; then
  echo "FAIL: the module contains a memory.grow instruction" >&2; exit 1
fi

"$BIN/wasm-ld" --no-entry --export-all --allow-undefined --export-memory \
  --initial-memory=8388608 --max-memory=8388608 -z stack-size=65536 -o "$OUT" "$W/trapx.o"

if command -v node >/dev/null 2>&1; then
  node --input-type=module -e "
import fs from 'node:fs';
const m = new WebAssembly.Module(fs.readFileSync(process.argv[1]));
const imp = WebAssembly.Module.imports(m);
if (imp.length) { console.error('FAIL: module has imports (a libcall leaked in): ' + JSON.stringify(imp)); process.exit(1); }
" "$OUT"
fi
echo "built $OUT ($(wc -c < "$OUT") bytes, 0 imports, fixed 8 MB memory, no allocator)"
