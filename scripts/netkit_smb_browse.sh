#!/usr/bin/env bash
# Browse Windows file sharing (SMB) from the command line, with the Netkit's PARENA code (KIT-SMB S2):
#   NETKIT_SMB_PASSWORD=... scripts/netkit_smb_browse.sh <host> [--port N] [--user U] [--domain D] shares
#   ... ls <share> [folder/path]        ... cat <share> <file/path>   (shows the first 60 KB if it is text)
# No --user = anonymous. The password is read from NETKIT_SMB_PASSWORD (never put it on the command line). Read only: nothing on the
# server is changed. Logic: stdlib/netkit/nk_smb_files.prn (wasm); host: stdlib/netkit/tools/nk_smb_browse.prn (PARENA -> TS, node 24+).
set -euo pipefail
cd "$(dirname "$0")/.."
HOST="${1:?usage: netkit_smb_browse.sh <host> [--port N] [--user U] [--domain D] shares|ls|cat [share] [path]}"; shift
PORT=445; USER_=""; DOMAIN=""; TRACE="${NETKIT_SMB_TRACE:-}"
while [ $# -gt 0 ]; do
  case "$1" in --port) PORT="$2"; shift 2;; --user) USER_="$2"; shift 2;; --domain) DOMAIN="$2"; shift 2;; *) break;; esac
done
CMD="${1:?shares, ls or cat}"; SHARE="${2:-}"; P="${3:-}"
PARENA="${PARENA_BIN:-./.parena-bootstrap}"
[ -x "$PARENA" ] || make .parena-bootstrap >/dev/null
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
NETKIT_MEM=8388608 scripts/build_netkit_wasm.sh "$W/netkit.wasm" >/dev/null
"$PARENA" build stdlib/netkit/tools/nk_smb_browse.prn -o "$W/browse.ts" >/dev/null
node scripts/ts_strip.mjs "$W/browse.ts" "$W/browse.mjs" 2>/dev/null
node "$W/browse.mjs" "$W/netkit.wasm" "$HOST" "$PORT" "$USER_" "$DOMAIN" "$CMD" "$SHARE" "$P" ${TRACE:+"$TRACE"}
