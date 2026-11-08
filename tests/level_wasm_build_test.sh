#!/usr/bin/env bash
# tests/level_wasm_build_test.sh -- `make test-level-wasm-build`: scripts/build_level_wasm.sh builds the real
# BRAWLPIT level and proves its surface, and refuses the three ways a level build goes wrong:
#   1. a compile error is reported at the ORIGINAL file:line, not the concatenation's;
#   2. calling a host function the manifest does not list fails the LINK (it would otherwise become an
#      `env` import the SHANKPIT host refuses at load -- caught at build time instead);
#   3. a manifest export with no function behind it fails the link.
# and a failed build leaves the previous output untouched (the host keeps running the old module).
# Needs the LLVM 18 toolchain (MONOREPO BOOTSTRAP.md); skips, loudly, without it.
set -uo pipefail
cd "$(dirname "$0")/.."
TC="${LLVM_TOOLCHAIN_ROOT:-$HOME/.local/opt/llvm-toolchain}"
if [ ! -x "$TC/usr/lib/llvm-18/bin/wasm-ld" ]; then
  echo "SKIP: no LLVM 18 wasm toolchain at $TC (see MONOREPO BOOTSTRAP.md)"; exit 0
fi
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
fail=0
pass() { echo "PASS: $1"; }
bad() { echo "FAIL: $1"; fail=1; }

# 0. the real level
if scripts/build_level_wasm.sh stdlib/brawlpit/level.manifest "$W/brawlpit_level.wasm" > "$W/ok.log" 2>&1; then
  pass "the BRAWLPIT level builds and its surface matches the manifest ($(tail -1 "$W/ok.log" | cut -c1-90)...)"
else
  bad "the BRAWLPIT level build failed:"; cat "$W/ok.log"
fi

# a two-file level: a.prn (3 lines) then b.prn
mkdir -p "$W/lv"
printf ';; a\n(defn level-api [] : I32 1)\n(defn helper [] : I32 2)\n' > "$W/lv/a.prn"
manifest() {  # $1 = b.prn body, $2 = extra manifest lines
  printf '%b' "$1" > "$W/lv/b.prn"
  { echo "name t"; echo "src $W/lv/a.prn"; echo "src $W/lv/b.prn"; echo "export level_api i()"; printf '%b' "$2"; } > "$W/lv/t.manifest"
}

# 1. compile errors are reported at the ORIGINAL file:line -- a parser error (it carries a line) and an
#    emitter error (it names the defn; the LLVM emitter's errors carry no line yet, a named PARENA gap)
manifest ';; b\n)\n' ""
echo "previous" > "$W/out.wasm"
out="$(scripts/build_level_wasm.sh "$W/lv/t.manifest" "$W/out.wasm" 2>&1)"
if [ $? -ne 0 ] && echo "$out" | grep -q "b.prn:2"; then pass "a parse error names the original file:line (b.prn:2, line 5 of the concatenation)"
else bad "parse error not mapped to b.prn:2: $out"; fi
[ "$(cat "$W/out.wasm")" = "previous" ] && pass "a failed build leaves the previous output untouched" || bad "the previous output was overwritten"
manifest ';; b\n;; c\n(defn broken [] : I32 (let [x] x))\n' ""
out="$(scripts/build_level_wasm.sh "$W/lv/t.manifest" "$W/out.wasm" 2>&1)"
if [ $? -ne 0 ] && echo "$out" | grep -q "b.prn:3"; then pass "an emitter error is located by its defn (b.prn:3)"
else bad "emitter error not located: $out"; fi

# 2. an FFI call the manifest does not list fails the link
manifest ';; b\n(llvm-extern "declare i32 @not_offered()")\n(defn sneaky [] : I32 #target {:llvm (inline-llvm "call i32 @not_offered()")})\n(defn level-enter [] : I32 (sneaky))\n' "export level_enter i()\n"
out="$(scripts/build_level_wasm.sh "$W/lv/t.manifest" "$W/out.wasm" 2>&1)"
if [ $? -ne 0 ] && echo "$out" | grep -q "not_offered"; then pass "calling an unlisted host function fails the link, by name"
else bad "an unlisted import got through: $out"; fi

# the same call, listed as an import, builds (so case 2 failed for the right reason)
manifest ';; b\n(llvm-extern "declare i32 @not_offered()")\n(defn sneaky [] : I32 #target {:llvm (inline-llvm "call i32 @not_offered()")})\n(defn level-enter [] : I32 (sneaky))\n' "export level_enter i()\nimport env.not_offered i()\n"
if scripts/build_level_wasm.sh "$W/lv/t.manifest" "$W/listed.wasm" > "$W/l.log" 2>&1 && tools/wasm_surface_check "$W/listed.wasm" | grep -q "import env.not_offered func i()"; then
  pass "listed in the manifest, the same call builds as an env import"
else bad "the listed import did not build: $(cat "$W/l.log")"; fi

# 3. an export with no function behind it fails the link
manifest ';; b\n(defn level-enter [] : I32 0)\n' "export level_enter i()\nexport level_missing v()\n"
out="$(scripts/build_level_wasm.sh "$W/lv/t.manifest" "$W/out.wasm" 2>&1)"
if [ $? -ne 0 ] && echo "$out" | grep -q "level_missing"; then pass "a manifest export with no function fails the build, by name"
else bad "a missing export got through: $out"; fi

# 4. a declared signature that does not match the code fails the surface check
manifest ';; b\n(defn level-enter [] : I32 0)\n' "export level_enter v(i)\n"
out="$(scripts/build_level_wasm.sh "$W/lv/t.manifest" "$W/out.wasm" 2>&1)"
if [ $? -ne 0 ] && echo "$out" | grep -q "level_enter"; then pass "an export whose signature differs from the manifest fails the surface check"
else bad "a wrong export signature got through: $out"; fi

[ $fail -eq 0 ] && echo "ALL PASS" || { echo "FAILED"; exit 1; }
