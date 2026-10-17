# D2 web (PARENA)

D2 (DEADWEIGHT_2, "Hold Battles") in the browser, at `wotan.okemily.com/d2/play.html`. Same shape as BRAWLPIT web, shankpit-460 web and Red Garden
web: **the whole client is PARENA compiled to wasm32** (no allocator, no imports, a fixed 8 MB) plus a page host that is PARENA compiled to TypeScript.

Founder real-time, 2026-10-17: *"now DEADWEIGHT_2 same affordances WOTAN ESPORTS WASM TS PARENA"* -- the affordances of Red Garden web: full match and
player tracking, guest play, IDUNA SSO, the existing bot infrastructure.

## It is a client of the real server

D2's rules are a small authoritative C server (`dw2_server`: pairing, a 120 s packing clock, a 30-tick fight, the round-break mini-game). This module is a
client of it, not a port:

```
browser: d2.wasm  <-- WebSocket -->  IDUNA gateway  <-- TCP (core/protocol.h) -->  dw2_server  <--  dw2_bot pool
        (this dir)   (IDUNA/internal/d2play)                                        (DEADWEIGHT_2, C)
```

The gateway introduces the player to the (`--no-auth`) server by name, relays only the player's own frames, and records the finished match from the
server's frames against the player's IDUNA account (Elo, win/loss, the ship they packed, every round break). The bots are `dw2_bot`
(`DEADWEIGHT_2/apps/bot`), protocol clients like any other; the server's own pairing rule gives a human the oldest waiting bot. The wire format on the
socket is the server's own frames with the length dropped, plus three gateway frames (who you are, the match was recorded, an error).

## Ruleset 2 and what the client does about it

Founder real-time, 2026-10-17, after playing it: damage "all of a sudden" took a ship from full health to dead; Titanium and Aether Ore had no visible point; "it
should matter where you put stuff"; "all components need to have a cost and a weight ... and all of it needs to read to the player without you having to explain it";
cutting was "something is happening but its not clear what"; "4 lasers and the rest bulwarks wins every time". The rules are DEADWEIGHT_2's `docs/RULESET_2.md`
(24 parts, 1000 credits and 40 tons, HP per cell, shots down lanes, in-place SPLIT, an EVENTS frame after every tick). The client's job is to make every one of those
rules readable:

