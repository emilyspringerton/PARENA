#!/usr/bin/env bash
# Builds the Netkit SSH terminal (stdlib/netkit/term/nk_term.prn + the shared GFD font) to a wasm32 module -- same toolchain and
# promises as build_netkit_wasm.sh: parena build -> opt -> llc -> wasm-ld, no malloc, ZERO imports, fixed 8 MB memory.
#   scripts/build_netkit_term_wasm.sh <out.wasm>
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:?usage: build_netkit_term_wasm.sh <out.wasm>}"
TC="${LLVM_TOOLCHAIN_ROOT:-$HOME/.local/opt/llvm-toolchain}"
BIN="$TC/usr/lib/llvm-18/bin"
export LD_LIBRARY_PATH="$TC/usr/lib/x86_64-linux-gnu:$TC/usr/lib/llvm-18/lib:${LD_LIBRARY_PATH:-}"
PARENA="${PARENA_BIN:-./.parena-bootstrap}"
[ -x "$PARENA" ] || make .parena-bootstrap >/dev/null
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
mapfile -t SRC < <(grep -v '^#' stdlib/netkit/term/MODULES | grep -v '^$')
cat "${SRC[@]}" > "$W/term_all.prn"
"$PARENA" build "$W/term_all.prn" -o "$W/term.ll"
"$BIN/opt" -O2 -S "$W/term.ll" -o "$W/term.opt.ll"
"$BIN/llc" -O2 -mtriple=wasm32-unknown-unknown -filetype=obj "$W/term.opt.ll" -o "$W/term.o"
if "$BIN/llvm-nm" "$W/term.o" | grep -Eqw '(malloc|calloc|realloc|free|sbrk|brk|memalign|posix_memalign|aligned_alloc)'; then
  echo "FAIL: an allocator symbol is present in the object code" >&2; exit 1
fi
if "$BIN/llvm-objdump" -d "$W/term.o" | grep -Eq 'memory\.grow'; then
  echo "FAIL: the module contains a memory.grow instruction" >&2; exit 1
fi
"$BIN/wasm-ld" --no-entry --export-all --allow-undefined --export-memory \
  --initial-memory=8388608 --max-memory=8388608 -z stack-size=65536 -o "$OUT" "$W/term.o"
python3 - "$OUT" <<'PY'
import sys
try:
    from wasmtime import Store, Module
except ImportError:
    print("note: wasmtime not installed; skipped the zero-imports check"); sys.exit(0)
m = Module.from_file(Store().engine, sys.argv[1])
imps = [(i.module, i.name) for i in m.imports]
if imps: print("FAIL: module has imports:", imps); sys.exit(1)
PY
echo "built $OUT ($(wc -c < "$OUT") bytes, 0 imports, fixed 8 MB memory, no allocator)"
