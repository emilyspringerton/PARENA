#!/usr/bin/env bash
# scripts/wow-package-refluxcore.sh -- build the RefluxCore World of Warcraft addon from PARENA source.
#
# RefluxCore is the first addon every other Collection addon depends on (## Dependencies: RefluxCore): it
# carries the shared REFLUX log (runtime/wow/reflux_host.lua) and the PARENA-built REFLUX stdlib
# (stdlib/reflux/reflux.prn, cursor.prn), registered as RefluxCore.modules["reflux/reflux"] and
# ["reflux/cursor"], and the REFLUX tips API: its rules (stdlib/collection/tips.prn, ["collection/tips"]), its
# catalogue and its registry glue (runtime/wow/reflux_tips.lua, the global RefluxTips). The .lua modules are
# `parena build` output; the only hand-written Lua is host glue, as everywhere in the Collection.
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

# wrap NAME OUT.lua SRC.prn... -- compile the sources as one module, then register its table in
# RefluxCore.modules[NAME]. The emitted chunk ends in `return M`; WoW discards a file's return value, so it is
# called as a function.
wrap() {
  local name="$1" out="$2" tmp
  shift 2
  tmp="$(mktemp)"
  "$PARENA_BIN" build "$@" -o "$tmp.lua" >/dev/null
  {
    echo "-- RefluxCore: $name, built by parena from $*. Do not edit; edit the .prn."
    echo "RefluxCore = RefluxCore or { modules = {} }"
    echo "RefluxCore.modules[\"$name\"] = (function(...)"
    cat "$tmp.lua"
    echo "end)(...)"
  } > "$out"
  rm -f "$tmp" "$tmp.lua"
}

cp runtime/wow/reflux_host.lua "$addon/reflux_host.lua"
wrap reflux/reflux "$addon/reflux.lua" stdlib/reflux/reflux.prn
wrap reflux/cursor "$addon/cursor.lua" stdlib/reflux/cursor.prn
# The REFLUX tips API every Collection addon publishes its tips through (/rumor tells one about another addon):
# the rules, RefluxCore's own catalogue (its tips and the API's lines) and the registry glue, RefluxTips.
wrap collection/tips "$addon/tips.lua" stdlib/reflux/reflux.prn stdlib/collection/tips.prn
cp runtime/wow/locales/reflux.lua "$addon/locale.lua"
cp runtime/wow/reflux_tips.lua "$addon/reflux_tips.lua"

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
tips.lua
locale.lua
reflux_tips.lua
EOF

echo "wow-package-refluxcore: built $addon (interface $WOW_INTERFACE, $version)"

if [ "${1:-}" = "--install" ]; then
  [ -n "${WOW_ADDONS_DIR:-}" ] || { echo "wow-package-refluxcore: --install needs WOW_ADDONS_DIR" >&2; exit 2; }
  rm -rf "$WOW_ADDONS_DIR/RefluxCore"
  cp -r "$addon" "$WOW_ADDONS_DIR/RefluxCore"
  echo "wow-package-refluxcore: installed to $WOW_ADDONS_DIR/RefluxCore"
fi
