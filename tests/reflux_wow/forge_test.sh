#!/usr/bin/env bash
# tests/reflux_wow/forge_test.sh -- the plugin forge end to end: the command line over a fake WTF folder, planning a
# kit, `forge build` baking it into MacroSync (then /ascend writing it, forge_ascend_test.lua), and the local server's
# page, API and refusals. usage: bash tests/reflux_wow/forge_test.sh <work dir>
set -uo pipefail
work="$1"; rm -rf "$work"; mkdir -p "$work"
pass=0 fail=0
ok() { pass=$((pass + 1)); }
bad() { fail=$((fail + 1)); echo "FAIL: $*"; }
check() { if printf '%s' "$2" | grep -qF -- "$3"; then ok; else bad "$1 [$(printf '%s' "$2" | head -c 300)]"; fi; }

acct="$work/wtf/Account/1234#1"
mkdir -p "$acct/SavedVariables" "$acct/Forever/Emily/SavedVariables" "$work/forge"
cat > "$acct/SavedVariables/CharacterBoard.lua" <<'LUA'
CharacterBoardDB = { ["chars"] = {
  ["Emily-Forever"] = { ["t"] = 1763100000, ["class"] = "PALADIN", ["race"] = "Human", ["level"] = 42, ["xp"] = 500, ["xpmax"] = 1000, ["rested"] = 250, ["gold"] = 1234567, ["zone"] = "Ironforge", ["ilvl"] = 38, ["profs"] = "Mining 120/150" },
  ["Ash-Forever"] = { ["t"] = 1763000000, ["class"] = "MAGE", ["race"] = "Gnome", ["level"] = 60, ["gold"] = 9000000, ["zone"] = "Stormwind" } },
  ["rows"] = {
    { ["t"] = 1762900000, ["character"] = "Emily-Forever", ["level"] = 40, ["xp"] = 100, ["xpmax"] = 1000, ["gold"] = 1000000, ["zone"] = "Loch Modan" },
    { ["t"] = 1762910000, ["character"] = "Emily-Forever", ["level"] = 41, ["xp"] = 0, ["xpmax"] = 1000, ["gold"] = 1100000, ["zone"] = "Wetlands" },
    { ["t"] = 1762900500, ["character"] = "Ash-Forever", ["level"] = 60, ["gold"] = 8000000, ["zone"] = "Stormwind" },
    { ["t"] = 1763100000, ["character"] = "Emily-Forever", ["level"] = 42, ["xp"] = 500, ["xpmax"] = 1000, ["gold"] = 1234567, ["zone"] = "Ironforge" } } }
LUA
cat > "$acct/SavedVariables/FafnirsBed.lua" <<'LUA'
FafnirsBedDB = { ["chars"] = {
  ["Emily-Forever"] = { ["gold"] = 1300000, ["gear"] = { [1] = 1001, [16] = 1002 }, ["loc"] = { ["bags"] = { [2589] = 3 }, ["equipped"] = { [1001] = 1, [1002] = 1 } } },
  ["Ash-Forever"] = { ["gold"] = 9000000, ["loc"] = { ["bags"] = { [2589] = 20 } } } },
  ["names"] = { [2589] = "Linen Cloth", [1001] = "Lionheart Helm", [1002] = "Ashbringer" } }
LUA
echo 'MacroSyncDB = { ["managed"] = { ["PalJudge"] = 1, ["PalOld"] = 1 }, ["ascends"] = 2 }' > "$acct/Forever/Emily/SavedVariables/MacroSync.lua"
export FORGE_DIR="$work/forge" WOW_WTF_DIR="$work/wtf"
f() { bash scripts/forge.sh "$@" 2>&1; }

