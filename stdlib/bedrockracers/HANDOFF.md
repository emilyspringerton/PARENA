# Bedrock Racers handoff (read this first)

Rewritten 2026-10-18 by the Claude session that finished the renderer, the page, the HUD and the screens (it replaces the note the
session before it left when its usage ran out). `README.md` next to this file says what is where; this file says what is done, what
is next, and the rules. Everything below was checked when it was written. Re-check anything you are about to rely on.

## UPDATE 2026-10-21: online multiplayer is SHIPPED (read this, then the evening section below for where things are)

IDUNA `main` `d2684db` (the `wip/bedrockracers-online` branch merged onto current main, deployed; `/api/v1/bedrockracers/status` says
`configured: true`) and WOTAN `main` (`bedrockracers-page` fast-forwarded, plus `a961b1c`) are both live, IDUNA first. The founder said
"do all of it" for the whole sequence, including the WOTAN push. What changed on the way:

- **The referee wasm in IDUNA was stale.** `IDUNA/internal/bedrockracers/bedrockracers.wasm` differed from the browsers' module in two
  net functions (`net_check_fail_bang`, `net_frame_bang`, 20 bytes: the ERROR-6-leaves-the-room change, PARENA `c5fa4b9`). It is now the
  WOTAN `bedrockracers-page` build (sha256 `bc8b7d72...`); the golden hashes did not move. If you rebuild, copy the new wasm to BOTH repos.
- Checked on the merged tree: `go build ./...`, the `bedrockracers` and `-run Bedrock` handler tests, and WOTAN's two-Chromium online
  test 10/10 against a binary built from it. The wider IDUNA `handlers` run has the same 9 failures on clean `main` (draft runs, guest
  lore name / upgrade, redeem refill, nock door scripts, identity admin merge): they are not this work. One run also failed
  `TestBedrockRacersPlay_RatedMatchIsRecordedAndShownInTheStats` while another test suite was hogging this 2-core box; 0 in 23 reruns.
- IDUNA's `IDUNA Construct` workflow has failed on every push since 2026-10-17, before this merge.
- Smoke it live after any deploy of either side: `scripts/bedrockracers-live-smoke.mjs [origin]` in WOTAN (two real browsers).
- Toolchain on a fresh box with no root: Go and Node tarballs into `~/.local`, Playwright + its Chromium into a scratch dir, and for the
  four missing system libs (`libatk-1.0`, `libatk-bridge-2.0`, `libatspi`, `libXcomposite`) `apt-get download` + `dpkg -x` into a scratch
  dir and `LD_LIBRARY_PATH` (no sudo needed).
- Still open: WOTAN ladder / Matches / profile pages; Firefox, Safari, phones, real latency, 3+ humans in a room; the tricks, car classes,
  audio and CHAMPION-trainer items below.

## UPDATE 2026-10-18 (evening): online multiplayer was BUILT and TESTED, NOT DEPLOYED then (shipped, see above)

Written by Claude Sonnet 5.5 when the founder moved game dev to an open-weights model. Everything below this section still holds; this
section supersedes the "Next" list where they differ.

**Where the online work is** (three repos, none deployed):

| Piece | Where | State |
|---|---|---|
| Engine side (wire protocol, tick queue, drop-to-bot, server exports, ERROR 6 -> leave room) | PARENA `main` (`7e902de`, `c5fa4b9`) | pushed; `br_net_test` 44/44 |
| Page host: guest/SSO identity, WebSocket, NETIN/NETOUT pump, status pill for online states | PARENA `main` (`stdlib/bedrockracers/web/br_host.prn`, generated JS) | pushed |
| IDUNA server: lockstep hub/room, wazero referee, recorder, 4-team Elo, handler `/api/v1/bedrockracers/{play,status,matches,replays,players,leaders}`, migration `202610180001_bedrockracers_web.sql`, games registry row | IDUNA branch **`wip/bedrockracers-online`** (`07736c5`) | pushed, **not on main**; its own tests pass (`go test ./internal/bedrockracers ./internal/http/handlers -run Bedrock`) |
| WOTAN page + online build + `scripts/bedrockracers-online-test.mjs` | WOTAN branch **`bedrockracers-page`** (`e673f26`), already merged up with main | pushed, **not on main** |

