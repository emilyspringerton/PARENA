# Ada/GNAT features relevant to PARENA's memory-safety mission

**Status:** gap analysis, not implemented. Written 2026-08-24 in response to founder real-time
direction ("make sure our compiler has all of the features of ADA GNAT" → narrowed: "the ones
that serve our mission of memory management" → "and allow for different levels of memory and
compiler and instruction hacks" / "like we want to use certain instructions as hacks").

**Scope call, stated up front:** "all of GNAT" is not a real task — GNAT is one of the most
complete Ada compilers in existence (full generics, tasking, contracts/SPARK, exception handling,
decades of engineering). This doc is the narrowed version: which of GNAT's real features are
direct precedent for the two gaps PARENA's own `region.h` *already self-documents* as unstarted,
plus the low-level instruction/layout-control angle the founder tied back in. Nothing below is
implemented — this is the scoping pass that should happen before any of it is.

## The two gaps PARENA already names as its own

`src/region.h`'s own header comment is explicit about what domain 2 (the region analyzer) does
NOT attempt yet:

1. **The Return invariant** — `Region(Value) >= Region(Caller)`. Nothing stops a function from
   returning a value that references a shorter-lived region than its caller's own.
2. **The Move/ownership invariant** — PARENA's own syntax already has `!var`/`(move var)` as
   real, written-about linear-move sigils (NORTHSTAR.md's own "Ownership transfer" row), but
   `region.c` (259 lines, single-pass, checks only the *assignment* invariant via `walk()`)
   doesn't enforce either of them yet — a `!`-prefixed name is currently just naming convention,
   not a checked invariant.

Both are real, load-bearing gaps in the actual memory-safety claim PARENA's whole design rests
on, not cosmetic ones.

## Ada's real precedent for gap 1 — accessibility checking

Ada's **accessibility levels** are the closest existing precedent to PARENA's own region-rank
system, and predate it by decades. Every access (pointer) type and every access value in Ada
carries a compile-time-tracked accessibility level — conceptually the same idea as PARENA's
`:region/scratch` = 0 / `:region/buffer` = 2 rank table, just with more levels (one per nested
scope, not two fixed named regions). The rule GNAT enforces at compile time —
`Ada.Unchecked_Access`/plain `'Access` is rejected unless the target's accessibility level is at
least as deep as the access type's own — is *exactly* PARENA's `Region(Source) ⪰
Region(Destination)` invariant, just already generalized to arbitrary nesting depth instead of
two fixed ranks, and already covering the return-value case region.c doesn't yet: GNAT rejects a
function returning an access value pointing to a local (a shallower accessibility level than the
caller needs) at compile time, with no runtime check involved — the direct real-world
implementation of PARENA's own still-unimplemented Return invariant.

**What this means concretely for `region.c`:** the Return invariant isn't a new kind of check,
it's the SAME `walk()`-style assignment check already implemented, applied to one more syntactic
position (a `defn`'s own return expression against its declared `@ Region` annotation) instead of
only `let`/`with-arena` assignment sites. Real, scoped follow-up, not a new algorithm.

## Ada's real precedent for gap 2 — ownership/aliasing (SPARK's Pointer annex)

Base Ada doesn't check ownership/move semantics at all (its access types are ordinary aliasing
pointers) — the real precedent is **SPARK** (the formally-verified Ada subset GNAT ships a prover
for): SPARK's Pointer aspect and Global/Depends contracts track exactly the shape PARENA's own
`!var`/`(move var)`/`&var`/`&mut var` sigils are reaching for — linear ownership, checked
aliasing, "this value has moved, using the old name is now an error." SPARK proves this via a
separate flow-analysis pass over the same AST the compiler already type-checks, not a different
language — the same shape a PARENA `move.c` (or a new pass inside `region.c`) would take: walk
`with-arena`/`let`/`defn`-param bindings, mark a binding "moved-from" the first time it's passed
somewhere a `!`-sigil consumes it, and flag any later reference to that same binding.

**What this means concretely:** this is real, separate, more novel work than gap 1 (no existing
PARENA code does anything like it yet) — a genuinely new small pass, not an extension of `walk()`.

## The instruction-hacking / low-level angle

Founder tied the memory-management ask back to PARENA's original stated goal (NORTHSTAR.md: "an
environment where we can make games that let us develop instruction hacks that we can then bring
into the real world of real architectures like arm and x86"). GNAT's real, relevant tools here,
none of which PARENA has any equivalent of yet:

- **Representation clauses** (`for T'Size use N;`, `for T'Address use ...;`, `for T use record ...
  at mod N;`) — explicit, checked control over a type's exact bit layout and memory address, the
  mechanism Ada uses when a type has to match a specific hardware register/protocol layout
  exactly. PARENA's own `defstruct` has no layout-control annotations at all today (field order
  is whatever the source wrote, no explicit padding/alignment/bit-packing control).
- **`System.Machine_Code`** — real inline assembly, typed and region-checked at the boundary
  (arguments/clobbers are declared, not just a raw string escape). PARENA's own `#target`
  `(inline-c "...")` is the closest existing analog (already a real, working escape hatch — used
  extensively this session) but is C-level, not raw-instruction-level, and untyped/untrusted
  ("VS0 has no way to check it" per `emit.c`'s own comment on `#target`).
  - Real, narrow next step if this becomes concrete: a `#target {:asm (inline-asm "...")}` sibling
    to the existing `:c` key, following the exact same trusted-verbatim pattern `find_target_c_src`
    already established — small, mechanical addition to `emit_target_defn`, not a new subsystem.
- **`pragma Machine_Attribute`** — per-subprogram target-CPU-specific attributes (calling
  convention, interrupt handling, specific instruction-set hints). No PARENA equivalent.
- **`Interfaces`** (`Interfaces.C`, `Interfaces.X86_64`-style packages in some GNAT targets) —
  architecture-specific fixed-width integer/register types. PARENA's own type system doesn't have
  fixed-width integers at all yet (`I32` is the only integer type; the STDLIB.md gap list
  separately already names "no int-vs-float distinction" as a real, wider gap this session
  actually ran into directly, fixing it as a narrow per-case workaround in `regex/pcre.prn`
  rather than the real thing — see PARENA CHANGELOG 2026-08-24).

## Real, honest recommendation

Do not attempt "port GNAT" as a task. Two real, concretely scoped next steps exist if this
becomes a priority:

1. **Return invariant** (gap 1) — small, mechanical extension of `region.c`'s existing `walk()`,
   real Ada precedent already validates the approach. Cheapest, highest-confidence next step.
2. **Move/ownership checking** (gap 2) — a genuinely new pass, SPARK's Pointer annex is the real
   design precedent to study before writing it, not a `walk()` extension.

The instruction-hacking angle (representation clauses, inline asm, fixed-width integer types) is
real and connects directly to the founder's own stated PARENA mission, but is a different kind of
work (new type-system/emitter surface, not memory-safety verification) — flagged here as
connected, not folded into the same implementation pass as 1/2 above.