if env -u FORGE_DIR bash scripts/forge.sh characters >/dev/null 2>&1; then bad "ran without FORGE_DIR"; else ok; fi
o="$(f characters)"
check "characters lists Ash first by name" "$o" "Ash-Forever              MAGE      60      900g 0s 0c"
check "with Emily's gold from Fafnir" "$o" "Emily-Forever            PALADIN   42      130g 0s 0c"
o="$(f show em)"
check "show: level, race, class" "$o" "Emily-Forever -- level 42 Human PALADIN"
check "show: xp and rested" "$o" "xp 50% of the level, 25% rested"
check "show: gear by slot" "$o" "Main hand  Ashbringer"
check "show: bags" "$o" "Linen Cloth  x3"
check "show: bank not read" "$o" "bank: not read yet"
check "show: gold" "$o" "gold: 130g 0s 0c"
o="$(f show zed)"; check "an unknown character is refused" "$o" "no character called 'zed'"
o="$(f history)"
check "history: the account's gold on its first day" "$o" "2025-11-11        900g 0s 0c   2 characters"
check "history: a later day adds the latest of each" "$o" "2025-11-12        910g 0s 0c (+10g 0s 0c)"
o="$(f history emily)"
check "history: one character keeps the last snapshot of each day" "$o" "2025-11-12  level 41"
check "history: with the change in gold" "$o" "123g 45s 67c (+13g 45s 67c)  Ironforge"
o="$(f find linen)"
check "find: by part of a name, with the account total" "$o" "Linen Cloth (2589): 23 on the account"
check "find: who has it, most first" "$o" "Ash-Forever              20  (bags 20)"
o="$(f find 1002)"; check "find: by id, worn gear included" "$o" "Emily-Forever            1  (equipped 1)"
o="$(f find zzz)"; check "find: nothing found says so" "$o" "nothing seen named like 'zzz'"
o="$(f --json find nobody-has-this)"; check "find --json: no items is an empty list" "$o" '"items":[]' 
o="$(f drop emily PalExo)"; check "drop" "$o" "dropped PalExo"
o="$(f edit emily PalHeal '#showtooltip\n/cast {Holy Light}')"; check "edit" "$o" "changed PalHeal"
o="$(f add emily PalSpeed '/cast [mod:alt] {Divine Steed}')"; check "add" "$o" "added PalSpeed"
f add emily PalJudge x >/dev/null; [ $? -eq 1 ] && ok || bad "a taken name exits 1"
o="$(f kit emily)"
check "kit: 3 planned changes" "$o" "3 planned changes"
check "kit: what /ascend wrote is starred" "$o" "* PalJudge"
check "kit: what the next /ascend prunes" "$o" "the next /ascend prunes: PalOld"
check "kit: the edit, with its newline" "$o" "edited   PalHeal          #showtooltip \\n /cast {Holy Light}"
o="$(f --json characters)"; check "--json" "$o" '"character":"Emily-Forever"'
o="$(f --json show ash)"; check "--json: an empty location is an array" "$o" '"bank":{"items":[],"read":false}'
printf 'kit\nemily\n' > "$work/args.txt"
o="$(f --json --argfile "$work/args.txt")"; check "--argfile" "$o" '"changes":3'
o="$(f class Zed-Forever DEATHKNIGHT)"; check "plan for a character the Board has not seen" "$o" "planning Zed-Forever as a DEATHKNIGHT (no class set"
o="$(f add zed DkGrip '/cast Death Grip')"; check "and give it a macro" "$o" "added DkGrip"
o="$(f class Emily-Forever MAGE)"; check "a Board character's class cannot be changed" "$o" "already a PALADIN"
o="$(f copy emily ash)"; check "a plan only copies within a class" "$o" "only copies within a class"
o="$(f want emily offhand linen)"; check "want: a slot by name, an item by part of its name" "$o" "Emily-Forever wants Linen Cloth in Off hand"
o="$(f want emily head 1001)"; check "want: an item by id" "$o" "wants Lionheart Helm in Head"
o="$(f want emily neck 55555)"; check "want: an item nobody has seen yet" "$o" "wants item 55555 in Neck"
o="$(f want emily elbow 1)"; check "want: an unknown slot is refused" "$o" "no slot called 'elbow'"
o="$(f want emily neck zzzz)"; check "want: an unknown name is refused" "$o" "no item named like 'zzzz'"
o="$(f gear emily)"
check "gear: worn and planned" "$o" "Head       Lionheart Helm                want Lionheart Helm  [worn]"
check "gear: on the account, and where" "$o" "want Linen Cloth  [on the account]  (Ash-Forever bags 20; Emily-Forever bags 3)"
check "gear: not on the account" "$o" "want item 55555  [not on the account]"
o="$(f unwant emily neck)"; check "unwant" "$o" "nothing planned for Neck now"
o="$(f unwant emily neck)"; check "unwant twice is refused" "$o" "nothing is planned for Neck"
check "the plan keeps slot numbers as numbers" "$(cat "$FORGE_DIR/kits.lua")" "[17] = 2589,"
o="$(f --json show emily)"; check "show --json carries the gear plan" "$o" '"status":"on the account"'
o="$(f export)"; check "export" "$o" '["Emily-Forever"] = {'

# build: the plans go into MacroSync, and /ascend writes the planned kit
if WOW_INTERFACE=99999 COLLECTION_CLIENT_OUT="$work/client" bash scripts/forge.sh build >"$work/build.log" 2>&1; then ok; else bad "forge build failed: $(tail -3 "$work/build.log")"; fi
check "build says what it baked" "$(cat "$work/build.log")" "baking 2 character kit(s)"
lua5.1 tests/reflux_wow/forge_ascend_test.lua "$work/client" || fail=$((fail + 1))

# the server: page, API, and what it refuses
port=$((20000 + RANDOM % 20000))
bash scripts/forge.sh serve "$port" >"$work/serve.log" 2>&1 &
spid=$!
for _ in $(seq 1 50); do curl -s -o /dev/null "http://127.0.0.1:$port/" && break; sleep 0.2; done
o="$(curl -s "http://127.0.0.1:$port/")"; check "the page is served" "$o" "<title>Plugin Forge</title>"
o="$(printf 'show\nemily\n' | curl -s -H 'X-Forge: 1' --data-binary @- "http://127.0.0.1:$port/api")"; check "the API runs the forge" "$o" '"slot_name":"Main hand"'
o="$(printf 'edit\nemily\nPalTaunt\n/say "hi" $(touch %s/pwned)\n' "$work" | curl -s -H 'X-Forge: 1' --data-binary @- "http://127.0.0.1:$port/api")"
check "a body with quotes and \$( ) is just text" "$o" '"message":"changed PalTaunt'
[ -e "$work/pwned" ] && bad "a macro body ran a command" || ok
o="$(curl -s -d 'characters' "http://127.0.0.1:$port/api")"; check "no X-Forge header: refused" "$o" "answers its own page only"
o="$(curl -s -H 'Host: evil.example' -H 'X-Forge: 1' -d 'characters' "http://127.0.0.1:$port/api")"; check "another Host: refused" "$o" "open the forge at"
kill "$spid" 2>/dev/null; wait "$spid" 2>/dev/null

echo "forge: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
