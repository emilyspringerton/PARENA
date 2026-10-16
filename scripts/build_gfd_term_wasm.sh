#!/usr/bin/env bash
# Builds the GFD web terminal (stdlib/gfd/term.prn) to a native wasm32 module -- no Emscripten,
# same toolchain as MIXFORGE/DEADWEIGHT: parena build -> opt -> llc -> wasm-ld.
# Usage: scripts/build_gfd_term_wasm.sh <out.wasm>
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:?usage: build_gfd_term_wasm.sh <out.wasm>}"
TC="${LLVM_TOOLCHAIN_ROOT:-$HOME/.local/opt/llvm-toolchain}"
BIN="$TC/usr/lib/llvm-18/bin"
export LD_LIBRARY_PATH="$TC/usr/lib/x86_64-linux-gnu:$TC/usr/lib/llvm-18/lib:${LD_LIBRARY_PATH:-}"
PARENA="${PARENA_BIN:-./.parena-bootstrap}"
[ -x "$PARENA" ] || make .parena-bootstrap >/dev/null
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
cat stdlib/gfd/term_font.prn stdlib/gfd/term.prn > "$W/term_all.prn"
"$PARENA" build "$W/term_all.prn" -o "$W/term.ll"
"$BIN/opt" -O2 -S "$W/term.ll" -o "$W/term.opt.ll"
"$BIN/llc" -O2 -mtriple=wasm32-unknown-unknown -filetype=obj "$W/term.opt.ll" -o "$W/term.o"
# 8 MB linear memory (term.prn's map ends at ~5 MB), no growth needed. Stack stays below 0x20000.
"$BIN/wasm-ld" --no-entry --export-all --allow-undefined --export-memory \
  --initial-memory=8388608 --max-memory=8388608 -z stack-size=65536 -o "$OUT" "$W/term.o"
echo "built $OUT ($(wc -c < "$OUT") bytes)"
