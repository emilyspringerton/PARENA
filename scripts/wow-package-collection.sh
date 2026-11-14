#!/usr/bin/env bash
# scripts/wow-package-collection.sh -- build the Collection of Value client: every addon that is built so far,
# plus the outside-the-game reader, as one folder (and optionally one zip).
#
#   AddOns/RefluxCore         the shared REFLUX log every other addon depends on
#   AddOns/CollectionJournal  REFLUX -> per-character SavedVariables journal (Step 6)
#   AddOns/FafnirsBed         Fafnir's Bed, the inventory manager: bags, bank, mail, gold across every
#                             character, net inventory, "who has this?" (spec addon 2)  -- /fafnir, /hoard
#   AddOns/Hrungnir           Hrungnir, the pet rock in a tiny frame that levels as you play (spec addon 70)  -- /rock
#   AddOns/RatatoskrRoll      Ratatoskr Roll, the critter census and Critter of the Week (spec addon 65)  -- /census
#   AddOns/Volva              Völva, a fortune at every login, sometimes from your own stats (spec addon 72)  -- /fortune
#   AddOns/SagaOfFools        Saga of Fools, joke achievements (spec addon 73)  -- /saga
#   reader/                   read-journal.sh: journal files -> eventstore NDJSON, after logout
#
# Every .lua module is `parena build` output; the only hand-written Lua is host glue in runtime/wow/.
#
# Environment (all overridable; see MONOREPO/scripts/collection-env.sh):
#   WOW_INTERFACE                    REQUIRED, never defaulted (an unconfirmed fact): the client's TOC number
#   COLLECTION_CLIENT_OUT            output folder                         (default build/wow/client)
#   COLLECTION_VERSION               version stamp                         (default: git short SHA, else dev)
#   COLLECTION_JOURNAL_ADDON / _VAR  journal addon folder / its global      (CollectionJournal / CollectionJournalDB)
#   COLLECTION_JOURNAL_KEEP          journal size cap in rows              (20000)
#   COLLECTION_JOURNAL_POLL_SECONDS  REFLUX poll interval while playing    (5)
#   FAFNIR_ADDON / FAFNIR_VAR        inventory addon folder / its global   (FafnirsBed / FafnirsBedDB)
#   HRUNGNIR_ADDON / HRUNGNIR_VAR    pet rock folder / its global           (Hrungnir / HrungnirDB)
#   RATATOSKR_ADDON / RATATOSKR_VAR  critter census folder / its global     (RatatoskrRoll / RatatoskrRollDB)
#   VOLVA_ADDON / VOLVA_VAR          fortune folder / its global            (Volva / VolvaDB)
#   SAGA_ADDON / SAGA_VAR            joke achievements folder / its global  (SagaOfFools / SagaOfFoolsDB)
#   PARENA_BIN                       compiler driver                       (./.parena-bootstrap)
#   WOW_ADDONS_DIR                   install target for --install
#   COLLECTION_WITH_TABLED           1 also packages tabled addons (EcoWar): tests only, never a release
#   COLLECTION_ONLY                  AddonA,AddonB: a partial dev build of only those addon folders, plus the base
#                                    every addon needs (RefluxCore, the journal) and the reader. Unset: the whole client.
#   COLLECTION_FRAGMENTS_DIR         where the suite fragments live        (default scripts/collection.d)
#
# Suite fragments (BOARD.md plug-in slots): after the built-in addons, every $COLLECTION_FRAGMENTS_DIR/*.sh is
# sourced in sorted (C locale) order, NN-<agent>.sh. A fragment adds its addons with the helpers the built-in blocks
# use (wrap, glue, toc, fun, game), plus needs (extra .toc dependencies), placeholder (extra @NAME@ glue values) and
# readme / readme_after (README.txt lines). The helpers skip any addon COLLECTION_ONLY leaves out, so fragments need
# no filtering of their own. Keep tabled addons behind COLLECTION_WITH_TABLED=1, as EcoWar is.
#
# usage: scripts/wow-package-collection.sh [--zip] [--install]     (--zip refuses a COLLECTION_ONLY build)
set -euo pipefail

