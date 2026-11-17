#!/usr/bin/env bash
# hoard.sh -- every character's gear, bags, bank and mail, served from this machine (no login, no public host).
#
#   bash scripts/hoard.sh                 serve the page on HOARD_ADDR (default http://127.0.0.1:8765/)
#   bash scripts/hoard.sh -csv FILE       write the CSV to FILE ("-" for stdout) and exit
#
# Reads WOW_WTF_DIR (and FAFNIR_ADDON / FAFNIR_VAR) from the environment; when MONOREPO's
# scripts/collection-env.sh sits beside this repo it is sourced first, so nothing here names a path.
# Builds tools/hoard with Go (1.19+) into COLLECTION_TMP, outside the repo.
set -euo pipefail
parena="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ -z "${WOW_WTF_DIR:-}" ]; then
    for env in "${COLLECTION_MONOREPO:-}/scripts/collection-env.sh" "$parena/../scripts/collection-env.sh"; do
        if [ -f "$env" ]; then . "$env"; break; fi
    done
fi
if [ -z "${WOW_WTF_DIR:-}" ]; then
    echo "hoard.sh: WOW_WTF_DIR is not set (the WoW client's WTF folder); export it or source collection-env.sh" >&2
    exit 2
fi
out="${COLLECTION_TMP:?COLLECTION_TMP is not set (collection-env.sh sets it)}/hoard"
mkdir -p "$out"
(cd "$parena/tools/hoard" && go build -buildvcs=false -o "$out/hoard.exe" .)
exec "$out/hoard.exe" "$@"
