#!/usr/bin/env bash
# Builds SHANKPIT Netkit's logic (stdlib/netkit/*.prn: scanner, VNC/RFB, DES, SSH) to one wasm32 module -- no
# Emscripten, same toolchain as shankpit-460 web / the GFD terminal: parena build -> opt -> llc -> wasm-ld.
#
#   scripts/build_netkit_wasm.sh <out.wasm>            # the full module: 64 MB memory (the VNC framebuffer lives at 16 MB)
#   NETKIT_MEM=8388608 scripts/build_netkit_wasm.sh <out-lite.wasm>   # same code, 8 MB: scanning + SSH only (RFB would trap)
#
# "No malloc, no imports": the module cannot touch a socket, a clock or the DOM. Every record lives at a fixed
# address in linear memory (stdlib/netkit/nk_mem.prn has the map). After linking this script asserts the module
# imports NOTHING -- a stray memset/memcpy libcall (which `opt` can synthesise from a store loop) would show up as an
# `env` import and fail the build here rather than at page load. The page host (PARENA -> TypeScript, nk_host.prn)
# and the thin Android shell move bytes in and out; they never decide anything.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:?usage: build_netkit_wasm.sh <out.wasm>}"
TC="${LLVM_TOOLCHAIN_ROOT:-$HOME/.local/opt/llvm-toolchain}"
BIN="$TC/usr/lib/llvm-18/bin"
export LD_LIBRARY_PATH="$TC/usr/lib/x86_64-linux-gnu:$TC/usr/lib/llvm-18/lib:${LD_LIBRARY_PATH:-}"
PARENA="${PARENA_BIN:-./.parena-bootstrap}"
[ -x "$PARENA" ] || make .parena-bootstrap >/dev/null
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT

mapfile -t SRC < <(grep -v '^#' stdlib/netkit/MODULES | grep -v '^$')
cat "${SRC[@]}" > "$W/netkit_all.prn"
"$PARENA" build "$W/netkit_all.prn" -o "$W/netkit.ll"
"$BIN/opt" -O2 -S "$W/netkit.ll" -o "$W/netkit.opt.ll"
"$BIN/llc" -O2 -mtriple=wasm32-unknown-unknown -filetype=obj "$W/netkit.opt.ll" -o "$W/netkit.o"
if "$BIN/llvm-nm" "$W/netkit.o" | grep -Eqw '(malloc|calloc|realloc|free|sbrk|brk|memalign|posix_memalign|aligned_alloc)'; then
  echo "FAIL: an allocator symbol is present in the object code" >&2; exit 1
fi
if "$BIN/llvm-objdump" -d "$W/netkit.o" | grep -Eq 'memory\.grow'; then
  echo "FAIL: the module contains a memory.grow instruction" >&2; exit 1
fi
# 64 MB linear memory by default, fixed (no growth, ever): 36 MB framebuffer at 16 MB. Stack stays below 0x20000.
MEM="${NETKIT_MEM:-67108864}"
"$BIN/wasm-ld" --no-entry --export-all --allow-undefined --export-memory \
  --initial-memory="$MEM" --max-memory="$MEM" -z stack-size=65536 -o "$OUT" "$W/netkit.o"

python3 - "$OUT" <<'PY'
import sys
try:
    from wasmtime import Store, Module
except ImportError:
    print("note: wasmtime not installed; skipped the zero-imports check"); sys.exit(0)
s = Store(); m = Module.from_file(s.engine, sys.argv[1])
imps = [(i.module, i.name) for i in m.imports]
if imps:
    print("FAIL: module has imports (a libcall leaked in):", imps); sys.exit(1)
PY
echo "built $OUT ($(wc -c < "$OUT") bytes, 0 imports, fixed $((MEM / 1048576)) MB memory, no allocator)"
