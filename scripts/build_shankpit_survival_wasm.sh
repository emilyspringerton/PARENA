#!/usr/bin/env bash
# Builds SURVIVAL (the SHANKPHONE's survival button): the shankpit-460 PARENA engine plus the CITY level and the survival
# mods (stdlib/shankpit_survival/MODULES), to one wasm32 module -- same toolchain and the same guarantees as the arena
# (no allocator, no imports, fixed 8 MB memory). The page host is shankpit-460's own (sp-host.js): the exports match.
#   scripts/build_shankpit_survival_wasm.sh <out.wasm>
set -euo pipefail
cd "$(dirname "$0")/.."
SP_MODULES=stdlib/shankpit_survival/MODULES exec scripts/build_shankpit460_wasm.sh "${1:?usage: build_shankpit_survival_wasm.sh <out.wasm>}"
