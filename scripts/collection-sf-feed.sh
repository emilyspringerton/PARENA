#!/usr/bin/env bash
# scripts/collection-sf-feed.sh -- write the Collection feed (REFLUX rows) from Self Found's SavedVariables, for
# SHANKPIT's Collection overlay (stream overlay, terminal, APPS page). Run it after logging out of WoW (the client
# writes SavedVariables on logout and /reload). docs/SELF_FOUND.md, "The feed".
#
# Environment:
#   WOW_WTF_DIR       REQUIRED. The client's WTF folder (WoW Forever: <WoW>/_forever_/WTF).
#   COLLECTION_FEED   REQUIRED. The feed file to write; SHANKPIT reads the same path from its own COLLECTION_FEED.
#                     It holds play data, so keep it outside every repository (Disputes D-8).
#   SELFFOUND_ADDON, SELFFOUND_VAR   the addon folder / global (default SelfFound / SelfFoundDB)
#   PARENA_BIN, LUA51                compiler driver (default ./.parena-bootstrap), Lua 5.1 (default lua5.1)
#
# usage: scripts/collection-sf-feed.sh
set -euo pipefail
cd "$(dirname "$0")/.."
die() { echo "collection-sf-feed: $*" >&2; exit 2; }

[ -n "${WOW_WTF_DIR:-}" ] || die "WOW_WTF_DIR is not set (the client's WTF folder)"
[ -d "$WOW_WTF_DIR/Account" ] || die "$WOW_WTF_DIR has no Account/ folder"
[ -n "${COLLECTION_FEED:-}" ] || die "COLLECTION_FEED is not set (where to write the feed; keep it outside every repo)"
PARENA_BIN="${PARENA_BIN:-./.parena-bootstrap}"
LUA51="${LUA51:-lua5.1}"
ADDON="${SELFFOUND_ADDON:-SelfFound}"
VAR="${SELFFOUND_VAR:-SelfFoundDB}"
[ -x "$PARENA_BIN" ] || die "no compiler at $PARENA_BIN (make .parena-bootstrap)"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
"$PARENA_BIN" build stdlib/reflux/reflux.prn stdlib/collection/selffound.prn -o "$work/sf.lua" >/dev/null

# One account-wide file per account: WTF/Account/<account>/SavedVariables/<ADDON>.lua (never the .bak).
files=()
for f in "$WOW_WTF_DIR"/Account/*/SavedVariables/"$ADDON".lua; do
  [ -f "$f" ] && files+=("$f")
done
[ "${#files[@]}" -gt 0 ] || echo "collection-sf-feed: no $ADDON.lua under $WOW_WTF_DIR/Account yet (log out of WoW once with the addon on); writing an empty feed" >&2
mkdir -p "$(dirname "$COLLECTION_FEED")"
"$LUA51" runtime/wow/sf_feed_read.lua "$work/sf.lua" "$VAR" "$COLLECTION_FEED" "${files[@]}"