**Proof it works:** `scripts/bedrockracers-online-test.mjs` in WOTAN: two real Chromium pages as guests + the real IDUNA binary: both seated in
one room, ~12 s of play with the server's 250-tick hash checks passing, a drop-to-bot, 10/10. Run it with Playwright in a `/tmp` scratch dir
(see the traps below) and `GOWORK=off GOMODCACHE=/tmp/gomod GOCACHE=/tmp/gocache GOFLAGS=-modcacherw`.

**To ship (the one thing left):** founder rule is *ask before pushing WOTAN main*; IDUNA first, then WOTAN: (1) merge `wip/bedrockracers-online` to IDUNA
`main`, let it deploy, check `/api/v1/bedrockracers/status` says `configured: true`; (2) merge `bedrockracers-page` to WOTAN `main`; (3) smoke it
live with two browsers. An old IDUNA just leaves PLAY as a bot match. After ANY engine change: `scripts/build-bedrockracers.sh` in WOTAN, commit the two
outputs, and copy the new wasm to `IDUNA/internal/bedrockracers/bedrockracers.wasm` (the referee must be the same module the browsers run; bump
`sim-version` in `br_net.prn` if the sim changed, and regenerate the golden hashes with `tests/br_golden.mjs`).

**Not done / known gaps:**
- WOTAN ladder, Matches and profile pages (IDUNA's stats routes exist and are tested; no page reads them).
- 8 failures in IDUNA's wider `handlers` test run (guest lore name, redeem refill, guest upgrade, draft runs, nock door scripts) were seen by the earlier
  session and NOT compared with a clean baseline; they look unrelated.
- Not verified: Firefox, Safari, phones, real network latency (lockstep adds a round trip to your own car), more than 2 humans in a room, the live cluster.
- Still not started: street tricks/jukes, car classes (both founder asks), audio, CHAMPION-level league trainer (`br_league.prn` stub).
- `tests/br_engine_test.mjs` frame-time guard is now 18 ms (14.3 ms median on this box; the old 12 ms came from a faster one).

## Founder rules for this work

- **Push at each step.** Every finished, tested stage gets committed and pushed straight to `main` of
  github.com/emilyspringerton/PARENA as soon as it lands. Don't save things up for one big push, and don't open PRs for this
  work. Unfinished work goes to a `wip/` branch so nothing is lost if a session ends.
- **Ask before pushing to WOTAN `main`.** A push to WOTAN `main` deploys to production (wotan.okemily.com). A *branch* does not
  (the workflow only runs on `main`), so finished WOTAN work waits on a branch for her go-ahead.
- **Commit as the repo's configured identity**, `Emily Springerton <emilyspringerton@gmail.com>` (monorepo `CLAUDE.md`, "Git Identity").
  Do not override it with `git -c user.name=...`. Subject `bedrockracers: ...`, a body with real measured numbers, a `session:
  <emily session current>` trailer, then the attribution lines. Add a `CHANGELOG.md` bullet under the day's `## YYYY-MM-DD`.
  Sync (commit + push) at the end of every turn: a box can vanish at any time.
- **Develop in the cloud container when you can.** Her Linux box (`/home/garybifrost`) catches up later; her Windows PC does not have the repo.

## The game, and what she asked for (her words)

A Rocket League-style Quidditch esport: four teams of three rocket cars in one stadium with four upside-down corner goals. The cars
are Tesla Roadster-style and use air as their mana. Her direction for the look: "MAKE IT NICE VERY TOUGHTFUL VERY DEMURE".

| Ask (2026-10-18) | Status |
|---|---|
| "make the ball magnet to the cars", "remember its like quidich", "add a control for the magnet" | **Done**: hold `F` / right mouse / pad B |
| "let the ball be thown and passed like NBA street vol 2" | **Done**: led passes, steals on bumps, dunks |
| "ON FIRE WITH REALLY DRAMATIC LIGHTING EFFECTS" | **Done and drawn**: the light table lights every vertex, drama dims the stadium, flames and particles, a goal flash, the ON FIRE banner and chip |
| Playable in a browser, screenshots | **Done**: `examples/bedrockracers_web` via `scripts/build_bedrockracers_web.sh`; the WOTAN page is on a branch (below) |
| "AND ADD REALLY LIKE STREET TRICKS" / "like jukes and stuff" | **Not started.** A juke is a quick side-jet dodge while carrying the ball that makes a tackle whiff |
| "MAYBE DIFFERENT CARS HAVE DIFFERENT ABILITIES" | **Not started** (car classes) |

Also queued from the original ask and **not built**: IDUNA SSO + stats (the "second pass", as for shankpit-460), the league trainer
for the CHAMPION bot level (`br_league.prn` is a three-line stub), audio (the SND ring exists in the memory map and nothing writes
to it).

## What is on main and how to check it

Since `d0ac58b` (the engine), in order: `3dfd52b` the renderer builds, `9a6c1f7` it runs, `21cb140` dynamic lighting, `2187ca0` the
page, `daf763f` the HUD and screens, `746eef8` the camera fixes and the look pass, then the gate script and docs.

```bash
make test-bedrockracers      # builds the bundle, runs every test, compares the soak line; the browser test needs NODE_PATH + PLAYWRIGHT_BROWSERS_PATH
```

The gate before any push: 0 imports; physics 21/21, magnet 37/37, fire 40/40, `br_engine_test.mjs` and `br_browser_test.mjs` green;
**the soak line byte-identical to `tests/br_soak_baseline.txt`** (regenerated from `d0ac58b`, whole line including `per_seed`; held-out
seeds 7-18 give 109 goals, 0 own goals). A render-only change must not move a goal. A change to the sim needs a new baseline and a reason.

**WOTAN:** the page is finished on branch `bedrockracers-page` in the WOTAN repo (not main): `/bedrockracers/`, `css/wotan-br.css`,
a `br` nav skin and Games-bar entry, a home card, `scripts/build-bedrockracers.sh`, `scripts/bedrockracers-browser-test.mjs` (11/11).
Merging it to main deploys it: ask her first. After any engine change, rerun `scripts/build-bedrockracers.sh` there and commit the
two outputs (the deploy image is built from tracked files only).

## Found and fixed while finishing it (so you do not trip on them again)

- `fill-tri!` hit `emit_llvm: too many locals`; split into `tri-grads!` / `fill-row!` with the gradients in scratch memory. `span-t!`
  needed the same care once the floor was filtered (helpers: `texel-for`, `bilerp-texel`).
- **Pausing did not freeze the view**: the ball kept rolling (spin ran on frame time) and the chase camera glided for ~3 s onto a car
  paused at speed. `g-cam-dt` is 0 while paused; a test pins "not one pixel changes".
- **After a kickoff the camera stayed upside down with its eye outside the arena** if the person had been on the ceiling
  (`sup-toward!` lerped through zero and kept the old vector at a half turn). `vec-toward!` now turns toward a perpendicular, the camera
  snaps when the car jumps > 60 units, the up is held in the air, and the safe-eye anchor is on the car's real up.
- The overtime and ball-reset banners could never show (`g-banner-t` was set and never counted down); `fx-tick!` counts it now.
- The amber floor camouflaged the amber team's cars; the floor is slate with a neutral road now. Rings thinner than a texel broke into
  arcs; they are baked as 3x3 coverage.
- The carried ball sat dead ahead and hid the road; the camera rises and backs off while carrying.
- Pitch convention: `Down` = pitch +1 = nose UP (a flight stick pulled back). The pad's stick-back is the same. Documented, not inverted.

## Next, in the order I would do it

1. **Merge the WOTAN branch** once she says go (it is finished and tested). Then IDUNA SSO + stats + a bot league, as for shankpit-460:
   persist per-match results, give the CHAMPION level a trainer (`br_league.prn`).
2. **Street tricks and jukes**, then **car classes** (her asks above). They change the sim: add the commands to the 8-byte command word
   (byte 6 has flags: boost 1, fans-off 2, magnet 4; room for more), give the bots a counter, and expect a new soak baseline.
3. **Audio**: fill the SND ring from the sim's events (catch, throw, pass, goal, fire) and play it in `br_host.prn` like `sp_host.prn`.
4. Reach: touch controls, Firefox and Safari, a physical pad, a frame-time guard (the smooth floor costs ~2.2 ms of a ~8.4 ms frame;
   the host can time `br_frame` and drop the floor filter on a slow machine).
5. **EASY is not easy for a naive player -- her call.** `tests/br_human_player.mjs` (keyboard events only, no hooks) scores against EASY bots,
   reproducibly (seed 1, tick 12940), but only in 1 of 7 four-minute matches (seeds 1-7): the bots swarm the carrier and touch most
   throws in flight (clean, well-aimed throws from 220-260 units out hit the tower and still did not go in when a bot clipped the ball).
   A throw has to be within about +-4 degrees of the corner's apex at 300 units (+-2 at 600), speed above ~450 hardly matters. If EASY
   should be beatable by a beginner, tune level 1's genome in `br_ai.prn` (the soak baseline is level 3, so it will not move) and rerun
   that script. Jukes (a dodge while carrying) would also help a carrier, which is one more reason to build them.

## Environment traps on this box (found 2026-10-18)

- `/home` is ~100% full. Each screenshot you *read* is stored in the session transcript (~100-200 KB), which is what ate it. Pack git's
  loose objects (`git repack -d`, non-destructive) to get megabytes back; look at fewer, smaller images.
- Playwright and Chromium are not installed here and `/opt/pw-browsers` does not exist: install them into a scratch directory on `/tmp`
  (`npm_config_cache=<scratch> npm i playwright`, then `PLAYWRIGHT_BROWSERS_PATH=<scratch> npx playwright install chromium`), never `/home`.
- `pkill -f <pattern>` kills your own tool call when the pattern is in its command line (exit 144). Use `pkill -f '[p]attern'`.
- `emily apples post` needs the port-forward and the EMILY-PRIME secret from the pod (memory: `reference_post_apple_from_this_box`).

## Mechanics reference (ruleset 3)

**Physics:** two 8 ms substeps per 16 ms tick. The arena is a rounded-box SDF (half extents 520, height 240, fillet 70). Goals are
shafts of radius 40 above the ceiling at (+-450, +-450); goal-y is 244 and the top is 340. Commands are quantised to 8 bytes per human
per tick. Byte 6 holds flags: boost = 1, fans-off = 2, magnet = 4.

**Magnet:**
- The cradle sits 17 ahead and 9 up.
- The pull is 1400, fading to 0 at range 34.
- A catch needs the ball within 9 and under 170 relative speed. A team-mate's pass allows 260.
- Throws go at car velocity + 260 along the nose.
- A pass goes to a team-mate who is holding their magnet, within 28 degrees and 280.
- There is a 0.7 s re-catch lockout, and an auto-throw after 313 ticks.
- No air refill while carrying, and a speed cap of 378.
- An enemy bump closing faster than 40 knocks the ball loose.

**ON FIRE:**
- You catch fire after being in on two of your team's goals in a row, and it lasts 45 s.
- It gives a full tank, +35% throw speed and magnet range 51.
- It ends on another team's goal, a steal, an own goal or the timeout.

**The scoring window** (measured with the bots' own what-if, `br_t_whatif_fine`, on a flat throw at the +x +z corner): a
throw along the floor diagonal at 350+ units/s goes in from up to 450 units out, at 450+ from up to 800; the aim has to be within about
+-4 degrees of the diagonal. Distance hardly matters, speed and aim do.
