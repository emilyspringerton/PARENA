#!/usr/bin/env bash
# hoard.sh -- every character's gear, bags, bank and mail, served from this machine (no login, no public host).
#
#   bash scripts/hoard.sh                 serve the page on http://127.0.0.1:${HOARD_PORT:-8765}/
#   bash scripts/hoard.sh -csv FILE       write the CSV to FILE ("-" for stdout) and exit
#
# All PARENA: the ledger (stdlib/collection/ledger.prn) is built to Lua and run under Lua 5.1 with the journal
# reader's sandboxed loader; the server (tools/hoard/hoard.prn) is built to C with the bootstrap compiler and gcc.
# Both land in $COLLECTION_TMP/hoard, outside the repo.
#
# Reads only env (MONOREPO/scripts/collection-env.sh, sourced here when it sits beside this repo and WOW_WTF_DIR is
# unset): WOW_WTF_DIR (required), COLLECTION_TMP, LUA51, PARENA_BIN, FAFNIR_ADDON, FAFNIR_VAR, HOARD_PORT.
set -euo pipefail
parena="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ -z "${WOW_WTF_DIR:-}" ]; then
    for env in "${COLLECTION_MONOREPO:-}/scripts/collection-env.sh" "$parena/../scripts/collection-env.sh"; do
        if [ -f "$env" ]; then . "$env"; break; fi
    done
fi
die() { echo "hoard.sh: $*" >&2; exit 2; }
[ -n "${WOW_WTF_DIR:-}" ] || die "WOW_WTF_DIR is not set (the WoW client's WTF folder); export it or source collection-env.sh"
[ -n "${COLLECTION_TMP:-}" ] || die "COLLECTION_TMP is not set (collection-env.sh sets it)"
lua="$(command -v "${LUA51:-lua5.1}" 2>/dev/null)" || die "no Lua 5.1 at LUA51=${LUA51:-lua5.1} (MSYS2: mingw-w64-ucrt-x86_64-lua51)"
# The compiler from this same tree first: the page needs its `-o *.js` target, and PARENA_BIN (collection-env.sh)
# may name another checkout's older build.
if [ -x "$parena/.parena-bootstrap" ]; then compiler="$parena/.parena-bootstrap"; else compiler="${PARENA_BIN:-}"; fi
[ -x "$compiler" ] || die "no PARENA compiler at $compiler (cd PARENA && make .parena-bootstrap)"
winpath() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s' "$1"; fi; }

out="$COLLECTION_TMP/hoard"
mkdir -p "$out"
"$compiler" build "$parena/stdlib/collection/ledger.prn" -o "$out/ledger.lua" >/dev/null
# the page: PARENA emitted as plain JavaScript (`-o *.js`), no Node, no TypeScript toolchain
"$compiler" build "$parena/tools/hoard/hoard_page.prn" -o "$out/app.js" >/dev/null
cp "$parena/runtime/wow/ledger_export.lua" "$parena/runtime/wow/journal_reader.lua" "$parena/tools/hoard/page.html" "$out/"
addon="${FAFNIR_ADDON:-FafnirsBed}"
var="${FAFNIR_VAR:-FafnirsBedDB}"

if [ "${1:-}" = "-csv" ]; then
    dest="${2:?usage: hoard.sh -csv FILE}"
    args=() saved=0
    for f in "$WOW_WTF_DIR"/Account/*/SavedVariables/"$addon".lua; do
        [ -f "$f" ] || continue
        args+=("$(basename "$(dirname "$(dirname "$f")")")" "$f")
        t="$(stat -c %Y "$f")"; [ "$t" -gt "$saved" ] && saved="$t"
    done
    run() { "$lua" "$out/ledger_export.lua" "$out/journal_reader.lua" "$out/ledger.lua" csv "$var" "$saved" ${args[@]+"${args[@]}"}; }
    if [ "$dest" = "-" ]; then run; else run > "$dest"; echo "hoard.sh: $(($(wc -l < "$dest") - 1)) rows -> $dest" >&2; fi
    exit 0
fi

"$compiler" build "$parena/stdlib/string.prn" "$parena/stdlib/net/tcp.prn" "$parena/stdlib/net/http_server.prn" \
    "$parena/stdlib/process.prn" "$parena/tools/hoard/hoard.prn" -o "$out/hoard_gen.c" >/dev/null
lib=()
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) lib=(-lws2_32 -static-libgcc) ;; esac
gcc -std=c99 -Wall -Wextra -Werror -DPARENA_NO_GRAPHICS -I "$parena/runtime" -I "$parena/tools/hoard" \
    -include "$parena/tools/hoard/hoard_host.h" "$out/hoard_gen.c" "$parena/tools/hoard/hoard_host.c" \
    "$parena/runtime/parena_runtime.c" -o "$out/hoard.exe" ${lib[@]+"${lib[@]}"}

export WOW_WTF_DIR="$(winpath "$WOW_WTF_DIR")" HOARD_LIB="$(winpath "$out")" HOARD_LUA="$(winpath "$lua")"
export FAFNIR_ADDON="$addon" FAFNIR_VAR="$var"
exec "$out/hoard.exe"
