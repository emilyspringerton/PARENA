# shankpit-460 web (PARENA)

`shankpit-460` -- the brand name, lower case and hyphenated, exactly like that -- is the competitive esports fork of
SHANKPIT: a small, quick arena FFA shooter. This directory is its browser build: **the whole game is PARENA compiled to
wasm32** (no allocator, no imports, a fixed 8 MB), plus a page host that is PARENA compiled to TypeScript. It lives at
`wotan.okemily.com/shankpit-460/`. Same shape as BRAWLPIT web (`../brawlpit/`).

Founder real-time, 2026-10-16: *"ADD SHANKPIT 460 as a first class citizen to WOTAN, full esports treatment, full IDUNA and
WOTAN integration ... keep it simple ... queue them into a bot match and say SEARCHING FOR MATCH ... if there is at least 1
other human in matchmaking put them in a ffa lobby ... human matches are first to 16 or they end at 6 minutes ... an end screen
with the score and a requeue button ... make it nice, very thoughtful, very demure ... THE GAME IS PORTED TO WASM AND
TYPESCRIPT VIA PARENA ... feel free to improve the bots."* Stats and the bot league are the **second pass** and are not here.

## What is where

| file | what |
|---|---|
| `sp_mem.prn` | memory map, accessors, the globals (`g-*`), PRNG, trig/atan2 without libm (F64 series -> F32) |
| `sp_fields.prn` | **generated** by `tools/sp_fields_gen.py`: player / projectile / tracer / particle record accessors |
| `sp_map.prn`, `sp_map_data.prn` | the arena: boxes, spawns, ray tests. `sp_map_data.prn` is **generated** by `tools/sp_map_gen.py` from shankpit-460's own `maps/v0_shankpit.map` |
| `sp_map_w77.prn` | map 1 of the bot-match rotation: 68 boxes ripped from the ShankPit Windows build 77 client (`map_geo` in ShankPit.exe; also `maps/w77_map.json`). Spawns and crown are placeholders (the exe table has none). Bot matches alternate v0 / W77; human lobbies stay on v0 until IDUNA's hit checks and respawns know W77 |
| `sp_phys.prn` | movement, collision, weapons, the katana, storm projectiles -- the port (see Parity) |
| `sp_sim.prn`, `sp_rules.prn` | match setup, the 16 ms tick, win conditions, the final board |
| `sp_ai.prn` | the bots |
| `sp_gfx.prn`, `sp_3d.prn`, `sp_view.prn` | 2D primitives and text, the perspective-correct z-buffered rasteriser, the arena and the people |
| `sp_hud.prn`, `sp_ui.prn`, `sp_input.prn` | HUD, title / play / end screens, keyboard + mouse + gamepad |
| `sp_fx.prn` | tracers, sparks, kill feed, the sound-event ring |
| `sp_net.prn` | the matchmaking wire protocol, spoken from inside the wasm |
| `sp_main.prn` | the exports the page calls, the frame loop, test hooks (`sp_t_*`) |
| `sp_host.prn` | the page host: instantiate, blit, forward input / pointer lock, the WebSocket, WebAudio, labels |
| `MODULES` | the build order |

Build: `scripts/build_shankpit460_web.sh <outdir>` -> `shankpit460.wasm` + `sp-host.js`. WOTAN commits both
(`WOTAN/scripts/build-shankpit460.sh`).

## The game

The match flow is the founder's: **PLAY -> a bot match starts at once and the HUD says SEARCHING FOR MATCH**. If another
human is searching, both are moved into a free-for-all lobby (a short countdown in which more can join; a match still in its
first four and a half minutes takes late arrivals). **Human matches are first to 16 kills or six minutes**; bot matches keep the
live pool's format, first to 13 or five minutes. Then an **end screen with the standings and REQUEUE**.

Controls are the native client's: WASD, mouse (0.15 deg/px, 0.05 zoomed), Space (hold to float), Ctrl / C / Shift crouch, R,
E (ability: storm charges for the sniper, the dash for the katana), 1-6, wheel, right mouse zooms the sniper, Tab shows scores.
A gamepad works (sticks, RT fire, A / B / X, bumpers).

### Parity with the C engine

`sp_phys.prn` is a line-for-line port of the **online** movement path of shankpit-460 (`apply_usercmd_inputs` ->
`shankpit_simulate_movement_tick` -> `update_entity` -> `update_weapons`, `update_projectiles`, the katana), in F32 with the C
evaluation order. `tests/shankpit460_parity_test.mjs` runs it side by side with the real `physics.h` / `net_sim.h` /
`local_game.h`: 40 scenarios (open-field walking, slide-jumps, jump arcs, the rocks, both bases, the middle tower, the
perimeter, every weapon at three ranges, reloads, the storm, the dash) -- **worst difference 5e-7** over 2,223 player
checkpoints. (The online path applies gravity twice per tick -- once in the movement tick, once in `update_entity` -- so a tap
jump clears ~3 units and a held one ~9, not the 6 and 18 the map file's comment says. That is how the live game plays and it is
ported as is.)

