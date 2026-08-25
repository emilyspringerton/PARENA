# NORTHSTAR — Ada/GNAT prior art for PARENA's memory/compiler mission

**Status:** north star, nothing implemented. Written 2026-08-24, founder real-time, full thread:
"make sure our compiler has all of the features of ADA GNAT" → narrowed: "the ones that serve
our mission of memory management" → "and allow for different levels of memory and compiler and
instruction hacks" / "like we want to use certain instructions as hacks" → "and on some level the
compiler should have a flag to turn that off" → "and pull any other useful ADA GNAT prior art" →
"and then prioritize all the features of ADA GNAT and what i discussed and beyond into a
northstar" → "like we can turn rng off at compile time if you think about it" → "wouldnt that
speed up a lot of things like games that might not always need real rng?" → "allowing us to run
shankpit on a casio calculator watch?"

**Scope call, stated up front, unchanged from the first pass on this:** "all of GNAT" is not a
real task — GNAT is one of the most complete Ada compilers in existence (full generics, tasking,
contracts/SPARK, exception handling, decades of engineering). This is the narrowed, prioritized
version: real GNAT/Ada features that are direct precedent for PARENA's own actual, self-declared
gaps, organized by priority, not an attempt to port Ada.

## Priority 1 — the Return invariant (region.c's own named gap)

`src/region.h` already documents this as unstarted: `Region(Value) >= Region(Caller)`. Ada's
**accessibility levels** are the real, decades-old precedent — the same invariant PARENA's
`:region/scratch`/`:region/buffer` rank table expresses, generalized to arbitrary nesting depth.
GNAT rejects a function returning an access value pointing to a shallower-lived local at compile
time, zero runtime cost — the real implementation of the exact check region.c doesn't do yet.
**Concretely scoped**: extend `region.c`'s existing `walk()` to check a `defn`'s own return
expression against its declared `@ Region` annotation, the same assignment-invariant logic
already applied at one more syntactic position. Cheapest, highest-confidence next step.

## Priority 2 — Move/ownership checking (region.c's other named gap)

PARENA's `!var`/`(move var)`/`&var`/`&mut var` sigils exist in NORTHSTAR.md's own syntax table
but aren't checked anywhere — currently naming convention, not an enforced invariant. Real
precedent: **SPARK's Pointer/ownership annex** (the formally-verified Ada subset GNAT ships a
prover for) — flow-analysis over the same AST the compiler already walks, marking a binding
"moved-from" the first time a `!`-sigil consumes it, flagging any later reference. Genuinely new
pass, not a `walk()` extension — real design study needed before writing it.

## Priority 3 — a real disable-checks flag (founder's own direct ask)

**"the compiler should have a flag to turn that off."** Direct match to GNAT's real
`pragma Suppress`/`-gnatp`: selectively (or globally) disable compile-time checks once a program
is trusted, without touching source. For PARENA this means a `--suppress=region,move` (or
`-gnatp`-style single flag) on `parena build` that skips `region_analyze()`/the future move-checker
entirely — real, useful for the "casio calculator watch" tier below, where every cycle of
compile-time-verified-but-runtime-identical code still costs something to GENERATE (extra
branches/casts the checker's own presence doesn't add, but a maximally-stripped emission path
might still want to skip for a tiny target). Small, mechanical: a CLI flag threaded into
`cmd_build`, guarding the existing `region_analyze()` call.

## Priority 4 — determinism as a compile-time restriction (RNG thread)

