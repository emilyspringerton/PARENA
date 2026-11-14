#!/usr/bin/env bash
# scripts/forge.sh -- the plugin forge: see every character (level, gold, gear, bags) and plan its macro kit, then bake
# the plans into the addon package. A command line (runtime/wow/forge/forge.lua) and a local web page over the same
# commands (runtime/wow/forge/forge_server.c). `scripts/forge.sh help` lists the commands.
#
#   scripts/forge.sh characters                 everyone on the account
#   scripts/forge.sh show emily                 one character's level, gold, gear, bags, bank, mail
#   scripts/forge.sh kit emily                  her planned macro kit
#   scripts/forge.sh drop emily PalExo          ...plan it
#   scripts/forge.sh build                      bake the plans into the addon package (then copy AddOns/ into the game)
#   scripts/forge.sh serve [PORT]               the same on a page at http://127.0.0.1:PORT (default 7744)
#   scripts/forge.sh ledger [FILE]              the whole account -- every item worn/bags/bank/mail, level, class, race,
#                                               gold, where -- in ONE spreadsheet (default $FORGE_DIR/ledger.csv)
#   scripts/forge.sh watch [FILE]               keep that file current: rewrite it each time WoW saves (logout, /reload)
#
# Environment (see MONOREPO/scripts/collection-env.sh):
#   FORGE_DIR         REQUIRED. Where the plans live (kits.lua), outside every repository; made if missing
#   WOW_WTF_DIR       the client's WTF folder: where the characters come from. Unset: only planned characters show
#   WOW_INTERFACE     needed by `build` (scripts/wow-package-collection.sh), as for every package
#   BOARD_ADDON / BOARD_VAR, FAFNIR_ADDON / FAFNIR_VAR, MACROSYNC_ADDON / MACROSYNC_VAR   addon folders / globals
#   PARENA_BIN        the compiler (default: ./.parena-bootstrap, built with `make .parena-bootstrap` if missing)
#   LUA51             a Lua 5.1 interpreter (default lua5.1)
#   CC                a C compiler for the page's server (default cc; on MSYS2 UCRT64 gcc)
# Exit codes: forge.lua's (0 done, 1 refused, 2 usage), 2 for setup errors here.
set -uo pipefail
abs() { case "$1" in /*|?:/*|?:\\*) printf '%s' "$1" ;; *) printf '%s/%s' "$PWD" "$1" ;; esac; }
die() { echo "forge: $*" >&2; exit 2; }
[ -n "${FORGE_DIR:-}" ] || die "FORGE_DIR is not set: where the forge keeps its plans (outside every repository)"
FORGE_DIR="$(abs "$FORGE_DIR")"
[ -n "${WOW_WTF_DIR:-}" ] && WOW_WTF_DIR="$(abs "$WOW_WTF_DIR")"
root="$(cd "$(dirname "$0")/.." && pwd)"
# a FILE given to ledger/watch is relative to where you ran this, not to the repository it moves into
if { [ "${1:-}" = ledger ] || [ "${1:-}" = watch ]; } && [ -n "${2:-}" ] && [ "${2:-}" != "-" ]; then
  set -- "$1" "$(abs "$2")" "${@:3}"
fi
cd "$root" || die "cannot enter $root"
LUA51="${LUA51:-lua5.1}"
command -v "$LUA51" >/dev/null 2>&1 || die "no Lua 5.1 at LUA51=$LUA51"
mkdir -p "$FORGE_DIR/lib" || die "cannot make $FORGE_DIR"

# The PARENA modules the forge runs: compiled once, again whenever their .prn changes.
PARENA_BIN="${PARENA_BIN:-./.parena-bootstrap}"
if [ "$PARENA_BIN" = "./.parena-bootstrap" ] && [ ! -x ./.parena-bootstrap ]; then
  make -s .parena-bootstrap >/dev/null || die "could not build the PARENA compiler (make .parena-bootstrap)"
fi
for m in kits macros; do
  if [ ! -f "$FORGE_DIR/lib/$m.lua" ] || [ "stdlib/collection/$m.prn" -nt "$FORGE_DIR/lib/$m.lua" ]; then
    "$PARENA_BIN" build "stdlib/collection/$m.prn" -o "$FORGE_DIR/lib/$m.lua" >/dev/null || die "could not compile stdlib/collection/$m.prn"
  fi
done
cp runtime/wow/journal_reader.lua "$FORGE_DIR/lib/journal_reader.lua"

# Where the characters come from: the Board and Fafnir (account-wide), MacroSync (per character: <realm>/<character>).
files=()
if [ -n "${WOW_WTF_DIR:-}" ]; then
  [ -d "$WOW_WTF_DIR/Account" ] || die "no Account folder under WOW_WTF_DIR=$WOW_WTF_DIR"
  for f in "$WOW_WTF_DIR"/Account/*/SavedVariables/"${BOARD_ADDON:-CharacterBoard}".lua; do [ -f "$f" ] && files+=(--board "$f"); done
  for f in "$WOW_WTF_DIR"/Account/*/SavedVariables/"${FAFNIR_ADDON:-FafnirsBed}".lua; do [ -f "$f" ] && files+=(--fafnir "$f"); done
  for f in "$WOW_WTF_DIR"/Account/*/*/*/SavedVariables/"${MACROSYNC_ADDON:-MacroSync}".lua; do
    [ -f "$f" ] || continue
    chardir="$(dirname "$(dirname "$f")")"
    files+=(--ms "$f" "$(basename "$chardir")-$(basename "$(dirname "$chardir")")")
  done
fi
forge() { "$LUA51" runtime/wow/forge/forge.lua --lib "$FORGE_DIR/lib" --dir "$FORGE_DIR" ${files[@]+"${files[@]}"} "$@"; }

case "${1:-}" in
  build)
    shift
    forge export > "$FORGE_DIR/macrosync_kits.lua" || exit $?
    echo "forge: baking $(grep -c '^  \["' "$FORGE_DIR/macrosync_kits.lua") character kit(s) into MacroSync"
    MACROSYNC_KITS="$FORGE_DIR/macrosync_kits.lua" bash scripts/wow-package-collection.sh "$@"
    ;;
  serve)
    port="${2:-7744}"
    case "$port" in *[!0-9]*|"") die "the port is a number" ;; esac
    srv="$FORGE_DIR/forge_server"
    if [ ! -x "$srv" ] && [ ! -x "$srv.exe" ] || [ runtime/wow/forge/forge_server.c -nt "$srv" ]; then
      extra=""; case "$(uname -s 2>/dev/null)" in MINGW*|MSYS*|CYGWIN*) extra="-lws2_32" ;; esac
      "${CC:-cc}" -std=c99 -O2 -Wall -Wextra -o "$srv" runtime/wow/forge/forge_server.c $extra || die "could not build the forge's server"
    fi
    # the server is a native program: on MSYS2 it needs Windows paths (D:/...), not /d/...
    sdir="$FORGE_DIR"; command -v cygpath >/dev/null 2>&1 && sdir="$(cygpath -m "$FORGE_DIR")"
    echo "forge: http://127.0.0.1:$port/  (Ctrl-C to stop)"
    exec "$srv" "$port" runtime/wow/forge/forge.html "$sdir" bash "$root/scripts/forge.sh"
    ;;
  watch)
    # WoW writes SavedVariables only at logout, /reload or exit -- so that is when the ledger can change. Poll the files'
    # times and sizes; rewrite once they have stopped changing for one poll (WoW writes several files in a row).
    [ -n "${WOW_WTF_DIR:-}" ] || die "watch needs WOW_WTF_DIR (the client's WTF folder)"
    ledger="${2:-$FORGE_DIR/ledger.csv}"
    every="${FORGE_WATCH_SECONDS:-3}"
    echo "forge: keeping $ledger current -- it updates when you log out or /reload  (Ctrl-C to stop)"
    done_sig="" prev=""
    while :; do
      sig="$(stat -c '%n %Y %s' "$WOW_WTF_DIR"/Account/*/SavedVariables/{"${BOARD_ADDON:-CharacterBoard}","${FAFNIR_ADDON:-FafnirsBed}"}.lua 2>/dev/null)"
      if [ "$sig" != "$done_sig" ] && [ "$sig" = "$prev" ]; then
        echo "$(date '+%H:%M:%S') $(bash "$0" ledger "$ledger" 2>&1)"
        done_sig="$sig"
      fi
      prev="$sig"
      [ -n "${FORGE_WATCH_ONCE:-}" ] && [ -n "$done_sig" ] && exit 0
      sleep "$every"
    done
    ;;
  *)
    forge "$@"
    ;;
esac
