#!/usr/bin/env bash
# scripts/collection-store-ingest.sh -- append the SavedVariables reader's events to the Collection's event store
# (spec Step 7, stdlib/collection/eventstore.prn). The store is on-disk compatible with PRRJECT_FATBABY's Go
# eventstore.FileStore, so analytics, the verifiers and Jon's reads can open it with the Go code as it is.
# Idempotent: events whose id is already stored are skipped, so it can run after every play session.
#
# Environment (all overridable; see MONOREPO/scripts/collection-env.sh):
#   COLLECTION_EVENTS   REQUIRED. The reader's NDJSON (scripts/collection-journal-read.sh writes it)
#   COLLECTION_STORE    REQUIRED. The store's root folder (holds events/ and state/; made if missing)
#   LUA51               a Lua 5.1 interpreter                    (default lua5.1)
#   PARENA_BIN          compiler driver, to build eventstore.prn  (default ./.parena-bootstrap)
#
# The append lock: Go's FileStore takes flock(2) on state/append.lock through PARENA's seqlock mod. Lua has no
# flock, so this takes state/append.lock.d (mkdir is atomic) instead. That excludes other ingests, NOT a Go
# process appending at the same moment -- the Collection's store is written only by this ingest; Go reads it.
#
# Exit codes: 0 ok; 1 the ingest refused (said on stderr); 2 setup error; 4 another ingest holds the lock.
# usage: scripts/collection-store-ingest.sh
set -uo pipefail

abs() { case "$1" in /*|?:/*|?:\\*) printf '%s' "$1" ;; *) printf '%s/%s' "$PWD" "$1" ;; esac; }
die() { echo "collection-store-ingest: $*" >&2; exit 2; }
[ -n "${COLLECTION_EVENTS:-}" ] || die "COLLECTION_EVENTS is not set (source MONOREPO/scripts/collection-env.sh)"
[ -n "${COLLECTION_STORE:-}" ] || die "COLLECTION_STORE is not set (source MONOREPO/scripts/collection-env.sh)"
COLLECTION_EVENTS="$(abs "$COLLECTION_EVENTS")"
COLLECTION_STORE="$(abs "$COLLECTION_STORE")"
cd "$(dirname "$0")/.."
PARENA_BIN="${PARENA_BIN:-./.parena-bootstrap}"
LUA51="${LUA51:-lua5.1}"
command -v "$LUA51" >/dev/null 2>&1 || die "no Lua 5.1 at LUA51=$LUA51"
[ -x "$PARENA_BIN" ] || die "no compiler at $PARENA_BIN (make .parena-bootstrap)"
[ -f "$COLLECTION_EVENTS" ] || { echo "collection-store-ingest: no events file at $COLLECTION_EVENTS -- nothing to ingest"; exit 0; }

mkdir -p "$COLLECTION_STORE/events" "$COLLECTION_STORE/state" || die "cannot create $COLLECTION_STORE"
lock="$COLLECTION_STORE/state/append.lock.d"
if ! mkdir "$lock" 2>/dev/null; then
  echo "collection-store-ingest: another ingest holds $lock (remove that folder if none is running)" >&2
  exit 4
fi
tmp="$(mktemp -d)"
trap 'rmdir "$lock" 2>/dev/null; rm -rf "$tmp"' EXIT

"$PARENA_BIN" build stdlib/collection/eventstore.prn -o "$tmp/es.lua" >/dev/null || die "could not build stdlib/collection/eventstore.prn"

journals=()
shopt -s nullglob
for j in "$COLLECTION_STORE"/events/*.ndjson; do journals+=("$j"); done   # glob order is sorted, as journalPaths
"$LUA51" runtime/collection/store_ingest.lua "$tmp/es.lua" "$COLLECTION_STORE" "$COLLECTION_EVENTS" "${journals[@]}"
