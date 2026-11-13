#!/usr/bin/env bash
# scripts/collection-journal-read.sh -- the external SavedVariables reader (Collection of Value, spec Step 6,
# COV-6b). Finds every character's journal file under the client's WTF folder and appends the rows not yet
# stored to the event file, as PRRJECT_FATBABY eventstore Events (runtime/wow/journal_reader.lua says how).
# Run it after logging out (the client writes SavedVariables on logout and /reload, never while playing).
#
# Environment (all overridable; see MONOREPO/scripts/collection-env.sh):
#   WOW_WTF_DIR               REQUIRED. The client's WTF folder (holds Account/<account>/<realm>/<character>/)
#   COLLECTION_EVENTS         REQUIRED. The NDJSON event file to append to (made if missing)
#   COLLECTION_JOURNAL_ADDON  the journal addon's folder name        (default CollectionJournal)
#   COLLECTION_JOURNAL_VAR    its SavedVariablesPerCharacter global  (default CollectionJournalDB)
#   LUA51                     a Lua 5.1 interpreter                  (default lua5.1)
#   PARENA_BIN                compiler driver, to build ids.prn       (default ./.parena-bootstrap)
#
# Exit codes: 0 all read; 1 a file was skipped (said on stderr); 2 setup error; 3 read, but a GAP was found.
# usage: scripts/collection-journal-read.sh
set -uo pipefail

# Paths given relative to the caller's folder stay meaning that folder after the cd below.
abs() { case "$1" in /*|?:/*|?:\\*) printf '%s' "$1" ;; *) printf '%s/%s' "$PWD" "$1" ;; esac; }
[ -n "${WOW_WTF_DIR:-}" ] && WOW_WTF_DIR="$(abs "$WOW_WTF_DIR")"
[ -n "${COLLECTION_EVENTS:-}" ] && COLLECTION_EVENTS="$(abs "$COLLECTION_EVENTS")"
here="$(cd "$(dirname "$0")" && pwd)"
# Two layouts: the release zip's reader/ folder (journal_reader.lua and a prebuilt ids.lua beside this script,
# no compiler needed) or a PARENA checkout (scripts/ -> build ids.lua from stdlib/collection/ids.prn).
if [ -f "$here/journal_reader.lua" ]; then
  reader_dir="$here"
else
  cd "$here/.."
  reader_dir="runtime/wow"
fi
PARENA_BIN="${PARENA_BIN:-./.parena-bootstrap}"
LUA51="${LUA51:-lua5.1}"
ADDON="${COLLECTION_JOURNAL_ADDON:-CollectionJournal}"
VAR="${COLLECTION_JOURNAL_VAR:-CollectionJournalDB}"

die() { echo "collection-journal-read: $*" >&2; exit 2; }
[ -n "${WOW_WTF_DIR:-}" ] || die "WOW_WTF_DIR is not set (source MONOREPO/scripts/collection-env.sh)"
[ -n "${COLLECTION_EVENTS:-}" ] || die "COLLECTION_EVENTS is not set (source MONOREPO/scripts/collection-env.sh)"
[ -d "$WOW_WTF_DIR/Account" ] || die "no Account folder under WOW_WTF_DIR=$WOW_WTF_DIR"
command -v "$LUA51" >/dev/null 2>&1 || die "no Lua 5.1 at LUA51=$LUA51"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
if [ -f "$reader_dir/ids.lua" ]; then
  cp "$reader_dir/ids.lua" "$tmp/ids.lua"
else
  [ -x "$PARENA_BIN" ] || die "no compiler at $PARENA_BIN (make .parena-bootstrap)"
  "$PARENA_BIN" build stdlib/collection/ids.prn -o "$tmp/ids.lua" >/dev/null || die "could not build stdlib/collection/ids.prn"
fi

# Account/<account>/<realm>/<character>/SavedVariables/<Addon>.lua -- the .lua.bak next to each is the
# client's previous copy and is never read (the glob ends in .lua).
args=()
shopt -s nullglob
for sv in "$WOW_WTF_DIR"/Account/*/*/*/SavedVariables/"$ADDON".lua; do
  rel="${sv#"$WOW_WTF_DIR"/Account/}"
  args+=("$sv" "${rel%%/*}")
done
if [ "${#args[@]}" -eq 0 ]; then
  echo "collection-journal-read: no $ADDON.lua under $WOW_WTF_DIR/Account/*/*/*/SavedVariables -- nothing to read"
  exit 0
fi

mkdir -p "$(dirname "$COLLECTION_EVENTS")"
"$LUA51" "$reader_dir/journal_read_cli.lua" "$reader_dir/journal_reader.lua" "$tmp/ids.lua" "$VAR" "$COLLECTION_EVENTS" "${args[@]}"