**"we can turn rng off at compile time" / "speed up games that might not always need real rng"
/ real answer already given directly: the bigger win is determinism, not raw speed** — lockstep
multiplayer (REDGARDEN/SHANKPIT's own real netcode) needs bit-identical RNG across every client;
replay/debugging needs reproducible runs. Real GNAT precedent: **`pragma Restrictions`** — Ada's
mechanism for eliminating whole categories of runtime behavior at compile time, CHECKED (a
violation is a compile error, not silently ignored), not just suppressed the way Priority 3's
flag suppresses a check. A PARENA `(restrict :no-true-random)` (module- or program-level) would:
(a) make any call to a true-entropy source a compile error, (b) let stdlib code branch to a
faster seeded-PRNG path when the restriction is active. Same real mechanism generalizes past RNG
to whatever other restriction categories turn out to matter (`:no-recursion`, `:no-dynamic-alloc`
for the tiny-target tier below — Ada's own `No_Recursion`/`No_Allocators` restrictions are the
direct precedent for those two specifically).

## Priority 5 — tiny-target tiers ("casio calculator watch")

Real GNAT precedent: **Ravenscar profile** / **Zero Footprint runtime** — restricted, minimal-
runtime build configurations GNAT genuinely ships for avionics-class and other extremely
resource-constrained embedded/bare-metal targets (no dynamic allocation, no unbounded recursion,
tiny fixed-size runtime support). Composes directly with Priority 4's restriction mechanism
(`:no-dynamic-alloc`/`:no-recursion`/`:no-true-random` together approximate a Ravenscar-style
profile) rather than being a separate feature — real, later work once restrictions themselves
exist, not scoped further here.

## Priority 6 — instruction-hacking / low-level control (the original PARENA mission thread)

Ties back to NORTHSTAR.md's own stated goal ("instruction hacks... bring into the real world of
real architectures like arm and x86"). Real GNAT tools, no PARENA equivalent today:

- **Representation clauses** (`for T'Size use N;`, `for T'Address use ...;`, bit-level record
  layout) — explicit, checked control over a type's exact memory layout. PARENA's `defstruct` has
  no layout-control annotations at all yet.
- **`System.Machine_Code`** — real, typed inline assembly (declared argument/clobber lists, not a
  raw string escape). PARENA's own `#target {:c (inline-c "...")}` is the closest existing analog
  (real, already used extensively this session) but is C-level and untyped/untrusted by design
  ("VS0 has no way to check it" — `emit.c`'s own comment). Real, narrow next step if this becomes
  concrete: a `#target {:asm (inline-asm "...")}` sibling key, same trusted-verbatim pattern
  `find_target_c_src` already establishes for `:c` — small, mechanical addition, not a new
  subsystem.
- **`pragma Machine_Attribute`** — per-subprogram target-CPU attributes (calling convention,
  interrupt handling). No PARENA equivalent.
- **`Interfaces`/fixed-width integer types** — PARENA's type system has only `I32` today; no
  fixed-width integer family at all. Connects to a real, WIDER gap this session already hit
  directly and worked around narrowly rather than fixed properly: `emit.c`'s own numeric-literal
  handling has no int-vs-float distinction at all (every literal is `double`) — see PARENA
  CHANGELOG 2026-08-24 (`regex/pcre.prn`'s `zero-i32` `#target` workaround). A real fixed-width
  integer family is downstream of fixing that more foundational gap first, not independent of it.

## Priority 7 — module namespacing (found live this session, real GNAT precedent exists)

Not from this thread directly, but real, connected, and worth recording here rather than losing
it: this session's `regex/pcre.prn` work found `emit.c`'s struct/enum registry is flat and
NOT module-scoped — `regex/nfa.prn` and `regex/pcre.prn` both define a struct named `Regex` with
different fields, and building both together silently shadows one with the other (worked around
by never compiling both in the same build, not fixed). Ada's real precedent:
**library units + `pragma Elaborate`/`Elaborate_All`** — Ada's own compilation-unit model
requires every name to resolve through its own package's namespace, with compiler-checked
elaboration (initialization) order across units. A real fix for PARENA's own gap: qualify every
registered struct/enum name by its owning `(module ...)` internally (`regex/nfa.Regex` vs.
`regex/pcre.Regex`), resolving a bare reference within the SAME module first, then via explicit
`(import ...)` — real, separate compiler work, not attempted here.

## What's explicitly NOT prioritized here

Full generics (`vec.prn`/`map.prn`'s own real blocker), tasking/concurrency (already has its own
real thread — see `GoblinFoxDragon/docs2/MOD_SURFACE_NORTHSTAR.md` §3a, federated process
operation / Erlang-BEAM north star, a different lineage than Ada tasking), and exception handling
(PARENA's `Result`/`Option` already cover the same ground `Result`-based error handling in
Rust/etc. does — Ada's own `exception`/`raise`/`when` model is a genuinely different mechanism,
not obviously better fit) are real GNAT features but don't serve THIS mission (memory
management/instruction-level control) specifically — flagged as out of scope for this doc, not
forgotten.
