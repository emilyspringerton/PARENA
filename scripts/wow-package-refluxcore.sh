#!/usr/bin/env bash
# scripts/wow-package-refluxcore.sh -- build the RefluxCore World of Warcraft addon from PARENA source.
#
# RefluxCore is the first addon every other Collection addon depends on (## Dependencies: RefluxCore): it
# carries the shared REFLUX log (runtime/wow/reflux_host.lua) and the PARENA-built REFLUX stdlib
# (stdlib/reflux/reflux.prn, cursor.prn), registered as RefluxCore.modules["reflux/reflux"] and
# ["reflux/cursor"]. Nothing here is hand-written addon logic; the .lua modules are `parena build` output.
#
# Environment (all overridable; see MONOREPO/scripts/collection-env.sh):
#   WOW_INTERFACE     REQUIRED. The client's TOC interface number (4th value of GetBuildInfo()). Never
#                     defaulted: it is an unconfirmed fact about WoW Forever, so the build refuses without it.
#   PARENA_BIN        compiler driver               (default ./.parena-bootstrap)
#   REFLUXCORE_OUT    where the addon folder is made (default build/wow)
#   WOW_ADDONS_DIR    install target for --install
#
# usage: scripts/wow-package-refluxcore.sh [--install]
set -euo pipefail

cd "$(dirname "$0")/.."
PARENA_BIN="${PARENA_BIN:-./.parena-bootstrap}"
REFLUXCORE_OUT="${REFLUXCORE_OUT:-build/wow}"

if [ -z "${WOW_INTERFACE:-}" ]; then
  echo "wow-package-refluxcore: WOW_INTERFACE is not set." >&2
  echo "  Run in game:  /run print(select(4, GetBuildInfo()))  and export the number it prints." >&2
  exit 2
fi
case "$WOW_INTERFACE" in *[!0-9]*) echo "wow-package-refluxcore: WOW_INTERFACE must be digits, got '$WOW_INTERFACE'" >&2; exit 2 ;; esac
[ -x "$PARENA_BIN" ] || { echo "wow-package-refluxcore: no compiler at $PARENA_BIN (make .parena-bootstrap)" >&2; exit 2; }

addon="$REFLUXCORE_OUT/RefluxCore"
rm -rf "$addon"
mkdir -p "$addon"

# wrap NAME SRC.prn OUT.lua -- compile, then register the module's table in RefluxCore.modules[NAME].
# The emitted chunk ends in `return M`; WoW discards a file's return value, so it is called as a function.
wrap() {
  local name="$1" src="$2" out="$3" tmp
  tmp="$(mktemp)"
  "$PARENA_BIN" build "$src" -o "$tmp.lua" >/dev/null
  {
    echo "-- RefluxCore: $name, built by parena from $src. Do not edit; edit the .prn."
    echo "RefluxCore = RefluxCore or { modules = {} }"
    echo "RefluxCore.modules[\"$name\"] = (function(...)"
    cat "$tmp.lua"
    echo "end)(...)"
  } > "$out"
  rm -f "$tmp" "$tmp.lua"
}

cp runtime/wow/reflux_host.lua "$addon/reflux_host.lua"
wrap reflux/reflux stdlib/reflux/reflux.prn "$addon/reflux.lua"
wrap reflux/cursor stdlib/reflux/cursor.prn "$addon/cursor.lua"

version="$(git -c safe.directory='*' rev-parse --short HEAD 2>/dev/null || echo dev)"
# Title/Notes are enUS only for now: the 12-locale TOC fields (## Title-deDE: ...) come from the i18n
# catalogues once they exist, not from a guess here.
cat > "$addon/RefluxCore.toc" <<EOF
## Interface: $WOW_INTERFACE
## Title: REFLUX Core
## Notes: The shared event log every Collection addon talks through.
## Version: $version
## X-Built-By: PARENA

reflux_host.lua
reflux.lua
cursor.lua
EOF

echo "wow-package-refluxcore: built $addon (interface $WOW_INTERFACE, $version)"

if [ "${1:-}" = "--install" ]; then
  [ -n "${WOW_ADDONS_DIR:-}" ] || { echo "wow-package-refluxcore: --install needs WOW_ADDONS_DIR" >&2; exit 2; }
  rm -rf "$WOW_ADDONS_DIR/RefluxCore"
  cp -r "$addon" "$WOW_ADDONS_DIR/RefluxCore"
  echo "wow-package-refluxcore: installed to $WOW_ADDONS_DIR/RefluxCore"
fi
