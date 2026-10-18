# Bedrock Racers web (PARENA)

A Rocket League-style Quidditch esport in your browser: **four teams of three rocket cars** in one stadium, **one ball** (the
Quaffle) and **four upside-down corner goals**. The cars are Tesla Roadster-style and use air as their mana. **The whole game is
PARENA compiled to wasm32** (no allocator, no imports, a fixed 8 MB, about 215 KB), plus a page host that is PARENA compiled to
TypeScript. Same shape as BRAWLPIT web (`../brawlpit/`) and shankpit-460 web (`../shankpit460/`).

Founder real-time, 2026-10-18: *"MAKE IT NICE VERY TOUGHTFUL VERY DEMURE"*, *"make the ball magnet to the cars, remember its like
quidich, add a control for the magnet"*, *"let the ball be thown and passed like NBA street vol 2"*, *"ON FIRE WITH REALLY DRAMATIC
LIGHTING EFFECTS"*, *"AND ADD REALLY LIKE STREET TRICKS ... like jukes and stuff"*, *"MAYBE DIFFERENT CARS HAVE DIFFERENT ABILITIES"*.
Read `HANDOFF.md` for what is done and what is next.

## Play it

```bash
scripts/build_bedrockracers_web.sh out/       # out/index.html + bedrockracers.wasm + br-host.js (+ the .ts)
python3 -m http.server -d out 8000            # then http://localhost:8000/
```

| | keyboard / mouse | gamepad |
|---|---|---|
| throttle / brake | `W` / `S` | RT / LT |
| steer | `A` / `D` | left stick |
| lift (hold) | `Space` | A |
| boost | `Shift` | RB |
| **magnet (hold to pull in and carry, let go to throw)** | `F` or **right mouse** | **B** |
| pitch (down arrow = nose up, like a flight stick pulled back) | `Down` / `Up` | left stick down / up |
| roll | `Right` / `Left` | right stick |
| strafe | `E` / `Q` | d-pad right / left |
| fans off | `C` | LB |
| scores | `Tab` (hold) | |
| pause | `Esc` | Start |
| ball cam | `V` | Y |

The title picks your team (Left/Right, A/D, d-pad or click) and the bots' skill (EASY / NORMAL / HARD); `Enter` / PLAY drives that
team's first car, `B` / WATCH sets twelve bots going under a broadcast camera. The cursor is never captured: the keys and sticks
drive, the right mouse button is the magnet.

## The game in one paragraph

Hold the magnet near the ball and it is pulled into your cradle (17 ahead, 9 up) and carried; let go and it is thrown along your
nose at your velocity plus 260. A team-mate who is holding their own magnet within 28 degrees of your nose and 280 units away is a
pass target: the throw is led to them. Bumping a carrier loose steals the ball. A ball carried or thrown into a shaft above the
ceiling at one of the four corners is a goal (a carried one is a dunk); the bread-and-butter shot is a hard throw at a corner on the
run, which climbs the curved corner and drops up into the shaft. Be in on two of your team's goals in a row and you are **ON FIRE**
for 45 s: a full tank, +35% throw speed, magnet range 51. Air is the fuel: lift and boost spend it, the road refills it, nothing
refills while you carry.

## What is where

| file | what |
|---|---|
| `web/br_mem.prn` | the linear-memory map, accessors, the globals (`g-*`), math |
| `web/br_fields.prn` | **generated** by `tools/br_fields_gen.py` -- car / ball record accessors. Never hand-edit; add fields at the END of a list and regenerate |
| `web/br_arena.prn` | the arena as a signed-distance field (a rounded box), the four towers, the floor texture (cool slate, a neutral road, rings baked as 3x3 coverage) |
| `web/br_phys.prn` | the rocket car, the ball and the magnet: two 8 ms substeps per 16 ms tick |
| `web/br_rules.prn`, `br_fire.prn` | the match (goals, kickoffs, overtime, the end) and ON FIRE |
| `web/br_ai.prn`, `br_league.prn` | the bots (a commander, a ball predictor, a shot planner, a thrust-vector motor); `br_league.prn` is a stub for the CHAMPION level |
| `web/br_sim.prn` | the driver: command bytes, the replay log, the 16 ms tick |
| `web/br_fx.prn` | the cosmetics: particles, flames and the **dynamic light table** the renderer reads |
| `web/br_gfx.prn` | 2D primitives and the bitmap font |
| `web/br_3d.prn` | the z-buffered perspective rasteriser (Gouraud, textured, near-plane clip, bilinear floor) |
| `web/br_light.prn`, `br_mesh.prn`, `br_view.prn` | per-vertex light, every mesh (arena shell, towers, car, the stitched-leather Quaffle), drawing the world |
| `web/br_cam.prn` | chase / ball cam / broadcast spectator / title orbit / fixed |
| `web/br_input.prn`, `br_hud.prn`, `br_ui.prn` | keys and pads -> commands; the HUD and the Tab board; the title, pause and results screens and the frame |
| `web/br_main.prn` | the exports the page calls, and the `br-t-*` test hooks |
| `web/br_host.prn` | the page host (PARENA -> TypeScript -> `br-host.js`): instantiate, blit, forward input, labels |
| `web/MODULES` | the build order |
| `HANDOFF.md` | the founder's rules for this work, what is done, what is next |

