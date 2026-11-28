# WORLDMAP contract (v1)

One map format and one rules module shared by **SHANKPIT** (the client, C/SDL2/wasm) and **GoblinFoxDragon** (the server, Go).
GFD is the server, SHANKPIT is the client; this is the seam between them.

## 1. Format: `.wmap`

A strict superset of shankpit-460's `.map` (`packages/common/map_loader.h`): every existing `.map` is a valid worldmap with one implicit
zone (id 0, kind `arena`). Plain text, one entry per line, `#` starts a comment.

```
worldmap 1                                   # header, first non-comment line (absent = legacy .map)
zone   <id> <kind> <name>                    # kind: town | field | dungeon | arena. Starts a zone; entries below belong to it.
box    <x> <y> <z> <w> <h> <d>               # as .map: centre + full size. Box 0 of a zone is its floor slab.
spawn  <x> <y> <z>
poi    <name> <x> <y> <z>
building <id> <label> <x> <z> <w> <d> <h> <tags...>   # a footprint; tags: enterable civic market brush stadium warehouse financial
npc    <id> <x> <z>                          # a named NPC stands here (GFD's npcDef.ID)
exit   <dir> <dest_zone>                     # MUD-style adjacency (north south east west up down); GFD's `exits` table
portal <x> <z> <r> <dest_zone> <dx> <dz>     # walk-in circle -> lands at (dx,dz) in dest_zone
crystal <id> <x> <z> <r> <dest_zone> <dx> <dz> <yaw> <cost> <total_ms> <commit_ms> <prompt...>
                                             # telecrystal: stand in the ring, press G; the jump fires at commit_ms
bound  <n>                                   # |x|,|z| <= n (SHANKPIT: 420)
```

Units are engine units. Zone ids are GFD's (town = 4, fields 0-3, TRAPX 200-207).

## 2. Rules: `wm_rules.prn`

Scalar, stateless, heap-free PARENA. Compiled by BURROW to **Go** (the server) and to **C** (the client; wasm via PARENA's LLVM target).
Covers: zone kinds, `job-change-allowed` (town only, never in combat), the combat-travel gate (742 ms, d20, <= 4 fails), `point-in-footprint`,
`point-in-circle`, `within-bound`, and the telecrystal cast (`crystal-in-ring`, `crystal-still-in-ring` with 1.5 of slack, `cast-commits`, `cast-interrupted`; lifted from apps/lobby's `telecrystal_tick`).

The host owns randomness and time: the rules take the roll as an argument.

## 3. Proof

`make test-worldmap` (`scripts/test_worldmap_parity.sh`) builds the module to both targets, runs one vector table through each and requires
identical, hand-derived output. GFD's Go build vendors the generated file (`apps2/mud/internal/burrowgen/wm_rules_gen.go`), regenerated with
`burrow build` and guarded by a test that fails when it drifts from the source.

## 4. Who does what

| | GFD (server, Go) | SHANKPIT (client) |
|---|---|---|
| owns | zones, mobs, characters, combat, the authoritative rules | rendering, input, the town scene, the menu |
| reads | `.wmap` (zones, exits, NPCs, buildings) | `.wmap` (boxes, buildings, portals, spawns) |
| runs | `wm_rules` as Go | `wm_rules` as C/wasm (prediction only; server decides) |

GFD is a top-level menu item in SHANKPIT; the Android build ships as its own app, **DragonsNShit**.