* **Packing shows the consequence of a placement before the fight** (`d2_pack.prn`). A bin of all 24 parts with price tags (credits, tons); two budget bars; a
  part card for whatever is under the pointer (cost, weight, HP, what each weapon does to it, what it feeds, boosts, heat); the lane table ("shots to open a
  lane", for lasers, slugs and shells, per column, with the HP behind it); the guns' fed/starved/blocked state and the damage a second they will do; the repair and
  heat summary (hot parts, parts a Cryo-Vent saves); gold dots for the power on the move; red BLOCKED/UNFED marks. Undo, CLEAR, EXAMPLE (the bot pool's ships and two
  drawn ones), and SPLIT (key C): a Titanium Beam becomes two Shards and an Aether Ore four Chips on exactly the cells they held, with a preview of the pieces.
  Nothing is sent until READY (or six seconds before the pack clock ends), because the server has no "remove a part".
* **Shows the wiring and the budgets truthfully** (`d2_rules.prn`): a port of the parts of `core/` that need no opponent -- the catalog (`core/items.c`: cost,
  weight, HP, multipliers), shapes, `dw2_ship_place` with the budgets, `dw2_split_pieces`, heat and cooling, weapon boosts, the clear-lane test for lasers,
  `dw2_lane_shots_to_open` (including a cannon shell's splash inside a lane) and the self half of the tick (energy through wires, the Back-EMF meltdown of a closed
  loop, weapon charge). **Verified against the C core number by number**: `scripts/d2_rules_parity.sh <DEADWEIGHT_2 checkout>` replays 4,900+ ships (the bot
  pool's, a textbook ring, random and dense wiring, structured circuits) and every tick's shots, meltdowns, burned and destroyed cells, and every ship's cost, weight, structure, dodge chance, heat map, boosts, lane numbers and splits agree
  with the C core (last run: 4,913 ships, 88,434 lane numbers, 2,075 splits, 3,490 boosted guns).
* **The fight is animated from the server's EVENTS, never guessed** (`d2_net.prn`, `d2_fx.prn`). The client runs only the circuit locally; damage, healing,
  evasion and destruction come from the EVENTS frame and are applied to per-cell HP arrays and scheduled as effects: a shot travels down its lane, the damage lands
  as a number on the part it hit (cracks and a red wreck marker as the part fails), the hull bar glides and leaves a ghost of what it lost, a hit shakes the ship, a
  dodge says DODGED, repairs and mends float up green, heat and Back-EMF flash. Both ships are drawn (the opponent's from OPP_SHIP, mirrored top to bottom so
  lanes line up bow to bow) with a stern wall of lane guides: pips for the guns armed in each lane and a red stripe when a lane is open to the hull. The log
  ("what just happened") names who hit what for how much; hovering a part shows its HP and what is hurting it.
* **Plays the round break** (`d2_ui.prn`, `d2_net.prn`): the server grades the *receipt time* of the press against a target; the client measures the round trip
  through the gateway (PING/PONG) and puts the line at `target - rtt`, so the player aims at what they see.

## What is where

| file | what |
|---|---|
| `d2_mem.prn` | memory map (items, examples, effects, undo), accessors, the globals (`g-*`), small math |
| `d2_gfx.prn` | the 2D software renderer (960x540 RGBA, 8x16 bitmap font shared with the GFD terminal), D2's quiet palette |
| `d2_rules.prn` | the item catalog with costs, weights, HP and multipliers; shapes, the player's grid, budgets, splits, boosts, heat, lanes, the wiring and its simulation |
| `d2_examples.prn` | **generated** by `scripts/d2_gen_examples.py` from `DEADWEIGHT_2/apps/bot/loadouts.h`: the EXAMPLE button's ships |
| `d2_input.prn` | keyboard / pointer |
| `d2_net.prn` | the protocol, the screen state machine, frames in (EVENTS become HP changes and effects) |
| `d2_view.prn` | drawing a ship: cells with HP bars, cracks, wrecks and heat, wires, welds, art for all 24 parts, bars, widgets |
| `d2_fx.prn` | the fight's effects: shots down lanes, damage numbers, hits, dodges, repairs, shields, heat, the hull ghost and the HP glide |
| `d2_pack.prn` | the packing screen: bin and prices, budget bars, lane table, part card, undo / clear / split / example, READY |
| `d2_ui.prn` | the other screens (title, searching, fight, result, error), the arena, roster, log, inspect panel, lane guides |
| `d2_main.prn` | the exports the page calls, and test hooks |
| `d2_host.prn` | the page host (PARENA -> TypeScript): instantiate, blit, input, **identity** (WOTAN SSO or a remembered guest account), the WebSocket, labels |
| `MODULES` | the build order |

Build: `scripts/build_d2_web.sh <outdir>` -> `d2.wasm` + `d2-host.js`. WOTAN commits both (`WOTAN/scripts/build-d2.sh`).

## Limits (honest)

No audio. Mouse and keyboard only (the on-screen buttons work by tap, but part placement on a phone is untested). The wasm is about 340 KB. The animation is as
good as the events: a second with more than 60 events drops the rest (the frame carries the count; the client keeps it but does not show it). Input is latched once per animation frame, like
the other wasm clients. Verified in headless Chromium against the real stack (`WOTAN/scripts/d2-browser-test.mjs`, 62 checks, and a live smoke against production,
`WOTAN/scripts/d2-live-smoke.mjs`); **not** verified in Firefox, Safari or on a phone, and two humans meeting in the browser is covered by the gateway's Go test against
the real server, not by a browser test. Whether the layouts are *fun* against skilled people is untested: the bot pool's balance comes from a search that ignores the
round-break duel (`DEADWEIGHT_2/docs/RULESET_2.md`).
