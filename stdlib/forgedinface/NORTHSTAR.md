# FORGEDINFACE — the character builder

**Status (2026-11-03): v0 rig core + morph targets are real, built, and tested.** `stdlib/forgedinface/rig.prn`
compiles with `parena build`, and `make test-forgedinface` builds it under
`gcc -std=c99 -Wall -Wextra -pedantic -Werror` and runs 23 hand-derived assertions in five checks (skeleton, skinning, proportions, GLB, morphs), all
passing.
Nothing renders yet — there is no mesh importer past the GLB header, no host, and no editor.

Founder framing that started this: free VTuber avatars have no web-based builder like VRoid
Studio; build one in PARENA; start with a full bone rig while staying low poly so an agent can
handle the geometry; support standard VRoid files; full face and body customization; in-game
clothing creation later ("the sewa dress simulator"); parlay it into the online game's avatar
pipeline.

## What v0 actually is

| Piece | Real in v0 | Not yet |
|---|---|---|
| Skeleton | canonical 20-bone flat map, parent index < child index | fingers, toes, twist bones |
| Pose | bind translation + unit quaternion + per-bone scale | animation clips, IK |
| FK | one flat forward pass, world matrices as 16 F64 per bone | rotation from angles (quats only, see below) |
| Skinning | linear blend, 2 influences, weights sum to 1 | 4-influence, dual quaternions |
| Customization | masc/fem bias + shoulder/hip/limb scalars over ONE topology | per-bone twist/roll helpers |
| Morph targets | weighted per-vertex deltas (blend shapes), folded into the skinning pass | expression presets, `morph/ready?` enforced at call sites |
| Import | GLB/VRM container header + JSON chunk bounds | JSON node/skin/mesh decode, VRoid bone remap |
| Output | cooked vertex positions in arena memory | any renderer, any export format |

Run it: `cd PARENA && make test-forgedinface`.

### Why quaternions and not Euler angles

The real `math` package has `cos`, `sqrt`, `log` — **no `sin`** (`stdlib/math/math.prn`). A
quaternion→matrix conversion needs no trig at all, so bone rotation is expressed as a unit
quaternion `(x y z w)` supplied by the caller. That is also what dual-quaternion skinning would
need later, so the representation does not have to change to get there.

### The 20-bone map (locked, index = hierarchy contract)

```
0 Root   1 Pelvis   2 Spine   3 Chest   4 Neck   5 Head   6 Jaw   7 L_Eye   8 R_Eye
9 L_Clavicle  10 L_UpperArm  11 L_Forearm  12 R_Clavicle  13 R_UpperArm  14 R_Forearm
15 L_Thigh  16 L_Calf  17 R_Thigh  18 R_Calf  19 Weapon_Slot
```

Every parent has a strictly lower index than its children. That single invariant is what makes
`rig/solve` one flat loop instead of a recursive pointer tree, and it is why an agent can rewrite
the whole vertex buffer every frame without any traversal state.

## Corrections to the design transcript this came from

The thread that led here proposed code that does not compile in the real language. Recorded so
nobody re-derives it:

- `(defstruct CharacterConfig (hair_style i32) ...)` — fields need `:`, and the scalar types are
  `I32`/`F64`/`Bool`/`String`. There is no `F32` on the C target and no `(Array F32 3)` field type;
  fixed-size numeric arrays are `(Vec F64)` field buffers or `alloc` into an `Arena`.
- `js/call "Math.sqrt"`, `set-data`, `@ core`, `dotimes` — not the real surface. Host calls are
  `#target {:c (inline-c "...")}` (or the TS/Lua equivalents), struct fields are `get-field`, and
  the loop forms are `loop`/`recur` and `when`.
- `(match a ((Lit s) ...))` on an `I32`-payload `defenum` — a real, already-documented emitter gap
  (`stdlib/base4/pattern.prn`); v0 avoids it by using plain `I32` fields.
- "Zero-copy 8MB Wasm boundary with WebGL" — not reachable today: there is no `defn main` → C
  `int main` convention, no wasm32 host wiring for this, and no renderer. v0 outputs cooked
  positions in arena memory, which is what any host (C, TS, wasm) would read.

## Real compiler constraints this module had to work around

Found live while building it, all in the same family the stdlib already documents:

1. `Unit` only resolves as a return type when the body is a `#target` form. Non-`#target` helpers
   return `I32` instead.
2. A `#target` call returning void cannot be bound in a `let` (`void _0 = ...`). The push helpers
   are comma-expressions returning `1`.
3. A `loop` cannot be a `let` binding (emits an uninitialized temp → `-Werror=uninitialized`); it
   has to be a `let` *body* statement, the shape `linalg.prn`'s own `matmul` uses.
4. A single `let` with ~60 bindings loses its later bindings ("unknown identifier"). `self-test` is
   split into four small checks for exactly that reason.
5. `and` wants binary nesting for long chains (`stdlib/base4/pattern.prn` already does this).
6. `short` is a C keyword — a let binding named `short` emits `Vec short = ...` and fails.

## Phases after v0

1. **Morph targets — DONE (2026-11-03).** `MorphSet` + `morph/new`/`add-delta!`/`add-weight!`/`ready?`,
   applied inside `skin/deform-morphed` in the same loop that skins, so a slider move costs one
   pass and no intermediate buffer. Face customization is now real at the data layer.
2. **GLB body decode** — accessors/bufferViews/nodes/skins, so a VRoid export actually loads;
   then the VRoid→20-bone remap, with decimation for VRoid's high-poly hair.
3. **Host** — one renderer (C/SDL2 first, matching SHANKPIT's lineage) drawing cooked vertices.
4. **Clothing** — flat 2D panel pattern + seam relaxation + capsule push-out, per the transcript's
   own sewing design, all before any networking.
5. **Net sync** — quantized slider state + panel/seam recipe; the sewing sim is deterministic, so
   peers re-simulate rather than transfer vertices.
6. **Export** — a `.gfd`-shaped binary, reusing `stdlib/gfd` rather than inventing a container.

FORGEDINFACE is intended to become its own repo; until that upstream exists, this directory is the
real source of truth for its core.
