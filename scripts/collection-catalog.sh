#!/usr/bin/env bash
# scripts/collection-catalog.sh -- build the plugin store's catalog from a packaged client (docs/PLUGIN_STORE_NORTHSTAR.md).
#
# Writes, under COLLECTION_CATALOG_OUT:
#   catalog.json            every plugin in catalog/plugins.tsv: channel, badge, title, version, depends, download
#   <Id>.zip                one zip per downloadable WoW addon, holding its <Id>/ folder, beside catalog.json
#                           (with --zips; needs zip). A catalog's "download" is relative to the catalog's own URL.
#
# Environment:
#   COLLECTION_CLIENT_OUT   REQUIRED. A client built by scripts/wow-package-collection.sh (it reads AddOns/*/*.toc).
#   COLLECTION_CATALOG_BUILD REQUIRED. A positive integer that grows with every release (CI passes its run number);
#                           a surface offers Update when a player's installed build is lower. Never defaulted.
#   WOW_INTERFACE           REQUIRED. The TOC interface number the client was built for.
#   COLLECTION_CATALOG_OUT  where to write (default $COLLECTION_CLIENT_OUT/../catalog)
#   COLLECTION_CATALOG_TSV  the manifest (default catalog/plugins.tsv)
#   PARENA_BIN, LUA51       compiler driver (default ./.parena-bootstrap), Lua 5.1 (default lua5.1)
#
# usage: scripts/collection-catalog.sh [--zips]
set -euo pipefail

cd "$(dirname "$0")/.."
die() { echo "collection-catalog: $*" >&2; exit 2; }

PARENA_BIN="${PARENA_BIN:-./.parena-bootstrap}"
LUA51="${LUA51:-lua5.1}"
TSV="${COLLECTION_CATALOG_TSV:-catalog/plugins.tsv}"
[ -n "${COLLECTION_CLIENT_OUT:-}" ] || die "COLLECTION_CLIENT_OUT is not set (build a client with scripts/wow-package-collection.sh first)"
[ -d "$COLLECTION_CLIENT_OUT/AddOns" ] || die "$COLLECTION_CLIENT_OUT has no AddOns/"
[ -n "${COLLECTION_CATALOG_BUILD:-}" ] || die "COLLECTION_CATALOG_BUILD is not set (a positive integer that grows each release)"
[ -n "${WOW_INTERFACE:-}" ] || die "WOW_INTERFACE is not set"
[ -x "$PARENA_BIN" ] || die "no compiler at $PARENA_BIN (make .parena-bootstrap)"
OUT="${COLLECTION_CATALOG_OUT:-$COLLECTION_CLIENT_OUT/../catalog}"

zips=0
for a in "$@"; do
  case "$a" in
    --zips) zips=1 ;;
    *) die "unknown option $a" ;;
  esac
done
if [ "$zips" = 1 ]; then command -v zip >/dev/null 2>&1 || die "--zips needs zip"; fi

version="$(cat "$COLLECTION_CLIENT_OUT/VERSION" 2>/dev/null || echo dev)"
mkdir -p "$OUT"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

"$PARENA_BIN" build stdlib/collection/catalog.prn -o "$work/catalog.lua" >/dev/null
"$LUA51" runtime/wow/catalog_gen.lua "$work/catalog.lua" "$TSV" "$COLLECTION_CLIENT_OUT" \
  "$COLLECTION_CATALOG_BUILD" "$version" "$WOW_INTERFACE" > "$work/catalog.json"
mv "$work/catalog.json" "$OUT/catalog.json"

if [ "$zips" = 1 ]; then
  abs_out="$(cd "$OUT" && pwd)"
  # Every "download" in the catalog names exactly one of these.
  grep -o '"download":"[A-Za-z0-9_-]*\.zip"' "$OUT/catalog.json" | sed 's/"download":"\(.*\)\.zip"/\1/' |
  while read -r id; do
    (cd "$COLLECTION_CLIENT_OUT/AddOns" && rm -f "$abs_out/$id.zip" && zip -qr "$abs_out/$id.zip" "$id")
  done
fi

n="$(grep -c '^{"id"' "$OUT/catalog.json" || true)"
echo "collection-catalog: $OUT/catalog.json ($n plugins, build $COLLECTION_CATALOG_BUILD, $version)"
