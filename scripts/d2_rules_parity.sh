#!/usr/bin/env bash
# Checks the D2 web client's port of the ship rules against the real combat core, tick by tick.
#   scripts/d2_rules_parity.sh /path/to/DEADWEIGHT_2 [dense-ships] [chain-ships]
# Builds DEADWEIGHT_2/tools/dw2_ref_dump.c (the C core: the bot pool's ships, a textbook closed loop, uniformly random ships and
# dense wiring that makes closed loops and Back-EMF meltdowns), builds d2.wasm, and replays every ship through the wasm's own port of
# dw2_ship_place / dw2_ship_panic_cut / the self half of dw2_ship_tick (stdlib/d2/web/d2_rules.prn): shots per tick, meltdowns and the
# burned cells must agree on every one.
set -euo pipefail
cd "$(dirname "$0")/.."
D2="${1:?usage: d2_rules_parity.sh /path/to/DEADWEIGHT_2 [ships]}"
N="${2:-3000}"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
( cd "$D2" && gcc -std=c99 -Wall -Wextra -Werror -Icore -DPARENA_NO_GRAPHICS -O2 tools/dw2_ref_dump.c core/items.c core/combat.c core/round.c \
    core/cannon.c core/parena_runtime.c cannon/cannon_decision_gen.c -o "$W/ref_dump" -lm )
"$W/ref_dump" 400 5 "$N" "${3:-1500}" > "$W/ref.txt"
scripts/build_d2_wasm.sh "$W/d2.wasm" >/dev/null
node scripts/d2_rules_parity.mjs "$W/d2.wasm" "$W/ref.txt"
