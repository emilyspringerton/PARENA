#!/usr/bin/env bash
# build_level_wasm.sh -- build a SHANKPIT level (PARENA -> wasm32) from its manifest, and prove its surface.
#
#   scripts/build_level_wasm.sh <level.manifest> <out.wasm>
#   e.g. scripts/build_level_wasm.sh stdlib/brawlpit/level.manifest /tmp/brawlpit_level.wasm
#
# Founder real-time 2026-11-08 (SHANKPIT T0, "implement ... parena first mods first api development"):
# BRAWLPIT boots inside the SHANKPIT world as a WASM level. The contract is
# SHANKPIT/docs/T0_LEVEL_WASM_CONTRACT.md; the manifest (format at the top of stdlib/brawlpit/level.manifest)
# is the single list of the level's sources, its exports and the imports it may use, so this script, the
# SHANKPIT shell (which file the view-source overlay opens) and the surface check never disagree.
#
# Same toolchain as the web build (build_brawlpit_wasm.sh, which stays untouched): parena build -> opt ->
# llc -> wasm-ld, no Emscripten, LLVM 18 at ~/.local/opt/llvm-toolchain (MONOREPO BOOTSTRAP.md has the
# no-sudo recipe; wasm-ld comes from lld-18). What is different, and why:
#   - explicit --export= per manifest line, not --export-all: the host binds a fixed surface, and an
#     accidental export is an accidental API;
#   - --allow-undefined-file with exactly the manifest's import names, not --allow-undefined: any OTHER
#     undefined symbol (a memset libcall `opt` synthesised from a store loop, a typo in an FFI name) fails
#     the LINK instead of becoming a silent `env` import that the host would refuse at load;
#   - then tools/wasm_surface_check (PARENA's own wasm reader, no node) asserts the exports are EXACTLY the
#     manifest's plus `memory`, and every import is on the allowlist.
#   - a compile error is reported as file:line of the original source, not of the concatenation.
# Exit status: 0 built and proven; 1 compile/link/surface failure (the previous output, if any, is left
# alone: the module is written to a temp file and moved into place only after every check passes).
set -euo pipefail
cd "$(dirname "$0")/.."
MANIFEST="${1:?usage: build_level_wasm.sh <level.manifest> <out.wasm>}"
OUT="${2:?usage: build_level_wasm.sh <level.manifest> <out.wasm>}"
TC="${LLVM_TOOLCHAIN_ROOT:-$HOME/.local/opt/llvm-toolchain}"
BIN="$TC/usr/lib/llvm-18/bin"
export LD_LIBRARY_PATH="$TC/usr/lib/x86_64-linux-gnu:$TC/usr/lib/llvm-18/lib:${LD_LIBRARY_PATH:-}"
for tool in opt llc wasm-ld llvm-nm llvm-objdump; do
  [ -x "$BIN/$tool" ] || { echo "build_level_wasm: missing $BIN/$tool (LLVM 18 + lld-18; see MONOREPO BOOTSTRAP.md)" >&2; exit 1; }
done
PARENA="${PARENA_BIN:-./.parena-bootstrap}"
[ -x "$PARENA" ] || make .parena-bootstrap >/dev/null
make -s tools/wasm_surface_check >/dev/null
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT

# ---- the manifest ---------------------------------------------------------------------------------------
SRC=(); EXPORTS=(); IMPORTS=()
while read -r key a b; do
  case "$key" in
    ''|'#'*) ;;
    name|entry) ;;
    src) SRC+=("$a") ;;
    export) EXPORTS+=("$a $b") ;;
    import) IMPORTS+=("$a $b") ;;
    *) echo "build_level_wasm: $MANIFEST: unknown directive '$key'" >&2; exit 1 ;;
  esac
done < "$MANIFEST"
[ "${#SRC[@]}" -gt 0 ] && [ "${#EXPORTS[@]}" -gt 0 ] || { echo "build_level_wasm: $MANIFEST lists no src or no export" >&2; exit 1; }

# ---- concatenate, remembering where each file starts -----------------------------------------------------
: > "$W/all.prn"; : > "$W/linemap"
line=1
for f in "${SRC[@]}"; do
  [ -f "$f" ] || { echo "build_level_wasm: $MANIFEST names $f, which does not exist" >&2; exit 1; }
  n=$(wc -l < "$f")
  echo "$line $n $f" >> "$W/linemap"
  cat "$f" >> "$W/all.prn"
  # a file without a trailing newline would glue its last form onto the next file's first
  [ -z "$(tail -c1 "$f")" ] || { echo >> "$W/all.prn"; n=$((n + 1)); }
  line=$((line + n))