cd "$(dirname "$0")/.."
PARENA_BIN="${PARENA_BIN:-./.parena-bootstrap}"
OUT="${COLLECTION_CLIENT_OUT:-build/wow/client}"
VERSION="${COLLECTION_VERSION:-$(git -c safe.directory='*' rev-parse --short HEAD 2>/dev/null || echo dev)}"
J_ADDON="${COLLECTION_JOURNAL_ADDON:-CollectionJournal}"
J_VAR="${COLLECTION_JOURNAL_VAR:-CollectionJournalDB}"
J_KEEP="${COLLECTION_JOURNAL_KEEP:-20000}"
J_POLL="${COLLECTION_JOURNAL_POLL_SECONDS:-5}"
F_ADDON="${FAFNIR_ADDON:-FafnirsBed}"
F_VAR="${FAFNIR_VAR:-FafnirsBedDB}"
H_ADDON="${HRUNGNIR_ADDON:-Hrungnir}"
H_VAR="${HRUNGNIR_VAR:-HrungnirDB}"
R_ADDON="${RATATOSKR_ADDON:-RatatoskrRoll}"
R_VAR="${RATATOSKR_VAR:-RatatoskrRollDB}"
V_ADDON="${VOLVA_ADDON:-Volva}"
V_VAR="${VOLVA_VAR:-VolvaDB}"
S_ADDON="${SAGA_ADDON:-SagaOfFools}"
S_VAR="${SAGA_VAR:-SagaOfFoolsDB}"

die() { echo "wow-package-collection: $*" >&2; exit 2; }
if [ -z "${WOW_INTERFACE:-}" ]; then
  echo "wow-package-collection: WOW_INTERFACE is not set." >&2
  echo "  Run in game:  /run print(select(4, GetBuildInfo()))  and export the number it prints." >&2
  exit 2
fi
case "$WOW_INTERFACE" in *[!0-9]*) die "WOW_INTERFACE must be digits, got '$WOW_INTERFACE'" ;; esac
for v in "$J_KEEP" "$J_POLL"; do case "$v" in ''|*[!0-9]*|0) die "journal KEEP/POLL must be positive integers" ;; esac; done
for n in "$J_ADDON" "$J_VAR" "$F_ADDON" "$F_VAR" "$H_ADDON" "$H_VAR" "$R_ADDON" "$R_VAR" "$V_ADDON" "$V_VAR" "$S_ADDON" "$S_VAR"; do
  case "$n" in [A-Za-z_]*) ;; *) die "'$n' is not a Lua/addon name" ;; esac
  case "$n" in *[!A-Za-z0-9_]*) die "'$n' is not a Lua/addon name" ;; esac
done
[ -x "$PARENA_BIN" ] || die "no compiler at $PARENA_BIN (make .parena-bootstrap)"

check_name() {
  case "$1" in [A-Za-z_]*) ;; *) die "'$1' is not a Lua/addon name" ;; esac
  case "$1" in *[!A-Za-z0-9_]*) die "'$1' is not a Lua/addon name" ;; esac
}

# COLLECTION_ONLY. SELECTED: the addon folders this run builds (all of them when ONLY is empty). SEEN: every addon
# folder a block or fragment reached, built or skipped, so a misspelt name is caught. BUILT: what was built, in order.
ONLY="" SELECTED=" RefluxCore $J_ADDON " SEEN=" RefluxCore " BUILT="RefluxCore"
if [ -n "${COLLECTION_ONLY:-}" ]; then
  IFS=', ' read -r -a only_names <<< "$COLLECTION_ONLY"
  for n in "${only_names[@]}"; do
    [ -n "$n" ] || continue
    check_name "$n"
    ONLY="$ONLY $n" SELECTED="$SELECTED$n "
  done
  [ -n "$ONLY" ] || die "COLLECTION_ONLY is set but names no addon"