## Tests (all from the repo root; `LLVM_TOOLCHAIN_ROOT=/` where system LLVM 18 is installed)

```bash
scripts/build_bedrockracers_wasm.sh /tmp/br.wasm
node tests/br_physics_test.mjs /tmp/br.wasm      # the car and the world
node tests/br_magnet_test.mjs  /tmp/br.wasm      # catch, carry, throw, pass, steal, dunk, the resets, the replay
node tests/br_fire_test.mjs    /tmp/br.wasm      # ON FIRE: rules, particles, the light table
node tests/br_engine_test.mjs  /tmp/br.wasm      # the client: hygiene, host contract, screens, input, camera, HUD pixels, determinism, budget
node tests/br_bot_soak.mjs     /tmp/br.wasm 6 3  # bots: the last line is the sim baseline (tests/br_soak_baseline.txt)
node tests/br_human_player.mjs /tmp/br.wasm 1    # NOT in the gate (minutes): a keyboard-only player scoring against EASY bots, reproducibly
node tests/br_shot.mjs /tmp/br.wasm shots "frames:30;shot:title;tap:13;frames:300;shot:kickoff"   # PNG screenshots from the framebuffer
# a real browser: the static bundle served and driven in headless Chromium
scripts/build_bedrockracers_web.sh /tmp/br-web
NODE_PATH=<dir with node_modules/playwright> PLAYWRIGHT_BROWSERS_PATH=<browsers> node tests/br_browser_test.mjs /tmp/br-web [shots-dir]
```

`make test-bedrockracers` runs all of this except the human-player script (and the browser test unless `NODE_PATH` / `PLAYWRIGHT_BROWSERS_PATH` are set).

**The gate before any push**: the wasm builds with 0 imports; physics 21/21, magnet 37/37, fire 40/40, engine and browser green;
**and the soak line is byte-identical** to the one from the commit before the change (a render-only change must not move a goal).
The baseline from `d0ac58b` (seeds 1-6, level 3, 15000 ticks) starts `{"seeds":6,"first_seed":1,"level":3,"minutes":24,"goals":63,
"own_goals":0,"touches":1609,...}`; held-out seeds 7-18 give 109 goals, 0 own goals.

## Rules this code lives by

* **Drawing only READS the sim.** A match drawn every tick ends in the same car and ball bytes as one that is never drawn
  (`br_engine_test.mjs` runs both for 700 ticks and compares them). Cosmetic randomness comes only from `fx-rand`. The HUD timers
  that count down (`g-fire-ann-t`, `g-banner-t`) tick in `fx-tick!`, the cosmetic step.
* **No allocator, no imports, no growth.** Every record is a fixed-address array; the build fails on an allocator symbol, a
  `memory.grow` or any import. `br_engine_test.mjs` also checks no two `g-*` words share a slot (they are claimed by hand across files).
* **The page JS is generated.** There is no hand-written JavaScript in the product; `br_host.prn` is the only page code.
* PARENA quirks that bite: float literals default to F64 (annotate F32 locals); a `defn` body is ONE expression (wrap several in
  `do`); a Unit branch ends in `unit`; a nested `dotimes` whose inner bound is a call fails; there is no `ignore`; use `b2i` for
  booleans; `emit_llvm: too many locals in one function` means split the function (it hit `fill-tri!` and `span-t!`).

## Honest status

Verified in Node and headless Chromium only (the keyboard, mouse and gamepad paths were driven synthetically; no physical pad was
attached). Not on Firefox or Safari, not on a phone (no touch controls). **No audio.** **Local play only**: no IDUNA sign-in, no
stats, no matchmaking; bot levels are EASY / NORMAL / HARD (the CHAMPION level and its league trainer are not built). Street tricks
and jukes, and different car classes, are not started. Frame time measured in Node on a shared cloud vCPU: median about 8.4 ms, p99
about 10 ms for the sim, eleven bots and a 640x360 software render; the smooth (bilinear) floor costs about 2.2 ms of that.
