# D2 web (PARENA)

D2 (DEADWEIGHT_2, "Hold Battles") in the browser, at `wotan.okemily.com/d2/play.html`. Same shape as BRAWLPIT web, shankpit-460 web and Red Garden
web: **the whole client is PARENA compiled to wasm32** (no allocator, no imports, a fixed 8 MB) plus a page host that is PARENA compiled to TypeScript.

Founder real-time, 2026-10-17: *"now DEADWEIGHT_2 same affordances WOTAN ESPORTS WASM TS PARENA"* -- the affordances of Red Garden web: full match and
player tracking, guest play, IDUNA SSO, the existing bot infrastructure.

## It is a client of the real server

D2's rules are a small authoritative C server (`dw2_server`: pairing, a 60 s packing clock, a 30-tick fight, the round-break mini-game). This module is a
client of it, not a port:

```
browser: d2.wasm  <-- WebSocket -->  IDUNA gateway  <-- TCP (core/protocol.h) -->  dw2_server  <--  dw2_bot x6
        (this dir)   (IDUNA/internal/d2play)                                        (DEADWEIGHT_2, C)
```

The gateway introduces the player to the (`--no-auth`) server by name, relays only the player's own frames, and records the finished match from the
server's frames against the player's IDUNA account (Elo, win/loss, the ship they packed, every round break). The bots are `dw2_bot`
(`DEADWEIGHT_2/apps/bot`), protocol clients like any other; the server's own pairing rule gives a human the oldest waiting bot. The wire format on the
socket is the server's own frames with the length dropped, plus three gateway frames (who you are, the match was recorded, an error).

## What the client does

* **Packs the ship locally** (`d2_ui.prn`): pick a part, turn it, place it, take it back, undo, cut a splittable piece. Nothing is sent until READY (or six
  seconds before the pack clock ends), because the server has no "remove a part". The sixteen-part limit, legality and Panic Cut fit are the server's rules.
* **Shows the wiring truthfully** (`d2_rules.prn`): a port of the parts of `core/` that need no opponent -- `core/items.c`, `dw2_shape_cells`,
  `dw2_ship_place`, `dw2_ship_panic_cut` and the self half of `dw2_ship_tick` (energy through wires into railguns, the Back-EMF meltdown of a closed loop, weapon
  charge). The readout ("railguns fed", "a closed loop") and the energy the player watches in the fight are what the server fights with.
  **Verified against the C core tick by tick**: `scripts/d2_rules_parity.sh <DEADWEIGHT_2 checkout>` replays 3,400+ ships (the bot pool's, a textbook ring, random and dense
  wiring that produces meltdowns) and every tick's shots, meltdowns and burned cells agree.
* **Plays the round break** (`d2_ui.prn`, `d2_net.prn`): the server grades the *receipt time* of the press against a target; the client measures the round trip
  through the gateway (PING/PONG) and puts the line at `target - rtt`, so the player aims at what they see.
* **The opponent is sealed**: the protocol carries only hull, armor and cargo scalars, so the client shows exactly that.

## What is where

| file | what |
|---|---|
| `d2_mem.prn` | memory map, accessors, the globals (`g-*`), small math |
| `d2_gfx.prn` | the 2D software renderer (960x540 RGBA, 8x16 bitmap font shared with the GFD terminal), D2's quiet palette |
| `d2_rules.prn` | the item catalog, shapes, the player's grid, the wiring and its simulation |
| `d2_examples.prn` | **generated** by `scripts/d2_gen_examples.py` from `DEADWEIGHT_2/apps/bot/loadouts.h`: the EXAMPLE button's ships |
| `d2_input.prn` | keyboard / pointer |
| `d2_net.prn` | the protocol, the screen state machine, frames in |
| `d2_view.prn` | drawing a ship: cells, wires, parts bin, bars |
| `d2_ui.prn` | the screens (title, searching, packing, fight, result, error) and what clicks mean |
| `d2_main.prn` | the exports the page calls, and test hooks |
| `d2_host.prn` | the page host (PARENA -> TypeScript): instantiate, blit, input, **identity** (WOTAN SSO or a remembered guest account), the WebSocket, labels |
| `MODULES` | the build order |

Build: `scripts/build_d2_web.sh <outdir>` -> `d2.wasm` + `d2-host.js`. WOTAN commits both (`WOTAN/scripts/build-d2.sh`).

## Limits (honest)

No audio. Mouse and keyboard only (the on-screen buttons work by tap, but part placement on a phone is untested). The opponent's ship is never shown: the protocol
does not carry it. Input is latched once per animation frame, like the other wasm clients. Verified in headless Chromium against the real stack
(`WOTAN/scripts/d2-browser-test.mjs`); **not** verified in Firefox, Safari or on a phone, and two humans meeting in the browser is covered by the gateway's Go test against the real
server, not by a browser test.