fi
for a in "$@"; do
  case "$a" in --zip) [ -z "$ONLY" ] || die "--zip makes a release, and a COLLECTION_ONLY build is partial: unset COLLECTION_ONLY" ;; esac
done

# want ADDON -- note that a block reached ADDON; true when this run builds it.
want() {
  case "$SEEN" in *" $1 "*) ;; *) SEEN="$SEEN$1 " ;; esac
  [ -z "$ONLY" ] && return 0
  case "$SELECTED" in *" $1 "*) return 0 ;; esac
  return 1
}

# skip_path PATH -- true when PATH is inside an addon folder ($OUT/AddOns/<addon>/...) this run leaves out. Paths
# outside AddOns/ (the reader) are always built.
skip_path() {
  local rest="${1#"$OUT/AddOns/"}"
  [ "$rest" != "$1" ] || return 1
  ! want "${rest%%/*}"
}

rm -rf "$OUT"
mkdir -p "$OUT/AddOns" "$OUT/reader"

# RefluxCore, by its own packager (same refusal rules).
REFLUXCORE_OUT="$OUT/AddOns" PARENA_BIN="$PARENA_BIN" bash scripts/wow-package-refluxcore.sh >/dev/null

# wrap NAME OUT SRC.prn... -- compile the sources as ONE module and register it in RefluxCore.modules[NAME].
# One scratch file for every wrap, removed at the end: each process start costs seconds on a busy Windows box.
WRAP_TMP="$OUT/.wrap.lua"
wrap() {
  local name="$1" out="$2"
  shift 2
  skip_path "$out" && return 0
  "$PARENA_BIN" build "$@" -o "$WRAP_TMP" >/dev/null
  {
    echo "-- $name, built by parena from: $*. Do not edit; edit the .prn."
    echo "RefluxCore = RefluxCore or { modules = {} }"
    echo "RefluxCore.modules[\"$name\"] = (function(...)"
    cat "$WRAP_TMP"
    echo "end)(...)"
  } > "$out"
}

# glue SRC OUT -- host glue with its @...@ values filled from the environment (and from `placeholder`).
glue() {
  skip_path "$2" && return 0
  sed -e "s/@ADDON@/${3:-}/g" -e "s/@VAR@/${4:-}/g" -e "s/@KEEP@/$J_KEEP/g" -e "s/@POLL@/$J_POLL/g" \
      -e "s/@FAFNIR_VAR@/$F_VAR/g" -e "s/@HRUNGNIR_VAR@/$H_VAR/g" -e "s/@RATATOSKR_VAR@/$R_VAR/g" \
      -e "s/@VOLVA_VAR@/$V_VAR/g" -e "s/@JOURNAL_VAR@/$J_VAR/g" ${GLUE_EXTRA[@]+"${GLUE_EXTRA[@]}"} "$1" > "$2"
  if [[ "$(<"$2")" =~ @[A-Z_]*@ ]]; then die "unfilled placeholder left in $2"; fi
}

