# FORGEDLEVEL — NORTHSTAR

**What this is:** the level/map builder that sits next to FORGEDINFACE. Founder real-time,
2026-11-03:

> "you know how vroid studio you are building a full interactable game affordance? cant we make a
> level builder in the same way to a certain extent? like paramertized materials etc for building
> to quickly build out maps" → "so it should be recursive we should be able to create a cinder
> block and then use a cinder block to create a wall creation tool use the wall creation tool to
> draw a basic structure use parena mods to make it parametized for helping the level builders"

That sentence is the architecture. Each clause is a rung, and v0 implements the rung, not a
picture of it:

```
material            ->  cinder block        ->  wall tool      ->  plan           ->  a mod
(parametric:            (an Assembly of        (lays Assemblies     (quantities       (parameterizes
 density, bevel,         material cells,        along a run:         over many         the wall tool;
 weathering)             with real cores)       joint, bond)         walls)            the builder's UI)
```

Real code: `PARENA/stdlib/forgedlevel/level.prn` (the four lower rungs) and
`PARENA/stdlib/forgedlevel/mods/cottage.prn` (the top rung — a real PARENA mod).

---

## What is real in v0

| Piece | What it does |
|---|---|
| `MaterialTable` | density (kg/m³), bevel (cm), weathering (0..1) per material id. Three real materials: concrete 2400, lightweight block 1900, timber 600 |
| `Library` | the recursive assembly store: one flat `cells` + `nested` buffer, `offset`/`span` per assembly — exactly what `Vec<Vec<Vec<Voxel>>>` compiles to, minus the per-node allocation |
| `block/cinder!` | a real cinder block: 4×2×2 cells of 10 cm = 40×20×20 cm, two cores through the depth on the bottom course (4 of 16 cells hollow = 25%) |
| `lib/prims`, `lib/volume-cm3` | the recursion: nested cells expand into the assembly they name, so a wall's material cost is computed from stored ids, never from a baked mesh |
| `wall/lay`, `wall/blocks-along` | the wall tool: block pitch, mortar joint, running bond. `n` full blocks fit iff `n*block + (n-1)*gap <= length` |
| `Plan`, `plan/add-wall!`, `plan/mass-kg` | quantities a level builder's HUD reads: blocks, half blocks, primitive cells, volume, mass |
| `mods/cottage.prn` | the mod surface: a `WallMod` value a designer swaps to change every wall built through it |

Units are centimetres and millimetres on purpose — this is a CAD-shaped tool, and the only
cm→mm conversion in the module lives in `wall/block-mm`.

**Verified, not asserted:** `make test-forgedlevel` runs `parena build` over both files, compiles
the result `gcc -std=c99 -Wall -Wextra -pedantic -Werror`, and runs hand-derived checks:

```
materials=1 block=1 quantities=1 wall=1 plan=1 nested=1 mod=1
```

Every number in those checks is derived on paper first: a 4 m run at 400 mm block + 10 mm joint
leaves 320 mm slack; 3 courses is 27 full blocks; running bond offsets one course, which in real
masonry is two *half* blocks, not one extra block; one cinder block is 12 solid cells of 1000 cm³
= 12000 cm³; the wall is therefore 0.336 m³ and weighs 806.4 kg in concrete; 27×12 + 2×6 = 336
primitive cells.

Two bugs the tests caught that the build could not:

1. `wall/block-mm` returned the block width in **cm** (40) while calling it millimetres, which
   made every wall count in the module wrong by 10×. Caught by the hand-derived
   per-course assertion, not by a compile error.
2. A `{:length-mm length-mm : courses courses ...}` literal — a space *after* a key's colon — is
   lexed as a bare `:` token plus two names, so the parser reported "odd number of forms" about a
   literal that is visibly an even number of forms. See the constraints table.

---

## How the recursion is kept honest

An assembly cell either holds a material id (`>= 0` in `cells`) or names another assembly
(`>= 0` in `nested`). The two are separate parallel arrays, not one signed field: `material 3`
and `assembly 3` being the same number is exactly the kind of ambiguity that becomes a loader bug
later.

`lib/add!` hands out ids in creation order, so an assembly can only nest assemblies that already
exist. **A cycle is unexpressible rather than merely detected** — the same invariant the 20-bone
skeleton uses for `parent < child`. The walkers still carry a depth bound (32) so a future
importer that breaks that invariant fails closed with `-1` instead of blowing the stack.

---

## The design transcript this came from was wrong, and was not built

