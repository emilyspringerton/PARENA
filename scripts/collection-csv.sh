#!/usr/bin/env bash
# scripts/collection-csv.sh -- export the account's character progression and bags to CSV (runtime/wow/csv_export.lua
# says what each file holds). Run it after logging out: the client writes SavedVariables on logout and /reload.
#
# Environment (see MONOREPO/scripts/collection-env.sh):
#   WOW_WTF_DIR          REQUIRED. The client's WTF folder
#   COLLECTION_CSV_DIR   REQUIRED. Where the CSV files go (outside every repository; made if missing)
#   BOARD_ADDON / BOARD_VAR    the Character Board's folder / global   (CharacterBoard / CharacterBoardDB)
#   FAFNIR_ADDON / FAFNIR_VAR  Fafnir's Bed's folder / global          (FafnirsBed / FafnirsBedDB)
#   LUA51                a Lua 5.1 interpreter (default lua5.1)
# Exit codes: 0 done; 1 a file was skipped (said on stderr); 2 setup error.
# usage: scripts/collection-csv.sh
set -uo pipefail
abs() { case "$1" in /*|?:/*|?:\\*) printf '%s' "$1" ;; *) printf '%s/%s' "$PWD" "$1" ;; esac; }
[ -n "${WOW_WTF_DIR:-}" ] && WOW_WTF_DIR="$(abs "$WOW_WTF_DIR")"
[ -n "${COLLECTION_CSV_DIR:-}" ] && COLLECTION_CSV_DIR="$(abs "$COLLECTION_CSV_DIR")"
here="$(cd "$(dirname "$0")" && pwd)"
if [ -f "$here/csv_export.lua" ]; then lib="$here"; else lib="$here/../runtime/wow"; fi
LUA51="${LUA51:-lua5.1}"
BOARD_ADDON="${BOARD_ADDON:-CharacterBoard}"
BOARD_VAR="${BOARD_VAR:-CharacterBoardDB}"
FAFNIR_ADDON="${FAFNIR_ADDON:-FafnirsBed}"
FAFNIR_VAR="${FAFNIR_VAR:-FafnirsBedDB}"
die() { echo "collection-csv: $*" >&2; exit 2; }
[ -n "${WOW_WTF_DIR:-}" ] || die "WOW_WTF_DIR is not set (source MONOREPO/scripts/collection-env.sh)"
[ -n "${COLLECTION_CSV_DIR:-}" ] || die "COLLECTION_CSV_DIR is not set (source MONOREPO/scripts/collection-env.sh)"
[ -d "$WOW_WTF_DIR/Account" ] || die "no Account folder under WOW_WTF_DIR=$WOW_WTF_DIR"
command -v "$LUA51" >/dev/null 2>&1 || die "no Lua 5.1 at LUA51=$LUA51"
mkdir -p "$COLLECTION_CSV_DIR" || die "cannot make $COLLECTION_CSV_DIR"

board=() fafnir=()
for f in "$WOW_WTF_DIR"/Account/*/SavedVariables/"$BOARD_ADDON".lua; do [ -f "$f" ] && board+=("$f"); done
for f in "$WOW_WTF_DIR"/Account/*/SavedVariables/"$FAFNIR_ADDON".lua; do [ -f "$f" ] && fafnir+=("$f"); done
[ ${#board[@]} -gt 0 ] || echo "collection-csv: no $BOARD_ADDON.lua yet -- log in and out once with the Character Board installed" >&2
exec "$LUA51" "$lib/csv_export.lua" "$lib/journal_reader.lua" "$COLLECTION_CSV_DIR" "$BOARD_VAR" "$FAFNIR_VAR" \
  ${board[@]+"${board[@]}"} -- ${fafnir[@]+"${fafnir[@]}"}