# placeholder NAME VALUE -- glue also fills @NAME@ with VALUE: a fragment's own names, e.g. another addon's global.
GLUE_EXTRA=()
placeholder() {
  case "$1" in [A-Z]*) ;; *) die "placeholder '$1': use [A-Z][A-Z0-9_]*" ;; esac
  case "$1" in *[!A-Z0-9_]*) die "placeholder '$1': use [A-Z][A-Z0-9_]*" ;; esac
  case "$2" in */*|*\\*|*\&*|*$'\n'*) die "placeholder @$1@: the value may not hold / \\ & or a newline" ;; esac
  GLUE_EXTRA+=(-e "s/@$1@/$2/g")
}

# needs ADDON DEP... -- ADDON's .toc lists each DEP after RefluxCore in ## Dependencies, so WoW loads DEP first.
# Call it before ADDON's toc. Under COLLECTION_ONLY, building ADDON without a DEP stops here and says what to add.
declare -A DEPS=()
needs() {
  local addon="$1" dep
  shift
  check_name "$addon"
  for dep in "$@"; do
    check_name "$dep"
    [ "$dep" != RefluxCore ] || continue
    case " ${DEPS[$addon]:-} " in *" $dep "*) continue ;; esac
    if [ -n "$ONLY" ]; then
      case "$SELECTED" in
        *" $addon "*) case "$SELECTED" in *" $dep "*) ;; *) die "COLLECTION_ONLY: $addon needs $dep; add it: COLLECTION_ONLY=$COLLECTION_ONLY,$dep" ;; esac ;;
      esac
    fi
    DEPS[$addon]="${DEPS[$addon]:+${DEPS[$addon]} }$dep"
  done
}

# readme ADDON LINE... -- lines for README.txt's IN GAME list; readme_after ADDON LINE... -- for AFTER PLAYING.
# Lines for an addon this run does not build are dropped.
README_GAME="" README_AFTER=""
readme() {
  local addon="$1" l
  shift
  want "$addon" || return 0
  for l in "$@"; do README_GAME="$README_GAME$l"$'\n'; done
}
readme_after() {
  local addon="$1" l
  shift
  want "$addon" || return 0
  for l in "$@"; do README_AFTER="$README_AFTER$l"$'\n'; done
}

# fun ADDON VAR MODULE PRN GLUE LOCALE TITLE NOTES -- one of the fun addons: its PARENA module, its catalogue,
# its host glue, and a per-character or account .toc (SVKIND in $FUN_SV).
fun() {
  local addon="$1" var="$2" module="$3" prn="$4" gluesrc="$5" locale="$6" title="$7" notes="$8" kind="$9"
  local d="$OUT/AddOns/$addon"
  want "$addon" || return 0
  mkdir -p "$d"
  wrap "collection/$module" "$d/${module}_rules.lua" stdlib/reflux/reflux.prn stdlib/collection/ids.prn "stdlib/collection/$prn"
  cp "runtime/wow/locales/$locale" "$d/locale.lua"
  glue "runtime/wow/$gluesrc" "$d/$gluesrc" "$addon" "$var"
  toc "$d/$addon.toc" "$title" "$notes" "$kind" "$var" "${module}_rules.lua" locale.lua "$gluesrc"
}

toc() {  # toc FILE TITLE NOTES SVKIND SVVAR FILES...
  local file="$1" title="$2" notes="$3" kind="$4" var="$5" addon deps=RefluxCore
  shift 5
  skip_path "$file" && return 0
  addon="${file#"$OUT/AddOns/"}" && addon="${addon%%/*}"
  if [ -n "${DEPS[$addon]:-}" ]; then deps="RefluxCore, ${DEPS[$addon]// /, }"; fi
  BUILT="$BUILT $addon"
  {
    echo "## Interface: $WOW_INTERFACE"
    echo "## Title: $title"
    echo "## Notes: $notes"
    echo "## Version: $VERSION"
    echo "## Dependencies: $deps"
    echo "## $kind: $var"
    echo "## X-Built-By: PARENA"
    echo
    for f in "$@"; do echo "$f"; done
  } > "$file"
}

j="$OUT/AddOns/$J_ADDON"
mkdir -p "$j"
wrap collection/journal "$j/journal.lua" stdlib/reflux/reflux.prn stdlib/reflux/cursor.prn stdlib/collection/journal.prn
glue runtime/wow/collection_journal.lua "$j/collection_journal.lua" "$J_ADDON" "$J_VAR"
toc "$j/$J_ADDON.toc" "Collection Journal" "Writes every REFLUX event to a per-character journal the outside reader collects." \
  SavedVariablesPerCharacter "$J_VAR" journal.lua collection_journal.lua

f="$OUT/AddOns/$F_ADDON"
mkdir -p "$f"
wrap collection/inventory "$f/inventory.lua" stdlib/reflux/reflux.prn stdlib/collection/ids.prn stdlib/collection/inventory.prn
glue runtime/wow/fafnirs_bed.lua "$f/fafnirs_bed.lua" "$F_ADDON" "$F_VAR"
toc "$f/$F_ADDON.toc" "Fafnir's Bed" "Every bag, bank and mailbox on the account in one hoard: net inventory and who has what. /fafnir" \
  SavedVariables "$F_VAR" inventory.lua fafnirs_bed.lua

fun "$H_ADDON" "$H_VAR" hrungnir hrungnir.prn hrungnir.lua hrungnir.lua "Hrungnir" \
  "A pet rock in a tiny frame. It levels up as you play and has opinions. /rock" SavedVariablesPerCharacter
fun "$R_ADDON" "$R_VAR" ratatoskr ratatoskr.prn ratatoskr_roll.lua ratatoskr.lua "Ratatoskr Roll" \
  "The critter census: every critter you /love, /pet or /wave at, and a Critter of the Week. /census" SavedVariables
fun "$V_ADDON" "$V_VAR" volva volva.prn volva.lua volva.lua "Völva" \
  "A fortune every time you log in, sometimes read from your own stats. /fortune" SavedVariablesPerCharacter
fun "$S_ADDON" "$S_VAR" saga saga.prn saga_of_fools.lua saga.lua "Saga of Fools" \
  "Joke achievements nobody asked for. /saga" SavedVariablesPerCharacter

# The Total Character Board (spec addon 1): the progression record the CSV export reads. /board
B_ADDON="${BOARD_ADDON:-CharacterBoard}"
B_VAR="${BOARD_VAR:-CharacterBoardDB}"
for n in "$B_ADDON" "$B_VAR"; do
  case "$n" in [A-Za-z_]*) ;; *) die "'$n' is not a Lua/addon name" ;; esac
  case "$n" in *[!A-Za-z0-9_]*) die "'$n' is not a Lua/addon name" ;; esac
done
bd="$OUT/AddOns/$B_ADDON"
mkdir -p "$bd"
wrap collection/board "$bd/board_rules.lua" stdlib/reflux/reflux.prn stdlib/collection/ids.prn stdlib/collection/games.prn \
  stdlib/collection/board.prn
cp runtime/wow/locales/board.lua "$bd/locale.lua"
glue runtime/wow/character_board.lua "$bd/character_board.lua" "$B_ADDON" "$B_VAR"
toc "$bd/$B_ADDON.toc" "Character Board" "Every character's level, xp, rested, gold, zone and professions, kept as a progression record for the CSV export. /board" \
  SavedVariables "$B_VAR" board_rules.lua locale.lua character_board.lua

# MacroSync (the spec's own addon): the sync rules and the class macro sets, two PARENA modules. /ascend
M_ADDON="${MACROSYNC_ADDON:-MacroSync}"
M_VAR="${MACROSYNC_VAR:-MacroSyncDB}"
for n in "$M_ADDON" "$M_VAR"; do
  case "$n" in [A-Za-z_]*) ;; *) die "'$n' is not a Lua/addon name" ;; esac
  case "$n" in *[!A-Za-z0-9_]*) die "'$n' is not a Lua/addon name" ;; esac
done
ms="$OUT/AddOns/$M_ADDON"
mkdir -p "$ms"
wrap collection/macrosync "$ms/macrosync_rules.lua" stdlib/reflux/reflux.prn stdlib/collection/macrosync.prn
wrap collection/macros "$ms/macros.lua" stdlib/collection/macros.prn
cp runtime/wow/locales/macrosync.lua "$ms/locale.lua"
glue runtime/wow/macrosync.lua "$ms/macrosync.lua" "$M_ADDON" "$M_VAR"
toc "$ms/$M_ADDON.toc" "MacroSync" "Your class's macros, loaded with /ascend; removed ones are pruned. Speaks in haiku." \
  SavedVariablesPerCharacter "$M_VAR" macrosync_rules.lua macros.lua locale.lua macrosync.lua

# The party games (spec 54-71). Each is built with collection/games and carries the shared kit (fun_kit.lua) and
# its catalogue. Folder and SavedVariables names are env-driven: GAME_<KEY>_ADDON / GAME_<KEY>_VAR.
# game KEY ADDON VAR MODULE GLUE TITLE NOTES SVKIND

game() {
  local key="$1" addon_default="$2" var_default="$3" module="$4" gluesrc="$5" title="$6" notes="$7" kind="$8"
  local av="GAME_${key}_ADDON" vv="GAME_${key}_VAR"
  local addon="${!av:-$addon_default}" var="${!vv:-$var_default}"
  for n in "$addon" "$var"; do
    case "$n" in [A-Za-z_]*) ;; *) die "'$n' is not a Lua/addon name" ;; esac
    case "$n" in *[!A-Za-z0-9_]*) die "'$n' is not a Lua/addon name" ;; esac
  done
  local d="$OUT/AddOns/$addon"
  want "$addon" || return 0
  mkdir -p "$d"
  wrap "collection/$module" "$d/${module}_rules.lua" stdlib/reflux/reflux.prn stdlib/collection/ids.prn \
    stdlib/collection/games.prn "stdlib/collection/$module.prn"
  cp runtime/wow/locales/fun_kit.lua "$d/kit_locale.lua"
  cp "runtime/wow/locales/$module.lua" "$d/locale.lua"
  cp runtime/wow/fun_kit.lua "$d/kit.lua"
  glue "runtime/wow/$gluesrc" "$d/$gluesrc" "$addon" "$var"
  toc "$d/$addon.toc" "$title" "$notes" "$kind" "$var" "${module}_rules.lua" kit_locale.lua locale.lua kit.lua "$gluesrc"
}
game HULDUFOLK HuldufolkHunt HuldufolkHuntDB huldufolk huldufolk_hunt.lua "Huldufólk Hunt" \
  "Hide and seek with hot-or-cold hints and a score board. /hunt" SavedVariables
game MJOLNIR MjolnirPass MjolnirPassDB mjolnir mjolnir_pass.lua "Mjölnir Pass" \
  "Tag for a party: touch someone to pass the hammer; who held it longest? /tag" SavedVariables
game RUNERIDDLE RuneRiddle RuneRiddleDB runeriddle rune_riddle.lua "Rune Riddle" \
  "Scavenger hunts: riddles point to real spots, arriving checks them off. /riddle" SavedVariables
game ODINN OdinnSays OdinnSaysDB odinn odinn_says.lua "Óðinn Says" \
  "Emote Simon Says: copy only when Óðinn says. /odinn" SavedVariables
game ANDVARI AndvarisMap AndvarisMapDB andvari andvaris_map.lua "Andvari's Map" \
  "Bury an X, draw a treasure map, mail the clue to a friend. /treasure" SavedVariables
game MIMIR MimirsRiddle MimirsRiddleDB mimir mimirs_riddle.lua "Mímir's Riddle" \
  "Trivia night in group or guild chat, scores kept over time. /mimir" SavedVariables
game SLEIPNIR SleipnirSprint SleipnirSprintDB sleipnir sleipnir_sprint.lua "Sleipnir Sprint" \
  "Foot and mount races with laps, times and a leaderboard. /sprint" SavedVariables
game FREYA FreyasRunway FreyasRunwayDB freya freyas_runway.lua "Freya's Runway" \
  "A fashion show: a runway queue and a judging panel. /runway" SavedVariables
game AEGIR AegirsCatch AegirsCatchDB aegir aegirs_catch.lua "Ægir's Catch" \
  "Fishing derbies: the biggest and the weirdest catch win. /derby" SavedVariables
game BRAGI BragisVerse BragisVerseDB bragi bragis_verse.lua "Bragi's Verse" \
  "Haiku duels with a syllable checker. /verse" SavedVariables
game ALFAR AlfarRevel AlfarRevelDB alfar alfar_revel.lua "Álfar Revel" \
  "Dance parties on the beat, with streak scores. /revel" SavedVariables
game NORNS NornsDraw NornsDrawDB norns norns_draw.lua "Norns' Draw" \
  "Bingo cards of in-world events. /bingo" SavedVariablesPerCharacter
game HOLMGANG Holmgang HolmgangDB holmgang holmgang.lua "Holmgang" \
  "Friendly duel tournaments: brackets, seeding and a champion. /holmgang" SavedVariables
game LOKI LokisLies LokisLiesDB loki lokis_lies.lua "Loki's Lies" \
  "Silly made-up news from your own adventures. /rumour" SavedVariablesPerCharacter
game TAPESTRY HallTapestry HallTapestryDB tapestry hall_tapestry.lua "Hall Tapestry" \
  "Group photo poses with a countdown and callouts. /tapestry" SavedVariables
# EcoWar is TABLED (founder, 2026-11-13: "skip ecowar for now its busted table it"). Its code and its test stay;
# it is only packaged when COLLECTION_WITH_TABLED=1 (the test build sets it), never in a release or an install.
if [ "${COLLECTION_WITH_TABLED:-0}" = 1 ]; then
game ECOWAR EcoWar EcoWarDB ecowar ecowar.lua "EcoWar" \
  "Team cozy-farming wars: critters and loot, four team modes, a league and ratings. /ecowar" SavedVariables
fi
game NAGLFAR NaglfarCrew NaglfarCrewDB naglfar naglfar_crew.lua "Naglfar Crew" \
  "Sardines: find the hider and squeeze in; last one out loses. /sardines" SavedVariables

# Suite fragments (BOARD.md plug-in slots), in sorted order; see the header. A fragment that fails stops the build.
FRAG_DIR="${COLLECTION_FRAGMENTS_DIR:-scripts/collection.d}" FRAGS="" frags=() LEFTOVERS=()
if [ -d "$FRAG_DIR" ]; then
  while IFS= read -r frag; do frags+=("$FRAG_DIR/$frag"); done < <(ls -1 "$FRAG_DIR" | grep '\.sh$' | LC_ALL=C sort)
fi
for frag in ${frags[@]+"${frags[@]}"}; do
  # shellcheck source=/dev/null
  . "$frag"
  FRAGS="$FRAGS ${frag##*/}"