done

map_line() {   # concatenated line -> "file:line"
  awk -v L="$1" '{ if (L >= $1 && L < $1 + $2) { printf "%s:%d", $3, L - $1 + 1; found = 1; exit } }
                 END { if (!found) printf "<concatenation>:%d", L }' "$W/linemap"
}

if ! "$PARENA" build "$W/all.prn" -o "$W/level.ll" > "$W/parena.log" 2>&1; then
  msg="$(grep -v '^parena: .* -> ' "$W/parena.log" | head -5)"
  if [[ "$msg" =~ line\ ([0-9]+) ]]; then
    echo "build_level_wasm: compile error at $(map_line "${BASH_REMATCH[1]}"): $msg" >&2
  elif [[ "$msg" =~ \(in\ defn\ ([^\)]+)\) ]]; then
    # emitter errors name the function, not the line: find where that defn is written
    where="$(grep -n -F "(defn ${BASH_REMATCH[1]} " "${SRC[@]}" | head -1 | cut -d: -f1,2)"
    echo "build_level_wasm: compile error at ${where:-<unknown>} (defn ${BASH_REMATCH[1]}): $msg" >&2
  else
    # the LLVM emitter's own errors carry no line yet (a named PARENA gap); say so rather than guess one
    echo "build_level_wasm: compile error (the compiler gave no line for this one): $msg" >&2
  fi
  exit 1
fi

"$BIN/opt" -O2 -S "$W/level.ll" -o "$W/level.opt.ll"
"$BIN/llc" -O2 -mtriple=wasm32-unknown-unknown -filetype=obj "$W/level.opt.ll" -o "$W/level.o"

# "No malloc", checked on the object code like the web build: no allocator, no memory growth.
if "$BIN/llvm-nm" "$W/level.o" | grep -Eqw '(malloc|calloc|realloc|free|sbrk|brk|memalign|posix_memalign|aligned_alloc)'; then
  echo "build_level_wasm: FAIL: an allocator symbol is present in the object code" >&2; exit 1
fi
if "$BIN/llvm-objdump" -d "$W/level.o" | grep -Eq 'memory\.grow'; then
  echo "build_level_wasm: FAIL: the module contains a memory.grow instruction" >&2; exit 1
fi

# ---- link: exactly the manifest's exports, only the manifest's imports may stay undefined -----------------
EXPORT_FLAGS=()
: > "$W/exports.spec"; : > "$W/imports.allowed"; : > "$W/imports.syms"
for e in "${EXPORTS[@]}"; do
  set -- $e
  EXPORT_FLAGS+=("--export=$1")
  echo "export $1 func $2" >> "$W/exports.spec"
done
echo "export memory memory -" >> "$W/exports.spec"
for i in "${IMPORTS[@]}"; do
  set -- $i
  echo "${1#*.}" >> "$W/imports.syms"
  echo "import $1 func $2" >> "$W/imports.allowed"
done
# 8 MB linear memory, fixed (no growth, ever). Stack stays below 0x20000, below every fixed region.
if ! "$BIN/wasm-ld" --no-entry "${EXPORT_FLAGS[@]}" --export-memory \
      --allow-undefined-file="$W/imports.syms" \
      --initial-memory=8388608 --max-memory=8388608 -z stack-size=65536 \
      -o "$W/level.wasm" "$W/level.o" 2> "$W/ld.log"; then
  echo "build_level_wasm: link failed (an undefined symbol that is not a manifest import fails here):" >&2
  head -10 "$W/ld.log" >&2
  exit 1
fi

# ---- prove the surface, with PARENA's own wasm reader -------------------------------------------------------
if ! tools/wasm_surface_check "$W/level.wasm" --exports "$W/exports.spec" --imports-allowed "$W/imports.allowed" > "$W/surface.log"; then
  echo "build_level_wasm: the module's surface does not match $MANIFEST:" >&2
  cat "$W/surface.log" >&2
  exit 1
fi
nimp=$(tools/wasm_surface_check "$W/level.wasm" | grep -c '^import ' || true)
mkdir -p "$(dirname "$OUT")"
mv "$W/level.wasm" "$OUT.tmp" && mv "$OUT.tmp" "$OUT"
echo "built $OUT ($(wc -c < "$OUT") bytes, ${#EXPORTS[@]} exports + memory, $nimp of ${#IMPORTS[@]} allowed imports used, fixed 8 MB, no allocator)"
