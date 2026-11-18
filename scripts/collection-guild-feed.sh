#!/usr/bin/env bash
# scripts/collection-guild-feed.sh -- GuildHerald's data out of WoW (run after logging out; the client writes
# SavedVariables on logout and /reload):
#   COLLECTION_GUILD_FEED  the Collection feed (REFLUX rows + names) SHANKPIT's /ascend reads (rewritten each run)
#   COLLECTION_GUILDS_LOG  an NDJSON log stream: every guild MOTD / info text, appended once each (state beside it)
# Both hold community and play data, so keep them outside every repository (Disputes D-8).
#
# Environment:
#   WOW_WTF_DIR             REQUIRED. The client's WTF folder (WoW Forever: <WoW>/_forever_/WTF).
#   COLLECTION_GUILD_FEED   REQUIRED. The feed file to write (SHANKPIT reads the same path).
#   COLLECTION_GUILDS_LOG   REQUIRED. The NDJSON log to append to.
#   GUILDHERALD_ADDON, GUILDHERALD_VAR   folder / global (default GuildHerald / GuildHeraldDB)
#   PARENA_BIN, LUA51                    compiler driver (default ./.parena-bootstrap), Lua 5.1 (default lua5.1)
#
# usage: scripts/collection-guild-feed.sh
set -euo pipefail
cd "$(dirname "$0")/.."
die() { echo "collection-guild-feed: $*" >&2; exit 2; }

[ -n "${WOW_WTF_DIR:-}" ] || die "WOW_WTF_DIR is not set (the client's WTF folder)"
[ -d "$WOW_WTF_DIR/Account" ] || die "$WOW_WTF_DIR has no Account/ folder"
[ -n "${COLLECTION_GUILD_FEED:-}" ] || die "COLLECTION_GUILD_FEED is not set (keep it outside every repo)"
[ -n "${COLLECTION_GUILDS_LOG:-}" ] || die "COLLECTION_GUILDS_LOG is not set (keep it outside every repo)"
PARENA_BIN="${PARENA_BIN:-./.parena-bootstrap}"
LUA51="${LUA51:-lua5.1}"
ADDON="${GUILDHERALD_ADDON:-GuildHerald}"
VAR="${GUILDHERALD_VAR:-GuildHeraldDB}"
[ -x "$PARENA_BIN" ] || die "no compiler at $PARENA_BIN (make .parena-bootstrap)"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
"$PARENA_BIN" build stdlib/reflux/reflux.prn stdlib/collection/guildherald.prn -o "$work/gh.lua" >/dev/null

files=()
for f in "$WOW_WTF_DIR"/Account/*/SavedVariables/"$ADDON".lua; do
  [ -f "$f" ] && files+=("$f")
done
[ "${#files[@]}" -gt 0 ] || echo "collection-guild-feed: no $ADDON.lua under $WOW_WTF_DIR/Account yet (log out of WoW once with the addon on)" >&2
mkdir -p "$(dirname "$COLLECTION_GUILD_FEED")" "$(dirname "$COLLECTION_GUILDS_LOG")"
"$LUA51" runtime/wow/guild_feed_read.lua "$work/gh.lua" "$VAR" "$COLLECTION_GUILD_FEED" "$COLLECTION_GUILDS_LOG" "${files[@]}"