done

if [ -n "$ONLY" ]; then
  for n in $ONLY; do
    case "$SEEN" in *" $n "*) ;; *) die "COLLECTION_ONLY names '$n', but no block or fragment builds that addon folder (tabled ones need COLLECTION_WITH_TABLED=1)" ;; esac
  done
  # An inline block may have made a folder, or copied its catalogue, before its helpers skipped the addon.
  for d in "$OUT"/AddOns/*/; do
    d="${d%/}"
    case "$SELECTED" in *" ${d##*/} "*) ;; *) LEFTOVERS+=("$d") ;; esac
  done
fi
rm -rf "$WRAP_TMP" ${LEFTOVERS[@]+"${LEFTOVERS[@]}"}

# Re-stamp RefluxCore's version with the release's, so all three agree.
sed -i "s/^## Version: .*/## Version: $VERSION/" "$OUT/AddOns/RefluxCore/RefluxCore.toc"

"$PARENA_BIN" build stdlib/collection/ids.prn -o "$OUT/reader/ids.lua" >/dev/null
cp runtime/wow/journal_reader.lua runtime/wow/journal_read_cli.lua runtime/wow/csv_export.lua "$OUT/reader/"
cp scripts/collection-csv.sh "$OUT/reader/export-csv.sh"
chmod +x "$OUT/reader/export-csv.sh"
cp scripts/collection-journal-read.sh "$OUT/reader/read-journal.sh"
chmod +x "$OUT/reader/read-journal.sh"

