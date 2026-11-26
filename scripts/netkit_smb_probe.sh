#!/usr/bin/env bash
# What does a Windows file-sharing (SMB, port 445) server say about itself before anyone logs in? (KIT-SMB S0)
#   scripts/netkit_smb_probe.sh <host> [port] [trace.hex]
# The logic is stdlib/netkit/nk_smb.prn inside the netkit wasm; the host is stdlib/netkit/tools/nk_smb_probe.prn (PARENA -> TS -> JS,
# node 24+). No password is sent and nothing on the server is read or changed. Needs the LLVM toolchain of build_netkit_wasm.sh.
set -euo pipefail
cd "$(dirname "$0")/.."
PARENA="${PARENA_BIN:-./.parena-bootstrap}"
[ -x "$PARENA" ] || make .parena-bootstrap >/dev/null
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
NETKIT_MEM=8388608 scripts/build_netkit_wasm.sh "$W/netkit.wasm" >/dev/null
"$PARENA" build stdlib/netkit/tools/nk_smb_probe.prn -o "$W/probe.ts" >/dev/null
node scripts/ts_strip.mjs "$W/probe.ts" "$W/probe.mjs" 2>/dev/null
node "$W/probe.mjs" "$W/netkit.wasm" "$@"
