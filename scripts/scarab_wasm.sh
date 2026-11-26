#!/usr/bin/env bash
# Builds and runs a SCARAB wasm suite: the modules under test + stdlib/scarab/wasm.prn + stdlib/firefly/ladybug_wasm.prn + the
# spec, concatenated into ONE wasm32 module (same toolchain and zero-imports rule as build_netkit_wasm.sh), then run by the
# PARENA -> TS host (stdlib/scarab/host.prn) under node 24+. Any extra files are loaded into the suite's file area, in order.
#
#   scripts/scarab_wasm.sh <spec.prn> [--modules <list file> | --module <a.prn> ...] [--file <path> ...]
#   e.g. scripts/scarab_wasm.sh tests/netkit/names_spec.prn --modules stdlib/netkit/MODULES --file /tmp/oui.bin
#
# Exit status: 0 all specs passed, 1 a spec failed, 2 the suite could not be built or loaded.
# SCARAB_KEEP=<dir> keeps the built suite.wasm and host there.
set -euo pipefail
cd "$(dirname "$0")/.."
SPEC="${1:?usage: scarab_wasm.sh <spec.prn> [--modules <list>] [--module <a.prn>] [--file <path>]}"; shift
TC="${LLVM_TOOLCHAIN_ROOT:-$HOME/.local/opt/llvm-toolchain}"
BIN="$TC/usr/lib/llvm-18/bin"
export LD_LIBRARY_PATH="$TC/usr/lib/x86_64-linux-gnu:$TC/usr/lib/llvm-18/lib:${LD_LIBRARY_PATH:-}"
PARENA="${PARENA_BIN:-./.parena-bootstrap}"
[ -x "$PARENA" ] || make .parena-bootstrap >/dev/null
W="${SCARAB_KEEP:-$(mktemp -d)}"; mkdir -p "$W"; [ -n "${SCARAB_KEEP:-}" ] || trap 'rm -rf "$W"' EXIT
SRC=(); FILES=()
while [ $# -gt 0 ]; do
  case "$1" in
    --modules) mapfile -t -O "${#SRC[@]}" SRC < <(grep -v '^#' "$2" | grep -v '^$'); shift 2;;
    --module) SRC+=("$2"); shift 2;;
    --file) FILES+=("$(realpath "$2")"); shift 2;;
    *) echo "scarab_wasm.sh: unknown argument $1" >&2; exit 2;;
  esac
done
cat "${SRC[@]}" stdlib/scarab/wasm.prn stdlib/firefly/ladybug_wasm.prn "$SPEC" > "$W/suite.prn"
{ "$PARENA" build "$W/suite.prn" -o "$W/suite.ll" >/dev/null \
  && "$BIN/opt" -O2 -S "$W/suite.ll" -o "$W/suite.opt.ll" \
  && "$BIN/llc" -O2 -mtriple=wasm32-unknown-unknown -filetype=obj "$W/suite.opt.ll" -o "$W/suite.o" \
  && "$BIN/wasm-ld" --no-entry --export-all --allow-undefined --export-memory \
       --initial-memory=100663296 --max-memory=100663296 -z stack-size=65536 -o "$W/suite.wasm" "$W/suite.o"; } \
  || { echo "FAIL: the suite $SPEC did not build" >&2; exit 2; }
if [ ! -f "$W/host.mjs" ] || [ stdlib/scarab/host.prn -nt "$W/host.mjs" ]; then
  "$PARENA" build stdlib/scarab/host.prn -o "$W/host.ts" >/dev/null
  node scripts/ts_strip.mjs "$W/host.ts" "$W/host.mjs" 2>/dev/null
fi
node "$W/host.mjs" "$W/suite.wasm" "${FILES[@]}"
