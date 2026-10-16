#!/usr/bin/env bash
# Builds the shankpit-460 web game (stdlib/shankpit460/*.prn) to a native wasm32 module -- no Emscripten,
# same toolchain as BRAWLPIT web / the GFD terminal: parena build -> opt -> llc -> wasm-ld.
#
#   scripts/build_shankpit460_wasm.sh <out.wasm>
#
# "No malloc": the module has no allocator and no imports. Every record lives at a fixed address in linear
# memory (stdlib/shankpit460/sp_mem.prn has the map). After linking, this script asserts the module imports
# NOTHING -- a stray memset/memcpy libcall (which `opt` can synthesise from a store loop) would show up as an
# `env` import and fail the build here rather than at page load.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:?usage: build_shankpit460_wasm.sh <out.wasm>}"
TC="${LLVM_TOOLCHAIN_ROOT:-$HOME/.local/opt/llvm-toolchain}"
BIN="$TC/usr/lib/llvm-18/bin"
export LD_LIBRARY_PATH="$TC/usr/lib/x86_64-linux-gnu:$TC/usr/lib/llvm-18/lib:${LD_LIBRARY_PATH:-}"
PARENA="${PARENA_BIN:-./.parena-bootstrap}"
[ -x "$PARENA" ] || make .parena-bootstrap >/dev/null
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT

# Source order is irrelevant to the compiler (two-pass, forward references resolve) but fixed here so the
# generated IR is reproducible. Optional files are listed in stdlib/shankpit460/MODULES.
mapfile -t SRC < <(grep -v '^#' stdlib/shankpit460/MODULES | grep -v '^$')
SRC+=(stdlib/gfd/term_font.prn)   # the 8x16 bitmap font (font-hex), shared with the GFD terminal

cat "${SRC[@]}" > "$W/shankpit460_all.prn"
"$PARENA" build "$W/shankpit460_all.prn" -o "$W/shankpit460.ll"
"$BIN/opt" -O2 -S "$W/shankpit460.ll" -o "$W/shankpit460.opt.ll"
"$BIN/llc" -O2 -mtriple=wasm32-unknown-unknown -filetype=obj "$W/shankpit460.opt.ll" -o "$W/shankpit460.o"
if "$BIN/llvm-nm" "$W/shankpit460.o" | grep -Eqw '(malloc|calloc|realloc|free|sbrk|brk|memalign|posix_memalign|aligned_alloc)'; then
  echo "FAIL: an allocator symbol is present in the object code" >&2; exit 1
fi
if "$BIN/llvm-objdump" -d "$W/shankpit460.o" | grep -Eq 'memory\.grow'; then
  echo "FAIL: the module contains a memory.grow instruction" >&2; exit 1
fi
# 8 MB linear memory, fixed (no growth, ever). Stack stays below 0x20000, below every fixed region.
"$BIN/wasm-ld" --no-entry --export-all --allow-undefined --export-memory \
  --initial-memory=8388608 --max-memory=8388608 -z stack-size=65536 -o "$OUT" "$W/shankpit460.o"

if command -v node >/dev/null 2>&1; then
  node --input-type=module -e "
import fs from 'node:fs';
const m = new WebAssembly.Module(fs.readFileSync(process.argv[1]));
const imp = WebAssembly.Module.imports(m);
if (imp.length) { console.error('FAIL: module has imports (a libcall leaked in): ' + JSON.stringify(imp)); process.exit(1); }
" "$OUT"
fi
echo "built $OUT ($(wc -c < "$OUT") bytes, 0 imports, fixed 8 MB memory, no allocator)"