### Deviations from the C engine (deliberate, all small)

1. **Walls stop bullets.** The C hitscan loops over players with no line-of-sight test, so shots pass through every box.
   Here a hit needs a clear line to the volume it hit.
2. **A sniper-storm kill goes through the same delayed death as every other kill.** The C projectile path respawned the victim
   in the same tick.
3. **A respawn zeroes velocity, faces the arena's middle, and (in a human match) carries a second of spawn protection.**
4. **Respawn takes the safest spawn** (farthest from every living opponent) instead of `spawns[id % n]`, so ten players on
   eight spawns do not stack.
5. The cosmetic dice (sparks, sound pitch, camera shake) use their own generator so what you see can never change what the
   simulation rolls. Everything else the simulation rolls (shotgun / AR spread) is the same LCG glibc's `rand()` is, which is how
   the parity harness lines the two engines up.

## The bots

`sp_ai.prn`. The C engine's `bot_think` runs at the nearest player and holds the trigger; the live pool's bots
(`apps2/emily-bot`) are QA clients that aim at the nearest peer. Neither can see a wall. These bots perceive (view cone, range,
line of sight, a reaction time that starts at a fresh sighting, a memory of where someone was), aim with a spring (turn rate and
noise per level; noise grows with how fast the target crosses the view; a shot is only taken once the crosshair is inside the
target's angular size, so a flick is a miss, not a laser), choose weapons by range and by whether the shield is down (a head
shot with no shield is triple damage), keep each weapon's range, strafe, dodge-jump, run for cover below a health line, hunt to the
last place they saw you, scan, roam between the spawns / the crown / the rocks with whisker avoidance, hop low ledges and recover
when stuck. HARD also charges the sniper's storm. They only ever write what a person writes (forward / strafe / yaw / pitch /
buttons through `apply-usercmd!`), so they obey the same physics and weapon rules. Levels EASY / NORMAL / HARD on the title screen.
`tests/shankpit460_bot_soak.mjs` plays eight bots against each other (kills/min about 8 / 35 / 60, none idle).

## Networking

The wasm cannot touch a socket. The page host owns one WebSocket to IDUNA's matchmaker (`/api/v1/shankpit460/play`) and does
two things with it: copy a received frame to `NETIN` and call `sp_net_recv`, and send what the engine left in `NETOUT`
(`[u16 len][frame]...`). The protocol is documented in `IDUNA/internal/shankpit460/proto.go`; `sp_net.prn` is its client.

* **Authority.** In a human match the server owns the clock, health, shields, kills, deaths, respawns and the score. This engine
  simulates only the local player (same physics as the bot match) and draws everyone else from snapshots, shown 100 ms behind
  the newest. A shot is a *claim* ("slot 3, AR, head") that the server checks: the weapon table, rate of fire, range, line of
  sight on the real map at any pose the victim held in the last ~300 ms, and spawn protection.
* **What that does and does not stop.** It stops impossible damage, firing faster than a weapon can, shooting through walls,
  shooting across the map with a knife, and speed / teleport hacks past a generous limit. It does **not** stop aimbots or
  wallhacks (the client knows where everyone is) and it trusts a client's reported position within the speed limit. A fully
  server-simulated match (the C server's model) is the esports-grade answer and is not this pass.
* Identity: a signed-in IDUNA token in HELLO resolves to the player's shankpit460 game-player row; no token (or a refused one)
  is a guest, named `Guest NNNN`.

## Tests

`make test-shankpit460` (or `scripts/test_shankpit460.sh`): build, **parity** (above), **engine** (module hygiene: 0 imports,
fixed memory, no two engine globals share a word; the screens; input; the bot match from PLAY to the end screen to REQUEUE;
the wire protocol against hand-built frames; determinism), **bot soak**. Then, elsewhere: IDUNA `go test ./internal/shankpit460`
(the lobby and referee, 20 tests incl. -race) and `./internal/http/handlers -run Shankpit460` (a real WebSocket); WOTAN
`scripts/shankpit460-net-test.mjs` (two real engines, a signed-in player and a guest, against the REAL IDUNA binary: pairing,
positions, a claimed hit moving health on both screens, a kill, a respawn, the kill limit, the end screen, requeue) and
`scripts/shankpit460-browser-test.mjs` (real headless Chromium, the same flow through the real page, pointer lock and all).

## Not done / not verified

* **Stats and the bot league** (second pass, founder's order). The lobby already knows each match's final standings per player
  and IDUNA has the game-player row; persisting them is the next step.
* Firefox, Safari, and phones (no touch controls); a physical gamepad (the Gamepad API path is written, not exercised);
  actually *hearing* the sound (it runs in headless without errors; nobody has listened).
* A match with more than two humans has been unit-tested on the server and the roster path is tested in the engine, but no
  one has played one.
* The weapon view-models and the player model are simple boxes. They read fine; they are not art.