The pasted design used syntax the real compiler does not have: `(Vec (Vec (Vec VoxelNode)))`,
`(defenum VoxelNode (Empty Void) ...)`, `f32/from_u8`, struct-field dot access
`(deref cell).extrusion_height`, `dotimes`, and `@ [R_nbr, R_out]` region lists. Everything here
is written against the actual `parena build` surface.

It also leaned on two things this repo deliberately does not have yet: GLTF/FBX mesh loading, and
a WASM/WebGL host with zero-copy vertex blitting. Both are real future work (FORGEDINFACE already
owns the GLB container reader and the skinning pass), not something v0 pretends to do.

---

## Real VS0 compiler constraints found building this

New in this pass (the FORGEDINFACE ones are recorded in `stdlib/forgedinface/rig.prn`'s header):

| Constraint | Symptom | Workaround |
|---|---|---|
| Space after a map key's colon | `{:a x : b y}` lexes `:` as its own token → "odd number of forms" on a visibly even literal | no space after `:` |
| `>` in a function name | `i32->f64` emits `i32_>f64`; gcc sees an implicit declaration | name it `i32-to-f64` |
| Cross-module call written with the full module id | `(forgedlevel/level/wall/lay ...)` emits `forgedlevel_level_wall_lay` while the definition is `wall_lay` | import, then call unqualified: `(wall/lay ...)` |
| `get-field` on a cross-module call's result | `get-field: 'void *' ... isn't a registered defstruct type` | exported accessors (`wall/per-course`, …) |
| Addressing a call result | `&(wall/params ...)` → `lvalue required as unary '&' operand` | bind it in the `let` first |
| `short` as a binding name | C keyword | `stub` |

Confirmed working (not obvious from the FORGEDINFACE notes): self-recursion is fine, a `loop`
*can* be bound in a `let`, nested `loop`/`recur` composes, and `set!` on a `&mut` struct field
mutates in place.

---

## Phases

Phase 0 — **done** (this pass): materials, recursive assembly library, cinder block, wall tool,
plan quantities, one real mod, 7 checks.

Phase 1 — **floor-plan painting + extrusion.** Draw a 2D plan in structural brushes, read the
boundaries, identify intersections, extrude with a roof pitch slider. This is the VRoid "paint on
the 2D canvas" affordance translated to a map, and it is the next thing a level designer actually
touches.

Phase 2 — **adjacency / joinery.** The topological graph: timber into concrete gets pegs, block
into block gets a flush joint, corners get trims. `Library` already carries neighbour lookups
(`lib/cell`, `lib/nested` are grid-indexed), so this is a decision pass over those, not new
storage.

Phase 3 — **mesh emission.** One assembly → vertices/UVs/bevels. FORGEDINFACE already solves the
hard part (skinning a low-poly mesh into world space), and the bevel radius is already a material
field waiting for a consumer. Output feeds FORGEDINFACE's `skin/deform` for animated assemblies.

Phase 4 — **mechanical assemblies.** FK/IK joints on cells (a piston, a gate), reusing
FORGEDINFACE's `rig/solve`. Then the GOLDENBAND actuator loop: observation vector, PD targets,
torque-speed saturation from *real* data sheets (Damiao / Maxon / TQ-Systems are the named
candidates), and mass/inertia recalculated when a builder swaps a material — which is exactly why
density is a material field in v0.

Phase 5 — **3D-print vertical integration.** SDF fusion over the recursive voxel space to get a
watertight manifold (game meshes are not printable: overlapping shells, interior walls), plus
print-profile tolerances (nozzle width, joint clearance, shell thickness) and a freeze-frame pose
export.

Phase 6 — **serialization + host.** `.gfd` (or an equivalent tight binary) of *parameters, not
vertices* — the whole map is a few kilobytes of ids and sliders — then LZ4 (this monorepo's
default codec), then the WASM/WebGL host. FORGEDINFACE's browser host is the same host.

---

## Open questions

- **`forgedlevel` vs `mapbuilder`.** `STDLIB.md` designs `mapbuilder/layout` and
  `mapbuilder/template`; neither has source. This module is the first real slice of that intent
  under the `forge` naming family FORGEDINFACE established. Whether `mapbuilder/*` should be the
  real home (and this a rename) is a naming decision, not a technical one.
- **Grid topology.** A uniform cube grid is the Minecraft half of "minecraft meets real CAD"; the
  CAD half wants non-uniform cells and bevels that survive subdivision. v0 keeps uniform cells and
  puts the roundness in the material's bevel field.
- **Who owns placement.** A cell currently stores a material id; a placed *assembly instance*
  (position + rotation + params) is a different thing from a nested definition. Phase 2/3 will
  need that distinction, and picking it late is how level formats end up with two ways to say the
  same block.
