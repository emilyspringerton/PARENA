#!/usr/bin/env bash
# tests/reflux_wow/suite.d/00-lead.sh -- the packager's own plug-in machinery (scripts/wow-package-collection.sh):
# suite fragments sourced in sorted order, COLLECTION_ONLY partial builds, and the needs / placeholder / readme
# helpers. Two throwaway fragments build test addons from Hrungnir's and Völva's real sources under new folder
# names. Run by tests/reflux_wow/run-suites.sh (LUA51, REFLUX_WOW_OUT exported); writes only under $REFLUX_WOW_OUT/lead.
set -uo pipefail
work="$REFLUX_WOW_OUT/lead"
rm -rf "$work" && mkdir -p "$work/frags" "$work/empty"
pass=0 fail=0
ok() { if [ "$1" = "$2" ]; then pass=$((pass+1)); else fail=$((fail+1)); echo "FAIL: $3 (got '$1', want '$2')"; fi; }
has() { case "$1" in *"$2"*) pass=$((pass+1)) ;; *) fail=$((fail+1)); echo "FAIL: $3 (no '$2' in: $1)" ;; esac; }
hasnt() { case "$1" in *"$2"*) fail=$((fail+1)); echo "FAIL: $3 ('$2' present)" ;; *) pass=$((pass+1)) ;; esac; }
pkg() { WOW_INTERFACE=99999 COLLECTION_CLIENT_OUT="$work/client" bash scripts/wow-package-collection.sh "$@" 2>&1; }

# 02-a: PktTest needs Hrungnir, uses a placeholder, adds README lines. 10-b: PktSkip, which COLLECTION_ONLY leaves out.
cat > "$work/frags/02-a.sh" <<'EOF'
echo 02-a >> "$PKT_ORDER"
placeholder PKT_VALUE forty-two
needs PktTest Hrungnir
fun PktTest PktTestDB hrungnir hrungnir.prn hrungnir.lua hrungnir.lua "Pkt Test" "Packager test addon." SavedVariables
glue "$PKT_SRC/pkt_glue.lua" "$OUT/AddOns/PktTest/pkt_glue.lua" PktTest PktTestDB
readme PktTest "  /pkt               the packager test line"
readme_after PktTest "  pkt after-playing line"
EOF
cat > "$work/frags/10-b.sh" <<'EOF'
echo 10-b >> "$PKT_ORDER"
needs PktSkip Volva
fun PktSkip PktSkipDB volva volva.prn volva.lua volva.lua "Pkt Skip" "Packager test addon, skipped." SavedVariables
readme PktSkip "  /pktskip           must not appear"
EOF
printf 'local x = "@PKT_VALUE@" -- @ADDON@ keeps @VAR@\n' > "$work/frags/pkt_glue.lua"
export PKT_ORDER="$work/order" PKT_SRC="$work/frags"

# --- a partial build through both fragments ---------------------------------------------------------------------
out="$(COLLECTION_FRAGMENTS_DIR="$work/frags" COLLECTION_ONLY=PktTest,Hrungnir pkg)"; rc=$?
ok "$rc" 0 "partial build succeeds"
ok "$(LC_ALL=C ls "$work/client/AddOns" | tr '\n' ' ')" "CollectionJournal Hrungnir PktTest RefluxCore " \
  "only the named addons and the base: FafnirsBed, the boards, games and PktSkip are left out"
ok "$(tr -d '\r' < "$work/order" | tr '\n' ' ')" "02-a 10-b " "fragments are sourced in sorted order"
has "$out" "fragments: 02-a.sh 10-b.sh" "the summary names the fragments"
has "$out" "RefluxCore, CollectionJournal, Hrungnir, PktTest, reader" "the summary names what was built, in order"
toc="$(tr -d '\r' < "$work/client/AddOns/PktTest/PktTest.toc")"
has "$toc" "## Dependencies: RefluxCore, Hrungnir" "needs adds the dependency to the .toc"
has "$toc" "hrungnir_rules.lua" "fun lists the module in the .toc"
has "$(tr -d '\r' < "$work/client/AddOns/Hrungnir/Hrungnir.toc")" "## Dependencies: RefluxCore
" "an addon without needs keeps RefluxCore alone"
ok "$(tr -d '\r' < "$work/client/AddOns/PktTest/pkt_glue.lua")" 'local x = "forty-two" -- PktTest keeps PktTestDB' \
  "placeholder fills @PKT_VALUE@ beside the built-in @ADDON@ and @VAR@"
ok "$([ -s "$work/client/AddOns/PktTest/hrungnir_rules.lua" ] && echo built)" built "the fragment's module is compiled"
readme="$(tr -d '\r' < "$work/client/README.txt")"
has "$readme" "PARTIAL DEV BUILD, not a release (COLLECTION_ONLY=PktTest,Hrungnir)" "README says it is partial"
has "$readme" "/ascend            MacroSync: load your class's macros (paladin, priest, mage, shaman, druid); /ascend preview changes nothing
  /pkt               the packager test line
  Each character is learned" "readme lines join IN GAME after the built-in ones"
has "$readme" "a snapshot now.
  pkt after-playing line" "readme_after lines end AFTER PLAYING"
hasnt "$readme" "/pktskip" "a skipped addon's README line is dropped"
ok "$([ -s "$work/client/reader/ids.lua" ] && [ -x "$work/client/reader/read-journal.sh" ] && echo yes)" yes "the reader is built"
ok "$(LC_ALL=C ls -A "$work/client" | tr '\n' ' ')" "AddOns README.txt VERSION reader " "no scratch file is left behind"

# --- refusals ---------------------------------------------------------------------------------------------------
: > "$work/order"
out="$(COLLECTION_FRAGMENTS_DIR="$work/frags" COLLECTION_ONLY=PktTest pkg)"; rc=$?
ok "$rc" 2 "a selected addon without its needed one stops the build"
has "$out" "COLLECTION_ONLY: PktTest needs Hrungnir; add it: COLLECTION_ONLY=PktTest,Hrungnir" "and says what to add"
out="$(COLLECTION_FRAGMENTS_DIR="$work/empty" COLLECTION_ONLY=Hrungnir,Hrungir pkg)"; rc=$?
ok "$rc" 2 "a misspelt COLLECTION_ONLY name stops the build"
has "$out" "COLLECTION_ONLY names 'Hrungir', but no block or fragment builds that addon folder" "and names it"
out="$(COLLECTION_FRAGMENTS_DIR="$work/empty" COLLECTION_ONLY=Hrungnir pkg --zip)"; rc=$?
ok "$rc" 2 "--zip refuses a partial build"
has "$out" "a COLLECTION_ONLY build is partial" "and says why"
out="$(COLLECTION_ONLY='Hrungnir,bad-name' pkg)"; rc=$?
ok "$rc" 2 "a name that is not a Lua/addon name is refused"
has "$out" "'bad-name' is not a Lua/addon name" "and named"
out="$(COLLECTION_ONLY=' , ,' pkg)"; rc=$?
ok "$rc" 2 "a COLLECTION_ONLY that names nothing is refused"

echo "lead (packager plug-ins): $pass passed, $fail failed"
[ "$fail" -eq 0 ]
