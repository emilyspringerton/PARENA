#!/usr/bin/env bash
# The Netkit scanner's maker table, built by PARENA only (founder, 2026-11-26: "ALL PARENA BASED MODS BASED DEVELOPMENT"):
#   scripts/build_netkit_oui.sh <out oui.bin> [<in.tsv>]            pack stdlib/netkit/data/oui_brands.tsv, or another TSV (every page build; offline)
#   scripts/build_netkit_oui.sh --curate <manuf> <out.tsv>          re-curate from a fresh Wireshark manuf (when the registry changes)
# The logic is stdlib/netkit/nk_oui_gen.prn (wasm, zero imports); the host is stdlib/netkit/tools/nk_oui_host.prn (PARENA -> TS -> JS,
# run by node 24+). Needs the same LLVM toolchain as build_netkit_wasm.sh.
set -euo pipefail
cd "$(dirname "$0")/.."
TC="${LLVM_TOOLCHAIN_ROOT:-$HOME/.local/opt/llvm-toolchain}"
BIN="$TC/usr/lib/llvm-18/bin"
export LD_LIBRARY_PATH="$TC/usr/lib/x86_64-linux-gnu:$TC/usr/lib/llvm-18/lib:${LD_LIBRARY_PATH:-}"
PARENA="${PARENA_BIN:-./.parena-bootstrap}"
[ -x "$PARENA" ] || make .parena-bootstrap >/dev/null
if [ "${1:-}" = "--curate" ]; then MODE=curate; IN="${2:?manuf file}"; OUT="${3:?out.tsv}"; else MODE=pack; OUT="${1:?usage: build_netkit_oui.sh <out oui.bin> [<in.tsv>] | --curate <manuf> <out.tsv>}"; IN="${2:-stdlib/netkit/data/oui_brands.tsv}"; fi
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
"$PARENA" build stdlib/netkit/nk_oui_gen.prn -o "$W/gen.ll" >/dev/null
"$BIN/opt" -O2 -S "$W/gen.ll" -o "$W/gen.opt.ll"
"$BIN/llc" -O2 -mtriple=wasm32-unknown-unknown -filetype=obj "$W/gen.opt.ll" -o "$W/gen.o"
"$BIN/wasm-ld" --no-entry --export-all --allow-undefined --export-memory \
  --initial-memory=16777216 --max-memory=16777216 -z stack-size=65536 -o "$W/gen.wasm" "$W/gen.o"
node -e 'const m=new WebAssembly.Module(require("fs").readFileSync(process.argv[1])); if (WebAssembly.Module.imports(m).length) { console.error("FAIL: nk_oui_gen.wasm imports something"); process.exit(1); }' "$W/gen.wasm"
"$PARENA" build stdlib/netkit/tools/nk_oui_host.prn -o "$W/host.ts" >/dev/null
node scripts/ts_strip.mjs "$W/host.ts" "$W/host.mjs" 2>/dev/null
node "$W/host.mjs" "$W/gen.wasm" "$MODE" "$IN" "$OUT"
echo "built $OUT ($(wc -c < "$OUT") bytes, $MODE, by PARENA)"