{
cat <<EOF
Collection of Value -- client $VERSION (WoW interface $WOW_INTERFACE), built by PARENA.
EOF
[ -z "$ONLY" ] || echo "PARTIAL DEV BUILD, not a release (COLLECTION_ONLY=$COLLECTION_ONLY): only ${BUILT// /, } and the reader."
cat <<EOF

INSTALL
  Copy every folder in AddOns/ into your client's Interface/AddOns folder
  (WoW Forever: <WoW>/_forever_/Interface/AddOns), then restart the client or /reload.

IN GAME
  /fafnir            net inventory: characters, distinct items, item count, gold across the account
  /fafnir <name|id>  who has it, and the net count (shift-click an item link in works too)
  /cov               the journal's state (rows held, trimmed, lost)
  /rock              Hrungnir, your pet rock (/rock pat, /rock hide, /rock show; right-click the rock to pat it)
  /census            Ratatoskr Roll: this week's most loved critters (target one, then /love, /pet or /wave)
  /fortune          Völva: today's fortune again (a new one opens at every login)
  /saga              Saga of Fools: joke achievements, earned and still to come
  /ascend            MacroSync: load your class's macros (paladin, priest, mage, shaman, druid); /ascend preview changes nothing
EOF
printf '%s' "$README_GAME"
cat <<EOF
  Each character is learned the first time you log it in; the bank is learned each time you open it,
  mail each time you open the mailbox.

AFTER PLAYING (log out first: the client writes SavedVariables on logout and /reload)
  WOW_WTF_DIR="<WoW>/_forever_/WTF" COLLECTION_EVENTS=<somewhere>/events.ndjson reader/read-journal.sh
  Needs a Lua 5.1 interpreter (LUA51=lua5.1). Appends new events only; exit 3 means rows were trimmed in
  game before a read reached them -- the report names them.
  WOW_WTF_DIR="<WoW>/_forever_/WTF" COLLECTION_CSV_DIR=<somewhere>/csv reader/export-csv.sh
  writes characters.csv (every snapshot: your progression), bags.csv, bags_history.csv (appended each run)
  and inventory_net.csv. /board in game shows the characters; /board snap takes a snapshot now.
EOF
printf '%s' "$README_AFTER"
} > "$OUT/README.txt"
echo "$VERSION" > "$OUT/VERSION"

echo "wow-package-collection: built $OUT ($VERSION, interface $WOW_INTERFACE): ${BUILT// /, }, reader${FRAGS:+; fragments:$FRAGS}${ONLY:+; partial (COLLECTION_ONLY=$COLLECTION_ONLY)}"

for a in "$@"; do
  case "$a" in
    --zip)
      command -v zip >/dev/null 2>&1 || die "--zip needs zip"
      zipname="CollectionOfValue-$VERSION.zip"
      (cd "$OUT" && rm -f "../$zipname" && zip -qr "../$zipname" AddOns reader README.txt VERSION)
      echo "wow-package-collection: $(dirname "$OUT")/$zipname" ;;
    --install)
      [ -n "${WOW_ADDONS_DIR:-}" ] || die "--install needs WOW_ADDONS_DIR"
      for d in "$OUT"/AddOns/*/; do
        n="$(basename "$d")"
        rm -rf "${WOW_ADDONS_DIR:?}/$n"
        cp -r "$d" "$WOW_ADDONS_DIR/$n"
      done
      echo "wow-package-collection: installed to $WOW_ADDONS_DIR" ;;
    *) die "unknown option $a" ;;
  esac
done
