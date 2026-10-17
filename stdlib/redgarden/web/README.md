# REDGARDEN web (PARENA)

Red Garden -- the arena MOBA ("Knights of the Void") -- in the browser, at `wotan.okemily.com/redgarden/play.html`.
Same shape as BRAWLPIT web and shankpit-460 web: **the whole client is PARENA compiled to wasm32** (no allocator, no imports,
a fixed 8 MB) plus a page host that is PARENA compiled to TypeScript.

Founder real-time, 2026-10-17: *"publish redgarden web ... it is an esport so it needs FULL WOTAN MATCH AND PLAYER TRACKING ... it
should allow for guest play ... it should integrate with iduna sso ... it should use existing bot match infrastructure and bot
pool ... PARENA WASM, PARENA TSX, just like shankpit-460 and brawlpit web ... make it nice, very demure."*

## It is a client, not a port of the game

Unlike BRAWLPIT and shankpit-460 (whose rules were ported to PARENA), Red Garden's rules are ~10,000 lines of C that already run
as an authoritative server with a persistent 19-bot pool in the cluster. Re-implementing them would have made a second, diverging
game. So **the match is the real arena server's**, and this module is a client of it:

```
browser: redgarden.wasm  <-- WebSocket -->  IDUNA gateway  <-- UDP (protocol.h) -->  matchmaker + 19 bots + arena_server
         (this directory)    (IDUNA/internal/redgardenplay)                           (REDGARDEN, C, unchanged)
```

The gateway mints the player's connect ticket, queues with the live matchmaker, relays validated orders and a throttled (20 Hz)
snapshot stream, and records the result from the arena's own snapshots. The client draws those snapshots straight out of wasm
memory (no second copy), runs the hero draft, the shop, the HUD, and turns clicks and keys into orders.

## What is where

| file | what |
|---|---|
| `rg_mem.prn` | memory map, accessors, the globals (`g-*`), small math |
| `rg_data.prn` | **generated** by `tools/rg_data_gen.py` from dumps of the real game (hero names/roles, ability names + descriptions, the 43-item catalog, the static map) |
| `rg_snap.prn` | readers for the arena's snapshot structs (offsets checked against the real C structs: `tools/data/layout.txt`), hero interpolation |
| `rg_net.prn` | the gateway protocol, the screen state machine, the kill feed (derived from kill/death count changes) |
| `rg_input.prn`, `rg_gfx.prn` | keyboard / pointer; 2D software-renderer primitives and text |
| `rg_view.prn`, `rg_hud.prn`, `rg_ui.prn` | the arena, the HUD (node race, abilities, minimap, scoreboard, shop, help), the screens and orders |
| `rg_main.prn` | the exports the page calls |
| `rg_host.prn` | the page host (PARENA -> TypeScript): instantiate, blit, input, **identity** (WOTAN SSO or a remembered guest account), the WebSocket, labels |
| `MODULES` | the build order |

Build: `scripts/build_redgarden_web.sh <outdir>` -> `redgarden.wasm` + `rg-host.js`. WOTAN commits both
(`WOTAN/scripts/build-redgarden.sh`).

## Facts about the real server this client had to know

* **Teams are split at slot 10** (`ARENA_TEAM_SIZE`), whatever the lobby size: a 6-player "3v3" lobby is six heroes on one team with
  nobody to fight. That is a latent bug in the live 3v3 queue (`:7780`), so only the 10v10 pools are used. A human takes the last slot.
* The draft has no timer of its own, but the server shuts a lobby down 60 s after it starts if the match is not live; the client
  auto-picks at 35 s and the gateway backstops at 38 s.
* The server broadcasts three UDP packets per 16 ms tick (a world message and two hero chunks); the gateway coalesces them to 20 Hz
  and the client slides heroes between the last two snapshots.
* The prod arena pods have no IDUNA agent credentials, so the arena itself reports nothing; the gateway records every web match.

## Limits (honest)

No audio, no touch controls (a tap moves/attacks, but there is no on-screen shop button or scoreboard), no fog of war (the server
sends everything), no replay, one top-down camera. Verified in headless Chromium against the real stack
(`WOTAN/scripts/redgarden-browser-test.mjs`); **not** verified in Firefox, Safari or on a phone.
