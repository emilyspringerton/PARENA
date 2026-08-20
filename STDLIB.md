# PARENA Standard Library — design

**Status:** Design only. Nothing implemented — VS0 (`parena-c`) doesn't have a `defn`/`import`
resolver yet, so none of this can run. This document exists so the package boundaries are decided
*before* the region analyzer and emitter get built against them, not invented ad hoc later.

Founder: "PARENA needs a standard library like the golang one." Go's stdlib is the reference
point, applied at the level that actually transfers to a region-typed, multi-target language:
small, single-purpose packages; consistent per-package naming (`pkg.Func`-style, here
`pkg/func`); few surprises; `io`/`os`/`strings`/`fmt`-shaped coverage before anything exotic.
What does *not* transfer directly: Go's stdlib assumes a GC and no region system, so every
function signature below carries real `@ Region` annotations, not decoration.

## Real constraint this design has to satisfy

Five stdlib calls already exist as **real, load-bearing code** in `NORTHSTAR.md`'s own core-idiom
and VS0 examples — copied from the source spec document, not invented for this design pass:

```clojure
(string/parse-i32 input)                          ; safe-parse-int
(io/write-string !file "Operation completed\n")    ; write-log-and-close
(io/close !file)                                   ; write-log-and-close
(log/info "Parsed integer:" val)                   ; safe-parse-int
(buffer/set-data buf-arena bad-str)                ; VS0 test.prn's own invalid-escape example
```

Every package below either contains one of these five verbatim (with a real, specific type
signature now) or is new — and every new package is justified against something VS0's own DoD
or NORTHSTAR.md's own examples already need, not spec-crept in.

## Dependency order

Founder: "planning any more stdlibs you need" → "then sort them in dep order" → "and build them
out" → "in parena" → (after `sdl2`/`editor`/`net` were added) → "and replan the whole deps
heirarchy." Full re-sort, every package planned this thread, topological by `import` (each
package appears only after everything it depends on) — this is the real build order for the
`.prn` source tree under `stdlib/`:

**Tier 0 — built-in, no `import` needed:** `core` (already core language forms) and `sdl2`
(founder: "SDL2 is built in" — the one package elevated to this tier despite being a real FFI
binding, not a language primitive).

1. `vec`, `map` — no stdlib dependencies (leaves)
2. `string`, `log`, `buffer` — depend on `core` only
3. `io` — depends on `string`
4. `array` — depends on `vec`
5. `linalg`, `stats` — depend on `array`
6. `expr` — depends on `map`, `string`
7. `regex/syntax` — depends on `string`, `vec`
8. `regex/nfa`, `regex/pcre`, `regex/posix`, `regex/glob` — depend on `regex/syntax`, `vec`
9. `dataframe` — depends on `array`, `string`, `vec`
10. `nn` — depends on `array`
11. `tokenizer` — depends on `string`, `vec`, `io`
12. `sort` — depends on `vec`
13. `net/tcp` — depends on `string` (leaf beyond that — raw sockets, no other package needed)
14. `net/udp` — depends on `string` (same tier as `net/tcp`, no dependency between the two)
15. `net/http` — depends on `net/tcp`, `map`, `string`
16. `grep` — depends on `regex/nfa`, `regex/pcre`, `regex/posix`, `io`, `vec`
17. `sed` — depends on `regex/pcre`, `io`, `string`
18. `awk` — depends on `regex/pcre`, `expr`, `vec`, `string`
19. `gfd` — depends on `string` only (world-state calls are FFI, not internal package deps)
20. `editor/plugin`, `editor/buffer`, `editor/events` — depend on `string`, `vec`
21. `editor/ui` — depends on `editor/events`, and *may* depend on `sdl2` once a shell is chosen
    (NORTHSTAR's own editor-shell question is still open — not resolved by this re-sort)

Real, honest limitation restated once here rather than per-package below: VS0 only has a working
*parser* (domain 1) — no region analyzer or C emitter yet (S189-13, domains 2-5 not started). Every
`.prn` file under `stdlib/` is real source, written in the language's actual documented syntax and
checked against the real `parena parse` command, but none of it compiles or runs yet — that
requires domains 2-3, which this pass does not build. "Built out in Parena" means *real, parseable
Parena source exists*, not *the standard library is executable*.

## Package list

### `core` — always in scope, no `import` needed

The handful of things every `.prn` file needs without asking for them, same role Go's
predeclared identifiers (`len`, `error`, `int`, `nil`) play.

- `(Option T)`, `(Result T E)` — the tagged unions `match` destructures (NORTHSTAR §"Zero-
  allocation pattern matching"). `Ok`/`Err`/`Some`/`None` constructors.
- `Arena`, `with-arena` — already core forms per NORTHSTAR's own "Declarations & bindings" and
  "Memory model" sections, not a package function, listed here only so this doc's own package
  list is complete.

### `vec` / `map` — the collection gap earlier drafts of this doc flagged but didn't design

Founder: "do any remaining dependency planning." Real, no-longer-speculative trigger: `expr`'s
`eval` (below, added for `awk`) takes `bindings : &Map`, and `regex`/`grep`/`awk`'s own
`(Vec ...)`-returning signatures throughout this doc assume push/get/len operations exist
somewhere — every prior draft left that as "Collections beyond `Vec`/`Map` literals... deliberately
left for whoever actually writes the first real program to ground against real need." That
program turned out to be this document's own `expr`/`awk` sections, so it's designed now, not
deferred again.

```clojure
; vec — no dependencies, generic over T
(defn new    [(dest : Arena @ Region)] : (Vec T) @ Region)
(defn push!  [(v : &mut (Vec T)) (item : T)])
(defn get    [(v : &(Vec T)) (idx : I32)] : (Option (&T)))
(defn len    [(v : &(Vec T))] : I32)

; map — no dependencies, generic over K/V
(defn new      [(dest : Arena @ Region)] : (Map K V) @ Region)
(defn get      [(m : &(Map K V)) (key : &K)] : (Option (&V)))
(defn set!     [(m : &mut (Map K V)) (key : K) (value : V)])
(defn contains [(m : &(Map K V)) (key : &K)] : Bool)
```

Both are as small as `[...]`/`{...}` literal syntax needs to become *usable* past a fixed literal
— exactly the same "minimum any language ships before its first real program" judgment `string`
used above, not a `Vec`/`HashMap`-standard-library's-worth of iterator adapters, sorted variants,
or capacity tuning. `array`'s `NDArray.data`, `dataframe`'s `Column`/`DataFrame`, `nn`/`tokenizer`/
`sort`'s `Vec I32`/`Vec T` parameters, and every `regex`/`grep`/`awk` signature that returns a
`Vec` were all already assuming this package exists implicitly; this section makes that dependency
real instead of implicit.

### `io` — the two calls the source spec already uses, plus their obvious neighbors

```clojure
(defn write-string [(!f : FileHandle @ :region/task) (s : String @ :region/scratch)]
  : (Result Unit IoError) @ :region/scratch)

(defn close [(!f : FileHandle @ :region/task)]
  : (Result Unit IoError) @ :region/scratch)

(defn open [(path : String @ :region/scratch) (mode : OpenMode)]
  : (Result FileHandle IoError) @ :region/task)

(defn read-string [(!f : FileHandle @ :region/task) (buf : Arena @ :region/buffer)]
  : (Result String IoError) @ :region/buffer)
```

`write-string`/`close` take `!f` (linear) matching the source document's own
`write-log-and-close` example exactly — the region analyzer's move/ownership invariant is what
makes "write after close" a compile error, not a runtime check `io` has to perform itself.
`open`/`read-string` are the obvious symmetric operations no `io` package could ship without,
grounded in the same `FileHandle` type the source examples already assume exists.

**`io` extension — binary reads** (grounded in porting `gpt2-alpine-c`, below): its real weight
loader (`gpt2.c`'s `fread_or_fail`) does raw `fread(buf, sizeof(float), n, f)` — a checkpoint file
is just a flat sequence of `float32`s read straight into a buffer, no string decoding involved.
`read-string` alone can't express that.

```clojure
(defn read-floats [(!f : FileHandle @ :region/task) (n : I32) (dest : Arena @ Region)]
  : (Result NDArray IoError) @ Region)   ; reads n raw F32 values, same "region caller picks" idiom
```

Returns `array`'s own `NDArray` directly (1-D, `shape = [n]`) rather than a bare `(Vec F64)` —
model weights are immediately going to be reshaped and matmul'd, so handing back the type
`linalg` already operates on avoids a pointless intermediate conversion step.

### `string` — one real call, plus the minimum a "no arrays yet but has Strings" language needs

```clojure
(defn parse-i32 [(s : String @ :region/scratch)]
  : (Result I32 ParseError) @ :region/scratch)

(defn length [(s : String @ :region/scratch)] : I32)
(defn concat [(a : String @ Region) (b : String @ Region) (dest : Arena @ Region)]
  : String @ Region)
(defn split [(s : String @ :region/scratch) (sep : String @ :region/scratch) (dest : Arena @ Region)]
  : (Vec String) @ Region)
```

`parse-i32` is verbatim from the source spec (`safe-parse-int`'s own body). `length`/`concat`/
`split` are the minimum any language ships before its first real program can be written that
does more than log a fixed string — deliberately small, not a full `strings`-package-worth of
surface, matching "yolo like php" over speccing every function up front.

### `log` — one real call

```clojure
(defn info  [(msg : String @ Region) (args : &Any ...)])
(defn warn  [(msg : String @ Region) (args : &Any ...)])
(defn error [(msg : String @ Region) (args : &Any ...)])
```

`info` is verbatim from the source spec. `warn`/`error` are the obvious same-shape siblings —
not adding a full leveled-logging framework (formatters, sinks, structured fields) until
something real needs one.

### `buffer` — one real call, the arena-adjacent operations `Arena`/`with-arena` alone don't cover

```clojure
(defn set-data [(!buf : Arena @ :region/buffer) (data : String @ Region)]
  : (Result Unit RegionError) @ :region/buffer)

(defn get-data [(buf : Arena @ :region/buffer)] : (Option String) @ :region/buffer)
```

`set-data` is verbatim from VS0's own `test.prn` (both the valid and the deliberately-invalid
example call it). `get-data` is the obvious read-side counterpart — without it, `buffer` would
be a write-only package, which no real stdlib package is.

### `array` / `linalg` / `stats` — the numpy/scipy equivalent

Founder: "can we build scipy and numpy into the standard language of PARENA?" → "whatever the
equivalent would be" → "build building blocks as stdlib if you need to or if it helps" → "to keep
things nice and composible." Real design, not a wholesale port of numpy/scipy's own API surface —
those two libraries are themselves layered (numpy: the array type + memory layout; scipy: linear
algebra/stats/optimization/signal-processing *built on top of* numpy's array), and that layering
is exactly the "nice and composable" shape to reuse: three small packages, each usable alone,
each `import`able without pulling in the others.

**`array`** — the actual numpy-equivalent: one region-typed, contiguous N-dimensional buffer type
and the operations every layer above it needs. This is the foundation everything else in this
section is built from — `linalg`/`stats` never touch raw memory themselves, only `NDArray`.

```clojure
(defstruct NDArray
  (data  : (Vec F64) @ Region)   ; contiguous backing storage, arena-owned by whatever
                                  ; region the array itself was allocated in
  (shape : (Vec I32)  @ Region)  ; e.g. [3 4] for a 3x4 matrix
  (strides : (Vec I32) @ Region))

(defn zeros  [(shape : (Vec I32) @ :region/scratch) (dest : Arena @ Region)] : NDArray @ Region)
(defn from-vec [(data : (Vec F64) @ Region) (shape : (Vec I32) @ :region/scratch) (dest : Arena @ Region)]
  : (Result NDArray ShapeError) @ Region)
(defn get [(a : &NDArray) (idx : (Vec I32) @ :region/scratch)] : (Result F64 IndexError))
(defn set! [(a : &mut NDArray) (idx : (Vec I32) @ :region/scratch) (v : F64)] : (Result Unit IndexError))
(defn reshape [(a : &NDArray) (new-shape : (Vec I32) @ :region/scratch)] : (Result NDArray ShapeError) @ Region)
(defn add [(a : &NDArray) (b : &NDArray) (dest : Arena @ Region)] : (Result NDArray ShapeError) @ Region)
(defn mul-elementwise [(a : &NDArray) (b : &NDArray) (dest : Arena @ Region)] : (Result NDArray ShapeError) @ Region)
```

Every allocating function takes an explicit `dest : Arena @ Region` — same "caller picks the
region" idiom `io/open`/`string/concat` already use above, not a hidden global allocator the way
numpy's own C implementation has one. That's the actual place PARENA's version has to diverge
from numpy, not a cosmetic API difference: region typing means "where does this array live" is
part of every signature, not an implementation detail.

**`linalg`** — depends only on `array`, matches scipy's own `scipy.linalg` scope, not scipy's
full breadth:

```clojure
(defn matmul  [(a : &NDArray) (b : &NDArray) (dest : Arena @ Region)] : (Result NDArray ShapeError) @ Region)
(defn transpose [(a : &NDArray) (dest : Arena @ Region)] : NDArray @ Region)
(defn dot     [(a : &NDArray) (b : &NDArray)] : (Result F64 ShapeError))
(defn inverse [(a : &NDArray) (dest : Arena @ Region)] : (Result NDArray LinalgError) @ Region)
(defn solve   [(a : &NDArray) (b : &NDArray) (dest : Arena @ Region)] : (Result NDArray LinalgError) @ Region)
```

**`stats`** — depends only on `array`, matches scipy's own `scipy.stats` scope, deliberately
starting narrow (mean/std/summary statistics, not distributions/hypothesis-testing yet — those
are real, separate follow-up work once something actually needs them):

```clojure
(defn mean [(a : &NDArray)] : F64)
(defn std  [(a : &NDArray)] : F64)
(defn sum  [(a : &NDArray)] : F64)
(defn min  [(a : &NDArray)] : (Result F64 EmptyArrayError))
(defn max  [(a : &NDArray)] : (Result F64 EmptyArrayError))
```

**Real, honest limitation**: numpy/scipy's actual value is decades of vectorized, SIMD-tuned,
BLAS/LAPACK-backed numerical kernels — nothing above specifies *how* `matmul`/`inverse`/etc. get
implemented fast. A first real implementation would plausibly call out to a real BLAS/LAPACK C
library via the FFI block NORTHSTAR.md's own core idioms already show (`#target {:c (inline-c
...)}`), rather than hand-writing a naive triple-nested-loop matmul and calling it done — flagged
as a real follow-up decision, not resolved by this design pass.

### `dataframe` — the pandas equivalent, depends on `array` + `string`

Founder: "and pandas build pandas into the standard library." The one real thing that makes
pandas a different tool from numpy, not just numpy-with-more-functions: **heterogeneous, labeled,
tabular data** — a `DataFrame` column can be numbers or strings, columns have names, rows aren't
just an unlabeled index. That's the actual design constraint this package has to satisfy, not the
several hundred methods pandas' own `DataFrame` class accumulated over a decade.

```clojure
(defenum Column
  (NumericCol (data : NDArray @ Region))
  (StringCol  (data : (Vec String) @ Region)))

(defstruct DataFrame
  (columns : (Vec Column) @ Region)
  (column-names : (Vec String) @ Region)
  (row-count : I32))

(defn read-csv [(path : String @ :region/scratch) (dest : Arena @ Region)]
  : (Result DataFrame IoError) @ Region)
(defn column [(df : &DataFrame) (name : String @ :region/scratch)]
  : (Result (&Column) ColumnNotFoundError))
(defn select [(df : &DataFrame) (names : (Vec String) @ :region/scratch) (dest : Arena @ Region)]
  : (Result DataFrame ColumnNotFoundError) @ Region)
(defn filter [(df : &DataFrame) (pred : (Fn [I32] Bool)) (dest : Arena @ Region)]
  : DataFrame @ Region)
(defn group-by [(df : &DataFrame) (name : String @ :region/scratch) (dest : Arena @ Region)]
  : (Result (Vec DataFrame) ColumnNotFoundError) @ Region)
```

Deliberately **not** included: `merge`/`join` (real, needed eventually, but a genuinely bigger
design question — pandas' own join semantics are notoriously subtle around index alignment and
NaN handling — left for whoever actually needs cross-DataFrame joins to ground against a real use
case), pivot tables, and time-series-specific indexing (pandas' `DatetimeIndex` machinery is close
to its own subsystem, not a `dataframe`-package afterthought). `read-csv` is included because it's
the one I/O path every real pandas program starts from — without it this package can't even be
exercised by a real `.prn` program, the same "would be write-only without it" reasoning `buffer`'s
own `get-data` used above.

**Same honest limitation as `array`/`linalg`/`stats`**: a real fast CSV parser and a real
columnar-scan `filter`/`group-by` implementation are both genuine engineering, not specified by
the signatures above — flagged, not resolved here.

### `nn` / `tokenizer` / `sort` — grounded in porting `gpt2-alpine-c`

Founder: "add any more stdlib you can think of that would be needed to port gpt2alpinec." Not
speculative — read the real source (`/home/fatbaby/gpt2-alpine-c/src/`) rather than guessing what
a GPT-2 inference engine needs. It's a small, real C program: `gpt2.c`'s `gpt2_model_forward`
(the transformer forward pass) calls three static helpers by name — `layernorm`, `gelu_inplace`,
`softmax_inplace` — plus raw matmuls that `linalg` above already covers; `tokenizer.c` has
`tokenizer_load`/`gpt2_encode`/`gpt2_decode` (a real BPE-style tokenizer); `archetype.c` calls
`qsort` with a comparator for top-N ranking (the sampling-time top-k selection every GPT-2-style
decoder needs). Three small packages, matching those three real needs exactly — nothing extra.

**`nn`** — depends on `array` only, three primitives, not a full deep-learning framework:

```clojure
(defn layernorm [(x : &NDArray) (weight : &NDArray) (bias : &NDArray) (dest : Arena @ Region)]
  : (Result NDArray ShapeError) @ Region)
(defn gelu [(x : &NDArray) (dest : Arena @ Region)] : NDArray @ Region)
(defn softmax [(x : &NDArray) (dest : Arena @ Region)] : NDArray @ Region)
```

Deliberately **not** an `attention` or `transformer-block` function — `gpt2_model_forward` itself
composes attention from matmuls (`linalg/matmul`) + `softmax` + `layernorm`, not a single fused
call, and that composition is exactly the shape worth preserving: a program *using* `nn` builds
its own attention out of these primitives, the same way it's expected to build its own model
architecture. Baking "attention" in as one opaque stdlib call would hide the actual computation a
`gpt2-alpine-c` port needs to be honest about.

**`tokenizer`** — a real, stateful BPE tokenizer, matching `tokenizer_load`/`gpt2_encode`/
`gpt2_decode`'s own three-function shape exactly:

```clojure
(defn load [(vocab-path : String @ :region/scratch) (dest : Arena @ Region)]
  : (Result Tokenizer IoError) @ Region)
(defn encode [(!t : &Tokenizer) (text : String @ Region) (dest : Arena @ Region)]
  : (Result (Vec I32) TokenizeError) @ Region)
(defn decode [(!t : &Tokenizer) (ids : &(Vec I32)) (dest : Arena @ Region)]
  : (Result String TokenizeError) @ Region)
```

Unlike the source's own `tokenizer_load`/`tokenizer_free` pair (a global, process-lifetime
singleton — real C, but exactly the kind of hidden global state region typing exists to avoid), a
`Tokenizer` here is a real value with its own region, loaded once and passed explicitly to
`encode`/`decode` — no separate `free` function needed, since it's just an arena allocation like
everything else, reclaimed when its region ends.

**`sort`** — depends on nothing, generic over any `Vec`, grounded in `archetype.c`'s real
`qsort(sorted, ARCH_COUNT, sizeof(ArchetypeScore), score_cmp)` top-N-ranking use, which is exactly
what GPT-2-style sampling's top-k/top-p selection needs at generation time:

```clojure
(defn sort-by [(v : &mut (Vec T)) (cmp : (Fn [T T] I32))])
(defn top-k [(v : &(Vec T)) (k : I32) (cmp : (Fn [T T] I32)) (dest : Arena @ Region)]
  : (Vec T) @ Region)
```

Generic over `T` (not `NDArray`-specific) since `archetype.c`'s own comparator sorts a struct
(`ArchetypeScore`), not raw floats — a `sort` package tied to `array` would have been narrower
than the real C code it's meant to replace.

### `gfd` — the GFD mod-surface binding, VS0 planning pass

Founder: "add any mor stdlib we think we will need for the vs0 of the mod api for GFD look at
that northstar do any planning necessary and add some docs." This section is that planning pass,
grounded in `GoblinFoxDragon/docs2/MOD_SURFACE_NORTHSTAR.md` (read in full, not skimmed) and the
real, already-running EduScript binding layer it names as the closest existing precedent — not a
green-field design.

**The real precedent, checked directly**: `packages/education/edu_bindings.c`'s builtin table —
`set_switch`/`is_switch_on`, `open_gate`/`close_gate`, `raise_bridge`, `stabilize_portal`/
`open_portal`, `move_crate`/`stop_crate`/`set_crate_speed`, `mark_enemy`/`slow_enemy`,
`spawn_prop`, `set_entity_pos`/`set_entity_vel`, `query_grounded`, `mark_quest_complete`/
`get_quest_state`, `print`/`show_message`, `scan_gate`/`scan_portal`/`scan_enemy_count` — is a
real, working, already-shipped C-style FFI table (flat function names, int-only args, dispatched
through `edu_binding_call(EduWorldState *world, ...)`). It runs today in `apps/lobby`'s
"Architect's Orb" terminal. `gfd` below is that same shape, upgraded to PARENA's actual type
system (real structs/enums/regions instead of raw ints) — not a reinvention of what world-object
scripting should look like, a direct port of a pattern that's already proven to work.

**Real, still-open blocker, not glossed over**: `apps2/battlegrounds_gui` (the FPS "edu edition"
client the founder specifically prioritized, per that northstar's own §1) has **zero** plugin/
mod/script-loading mechanism today — this section designs the PARENA-side function signatures a
future binding layer would expose; it does not build that loading mechanism, which is real,
separate, unstarted work on the GFD side, not a PARENA-side gap.

```clojure
(defenum WorldObjectKind (Gate) (Bridge) (Portal) (Switch) (Crate) (Prop))

(defn set-switch    [(id : I32) (on : Bool)] : (Result Unit WorldError))
(defn open-gate     [(id : I32)] : (Result Unit WorldError))
(defn raise-bridge   [(id : I32)] : (Result Unit WorldError))
(defn spawn-prop     [(kind : String @ :region/scratch) (x : F64) (y : F64) (z : F64)]
  : (Result I32 WorldError))
(defn set-entity-pos [(id : I32) (x : F64) (y : F64) (z : F64)] : (Result Unit WorldError))
(defn query-grounded [(id : I32)] : Bool)
```

**Solidity / destructibility** (NORTHSTAR §4's "solid buildings ~80% collidable" +
"destructible-environments engine" — the latter explicitly meant to share one real system with
`/home/fatbaby/skateboard/NORTHSTAR.md`'s own mesh-based damage/reveal design, not become a
second parallel implementation):

```clojure
(defn set-solid       [(prop-type : String @ :region/scratch) (solid : Bool)] : (Result Unit WorldError))
(defn breach          [(prop-id : I32) (impact-x : F64) (impact-y : F64) (impact-z : F64)]
  : (Result Unit WorldError))     ; opens a real mesh-based damage/reveal state, per skateboard's own spec
(defn is-breached      [(prop-id : I32)] : Bool)
```

**Skate-culture surfaces** (NORTHSTAR §4's own citation of `skateboard/NORTHSTAR.md`'s "the city
itself is the skatepark" — grindable/ollie-able as a per-surface-type property, not a hardcoded
mesh list):

```clojure
(defn set-grindable [(surface-type : String @ :region/scratch) (grindable : Bool)]
  : (Result Unit WorldError))
(defn set-ollieable  [(surface-type : String @ :region/scratch) (ollieable : Bool)]
  : (Result Unit WorldError))
```

**Faction hooks** (NORTHSTAR §4's own citation of GTA7's real, already-shipped
`FactionManager.java` — `join(player, faction)`/`reputation(playerId)`/
`addRep(playerId, amount)` checked directly — same shape, not a bespoke GFD-only system):

```clojure
(defn faction-join     [(player-id : I32) (faction : String @ :region/scratch)] : (Result Unit WorldError))
(defn faction-rep      [(player-id : I32)] : I32)
(defn faction-add-rep  [(player-id : I32) (amount : I32)] : (Result Unit WorldError))
```

**METALVERSE terminal panels** (NORTHSTAR §4a — spawn a world-anchored typed panel showing a
ticker chart or news feed, backed by FatBaby's real, already-live `signalapi` on `:9091`):

```clojure
(defenum PanelKind (TickerChart (ticker : String @ Region)) (NewsFeed))
(defn spawn-panel [(kind : PanelKind) (x : F64) (y : F64) (z : F64)] : (Result I32 WorldError))
```

**Explicitly not resolved by this planning pass** — same open questions `MOD_SURFACE_NORTHSTAR.md`
§6 itself lists, not silently answered here: whether EduScript's arrays/functions gap needs
closing regardless of PARENA's own progress (moot if PARENA is the chosen path, still real if
not), how a `gfd` binding layer actually loads/sandboxes untrusted mod scripts inside
`apps2/battlegrounds_gui` (a real security/stability question, zero design here), and whether
`gfd` should be one flat package or split further once real usage exists to ground that call —
matching every other "don't split packages speculatively" decision already made in this document.

### `regex/*` + `grep`/`sed`/`awk` — pattern matching and the classic Unix text tools built on it

Founder: "also ensure we have like elite elite elite level regex in the stdlib" → "maybe we
implement all the different regex types" → "as different packages" → "like perl should be
jealous" → "sed awk grep" → "in the stdlib" → "and beyond" → "and add dependencies you need for
all these asks as std libs themselves." Unlike every stdlib section above, there's no internal
repo call site to ground this against — no code in this monorepo does real regex matching today.
The grounding here is real prior art in how production regex engines are actually built, not
internal call sites: **"all the different regex types" is a real, well-known engineering
distinction**, not marketing — a backtracking engine (Perl/PCRE) and a guaranteed-linear-time
engine (RE2/Go's `regexp`) solve different problems and neither one subsumes the other, so "elite
elite elite" means shipping *both*, honestly, as separate packages, not one package pretending to
be both.

**`regex/syntax`** — the dependency every engine below needs: parses pattern *text* into a shared
AST once, so `regex/nfa` and `regex/pcre` don't each hand-roll their own parser for the ~90% of
syntax (literals, `.`, `*`/`+`/`?`, `[...]` classes, `\d`/`\w`/`\s`, alternation, groups) that's
identical between them — real precedent: Rust's own `regex` crate splits exactly this way
(`regex-syntax` parses once, `regex-automata`/backtracking backends consume the same AST). This is
the "dependency you need for these asks as its own std lib" pattern applied to regex itself, one
level down.

```clojure
(defn parse [(pattern : String @ :region/scratch) (dest : Arena @ Region)]
  : (Result PatternAst SyntaxError) @ Region)
```

**`regex/nfa`** — Thompson-construction NFA compiled to a Pike's-VM bytecode, run breadth-first
over all live threads at once (Russ Cox, "Regular Expression Matching Can Be Simple And Fast" —
the same technique RE2 and Go's `regexp` package ship in production). **Guaranteed** `O(len(text)
* len(pattern))` worst case — no catastrophic backtracking, ever, by construction, because there
is no backtracking. The trade-off, stated plainly and not glossed over: this rules out
backreferences and lookaround, which are fundamentally backtracking features (the NFA has no
notion of "what did group 1 already capture" or "what comes before this position" while running
all threads in lockstep) — this is the *safe default* engine, not the full-featured one.

```clojure
(defn compile   [(pattern : String @ :region/scratch) (dest : Arena @ Region)]
  : (Result Regex SyntaxError) @ Region)
(defn is-match  [(re : &Regex) (text : String @ Region)] : Bool)
(defn find      [(re : &Regex) (text : String @ Region)] : (Option Match))
(defn find-all  [(re : &Regex) (text : String @ Region) (dest : Arena @ Region)]
  : (Vec Match) @ Region)

(defstruct Match
  (start  : I32)
  (end    : I32)
  (groups : (Vec (Option (I32 I32))) @ Region))
```

**`regex/pcre`** — the "Perl should be jealous" ask, literally: a real backtracking VM with the
full Perl/PCRE2 feature set the NFA engine above structurally cannot support — named captures
(`(?<name>...)`), backreferences (`\1`, `\k<name>`), lookahead/lookbehind (`(?=...)`/`(?!...)`/
`(?<=...)`/`(?<!...)`), atomic groups (`(?>...)`), possessive quantifiers (`*+`/`++`/`?+`), and
non-greedy quantifiers (`*?`/`+?`). Honest limitation stated up front, not discovered later:
backtracking engines are worst-case exponential (classic ReDoS: `(a+)+b` against a long non-
matching string) — real engines don't pretend otherwise, they cap it. PCRE2 itself ships a
match-limit/depth-limit safety valve for exactly this reason; `compile` below takes the same kind
of budget rather than shipping an engine that can hang a process on attacker-controlled input.

```clojure
(defn compile  [(pattern : String @ :region/scratch) (budget : MatchBudget) (dest : Arena @ Region)]
  : (Result Regex SyntaxError) @ Region)
(defn is-match [(re : &Regex) (text : String @ Region)] : (Result Bool BudgetExceededError))
(defn find     [(re : &Regex) (text : String @ Region)] : (Result (Option Match) BudgetExceededError))
(defn find-all [(re : &Regex) (text : String @ Region) (dest : Arena @ Region)]
  : (Result (Vec Match) BudgetExceededError) @ Region)
(defn replace  [(re : &Regex) (text : String @ Region) (replacement : String @ Region) (dest : Arena @ Region)]
  : String @ Region)   ; replacement supports $1/$name backreferences into captured groups
```

**`regex/posix`** — BRE/ERE compatibility, kept as its own package because POSIX match semantics
are a genuinely different rule, not a syntax dialect switch: POSIX mandates **leftmost-longest**
matching (the overall match is the longest one starting at the earliest position, full stop),
where Perl/PCRE and the NFA engine above are both **leftmost-first** (first alternative that
matches wins, `a|ab` against `"ab"` matches `"a"`). Conflating these under one `compile` flag
would silently change match results depending on flag state — a real correctness hazard, hence a
separate package.

```clojure
(defenum PosixFlavor (BRE) (ERE))
(defn compile [(pattern : String @ :region/scratch) (flavor : PosixFlavor) (dest : Arena @ Region)]
  : (Result Regex SyntaxError) @ Region)
```

**`regex/glob`** — shell-style glob (`*`, `?`, `[...]`, `{a,b}` brace expansion), deliberately
*not* built on `regex/syntax` — glob is a different, much smaller grammar (no alternation-via-`|`,
no quantifiers, no capture groups) and translating it through a full regex AST would be more
machinery than the problem needs, same "don't over-generalize" judgment `sort`'s `Vec`-generic
design used above.

```clojure
(defn matches [(pattern : String @ :region/scratch) (path : String @ Region)] : Bool)
```

**`grep`/`sed`/`awk`** — the actual Unix tools, each thin and built directly on the packages
above rather than reimplementing matching logic:

```clojure
(defenum Engine (Nfa) (Pcre) (Posix))   ; real grep itself has -G/-E/-P engine-select flags — same idea

(defn lines-matching [(!f : FileHandle @ :region/task) (pattern : String @ :region/scratch)
                       (engine : Engine) (dest : Arena @ Region)]
  : (Result (Vec String) IoError) @ Region)
```

`sed`'s one real primitive, `s/pattern/replacement/flags` substitution over a stream, needs
line-at-a-time reading that today's `io` package doesn't have (`read-string` above reads the
whole file) — extending `io` with `read-line`/`lines` is exactly the "add dependencies you need
for these asks as std libs themselves" instruction, so it's added to `io` (not a new package,
since it's a natural extension of an existing one, not a new domain):

```clojure
; io extension, added alongside read-string/read-floats above:
(defn read-line [(!f : FileHandle @ :region/task)] : (Result (Option String) IoError) @ :region/scratch)
```

```clojure
(defn substitute [(!f : FileHandle @ :region/task) (pattern : String @ :region/scratch)
                   (replacement : String @ :region/scratch) (dest : Arena @ Region)]
  : (Result String IoError) @ Region)   ; built on io/read-line + regex/pcre's own replace
```

`awk` is the one that needs a real new dependency, stated honestly rather than hidden inside the
`awk` package itself: field-splitting a record and evaluating a pattern-action program both need
a small typed expression evaluator with awk's own string/number coercion rules (`"3" + 4` is `7`)
— that's a real, separate, reusable capability, not `awk`-specific machinery, so it's its own
package:

```clojure
; expr — new dependency: a tiny arithmetic/string/comparison expression evaluator
(defenum ExprValue (Num (v : F64)) (Str (v : String @ Region)))
(defn eval [(src : String @ :region/scratch) (bindings : &(Map String ExprValue))
            (dest : Arena @ Region)]
  : (Result ExprValue EvalError) @ Region)
```

```clojure
(defstruct AwkRule
  (pattern : (Option Regex) @ Region)   ; None = unconditional (BEGIN/END or a bare action)
  (action  : String @ Region))          ; expr source text, (re-)evaluated per matching Record

(defstruct AwkProgram (rules : (Vec AwkRule) @ Region))
(defstruct Record (fields : (Vec String) @ Region) (nr : I32))

(defn run [(!f : FileHandle @ :region/task) (program : &AwkProgram) (dest : Arena @ Region)]
  : (Result Unit IoError) @ Region)   ; per line: read-line -> split into Record.fields via string/split
                                       ; -> each rule whose pattern (regex/pcre) matches (or is None)
                                       ; gets its action run through expr/eval against NR/NF/$1../$N
```

**"and beyond"** — named but deliberately not designed here, same "flagged, not resolved" pattern
this whole document already uses for `merge`/`join` and `net`: `tr` (character transliteration —
would share `regex/syntax`'s own `[...]` class-parsing code, not the matching engine) and `diff`
(needs an LCS/Myers-diff algorithm that's real, separate work unrelated to regex at all) are the
natural next two, left for whoever actually needs them to ground the design against a real use
case rather than speculating on `run`/`substitute`'s own signature shape sight-unseen.

**Standing note on scope, not just for this section**: founder, live, after this section —
"like this is going to be pretty heavy batteries included." Correct read, stated explicitly so
it's a decision and not a drift: this document opened with Go's stdlib (small, narrow, `io`/`os`/
`strings`/`fmt`-shaped) as the reference point, and this section is a real, acknowledged move past
that toward Python's "batteries included" end of the spectrum (a full engine-choice regex family
plus the classic Unix text tools on top, not just a `regexp`-equivalent). Not walked back — the
same "small, single-purpose, composable packages" discipline still applies *within* each package
(`regex/nfa` doesn't grow PCRE features, `awk` doesn't grow a `merge`/`join`), it's the *count* of
packages that's allowed to be large, same as CPython's own stdlib being simultaneously huge and
made of individually small modules.

### `net/tcp` / `net/udp` / `net/http` — resolves the earlier "net — not designed" gap for real

Founder: "i think thats the stack?" → "as long as we have full http stuff" → "and tcp" → "and
udp." Grounded in two real, different networking shapes already live in this monorepo, not
invented from scratch: SHANKPIT's server-authoritative UDP FPS (`:6969`) and REDGARDEN/
GoblinFoxDragon's `arena_server`/`wsudprelay` (raw `sendto`/`recvfrom` UDP, socket-per-port) are
the real `net/udp` precedent; IDUNA and PRRJECT_FATBABY's `signalapi`-style JSON-over-HTTP
services are the real `net/http` precedent. Three packages, not one flat `net`, because UDP/TCP's
own connectionless-vs-connection-oriented distinction is as real a semantic split as
`regex/posix`'s leftmost-longest-vs-leftmost-first was above — collapsing them into one package
would hide that difference behind a single API that has to lie about one side of it.

```clojure
; net/udp — connectionless, matches SHANKPIT/arena_server's own sendto/recvfrom shape
(defn bind    [(port : I32) (dest : Arena @ Region)] : (Result UdpSocket NetError) @ Region)
(defn send-to [(!sock : &UdpSocket) (addr : SocketAddr) (data : String @ Region)]
  : (Result I32 NetError))
(defn recv-from [(!sock : &UdpSocket) (dest : Arena @ Region)]
  : (Result (String SocketAddr) NetError) @ Region)   ; blocks; a real server owns its own poll loop

; net/tcp — connection-oriented
(defn listen  [(port : I32) (dest : Arena @ Region)] : (Result TcpListener NetError) @ Region)
(defn accept  [(!l : &TcpListener) (dest : Arena @ Region)] : (Result TcpStream NetError) @ Region)
(defn connect [(host : String @ :region/scratch) (port : I32) (dest : Arena @ Region)]
  : (Result TcpStream NetError) @ Region)
(defn read    [(!s : &mut TcpStream) (dest : Arena @ Region)] : (Result String NetError) @ Region)
(defn write   [(!s : &mut TcpStream) (data : String @ Region)] : (Result I32 NetError))

; net/http — depends on net/tcp only, matches signalapi's own request/response JSON shape
(defstruct HttpRequest  (method : String @ Region) (path : String @ Region)
                        (headers : (Map String String) @ Region) (body : (Option String) @ Region))
(defstruct HttpResponse (status : I32) (headers : (Map String String) @ Region)
                        (body : String @ Region))

(defn get  [(url : String @ :region/scratch) (dest : Arena @ Region)]
  : (Result HttpResponse NetError) @ Region)
(defn post [(url : String @ :region/scratch) (body : String @ Region) (dest : Arena @ Region)]
  : (Result HttpResponse NetError) @ Region)
(defn serve [(port : I32) (handler : (Fn [&HttpRequest Arena] HttpResponse))]
  : (Result Unit NetError))   ; matches signalapi/IDUNA's own "one handler fn per route" shape
```

Real, honest limitation, same pattern as `array`/`linalg`'s BLAS/LAPACK note above: real TLS
(`net/http`'s `get`/`post` against `https://` URLs, which IDUNA/signalapi both require in
production) needs a real TLS implementation underneath — not specified here, genuinely separate
work, most plausibly an FFI binding to a real C TLS library rather than a from-scratch
implementation, the same judgment call already made for `linalg`.

### `sdl2` — built-in, not an optional `import`

Founder: "we need to build SDL2 in PARENA" → "in the stdlib" → "SDL2 is built in" → "but written
in PARENA" → "same APIs." Four real, separate decisions, each honored exactly, not blended into
one vague "add SDL2 support": **(1)** it's a *stdlib* package, not a code-generator or build-
system integration; **(2)** it ships **built-in** — same tier as `core`, no `(import sdl2)` line
needed, unlike every other package in this document; **(3)** the binding itself is **real Parena
source**, using the `#target {:c (inline-c "...")}` FFI escape NORTHSTAR's own "Cross-target
native FFI" idiom already specifies, not hand-written C glue code sitting outside the language;
**(4)** function names **mirror real SDL2**, translated to kebab-case, not a redesigned windowing
API — `sdl2/create-window` calls real `SDL_CreateWindow`, it doesn't reinvent what a window is.

Call surface grounded in a real grep across this monorepo's own SDL2 usage (BRAWLPIT, SHANKPIT,
REDGARDEN, PITVIPER, GoblinFoxDragon) rather than the full upstream SDL2 API — the same "small,
grounded in real need" discipline as every other package above, not a wholesale header port:

```clojure
(defn init [] : (Result Unit Sdl2Error))
(defn quit [])
(defn create-window [(title : String @ :region/scratch) (w : I32) (h : I32) (dest : Arena @ Region)]
  : (Result Window Sdl2Error) @ Region)
(defn destroy-window [(!w : Window)])

(defn poll-event [] : (Option Event))          ; matches every client's own SDL_PollEvent loop shape
(defn get-keyboard-state [] : &(Vec Bool))      ; SDL_GetKeyboardState
(defn get-mouse-state [] : (I32 I32 I32))       ; x, y, button-mask — SDL_GetMouseState
(defn set-relative-mouse-mode [(on : Bool)])    ; PITVIPER's own mouse-drag-selection precedent

(defn get-ticks [] : I32)                       ; SDL_GetTicks
(defn delay [(ms : I32)])                       ; SDL_Delay

(defn get-clipboard-text [(dest : Arena @ Region)] : (Result String Sdl2Error) @ Region)
(defn set-cursor [(cursor : SystemCursor)])      ; SDL_CreateSystemCursor + SDL_SetCursor, PITVIPER precedent

(defn open-audio-device [(dest : Arena @ Region)] : (Result AudioDevice Sdl2Error) @ Region)
(defn queue-audio [(!dev : &AudioDevice) (samples : &NDArray)] : (Result Unit Sdl2Error))
```

`get-mouse-state` returning a tuple and `queue-audio` taking `array`'s own `NDArray` (raw PCM
samples are just a float/int buffer, same reasoning `io/read-floats` used for GPT-2 checkpoint
weights above) are the two real design choices here beyond a flat rename — everything else is a
direct one-to-one mirror of the grepped call, on purpose.

**Real, honest limitation**: game controller support (`SDL_GameControllerOpen`/`GetAxis`/
`GetButton`, real, grepped, used by BRAWLPIT/REDGARDEN/GoblinFoxDragon) and full renderer/texture
calls (`SDL_CreateRenderer`, draw calls) are left out of this pass — the grep surfaced them, but
folding two more real subsystems (controller input state machine, a texture/renderer resource
lifecycle with its own linear-ownership shape) into the same pass as everything else this session
already added risks the "add dependencies as std libs themselves" instruction turning into
unbounded scope creep; flagged as the next real extension once a renderer-owning program actually
needs it, not designed blind here.

### `editor/plugin` / `editor/buffer` / `editor/events` / `editor/ui` — the editor half, real API surface only

Founder: "also the stdlibs we need for the editor." NORTHSTAR.md's own "Editor/plugin API"
section already names these four modules and their purpose (`parena/plugin` lifecycle+commands,
`parena/buffer` text access, `parena/events` hooks, `parena/ui` decorations/overlays) but
explicitly flags the editor shell itself — which of Electron/Tauri/GTK+GtkSourceView/SDL2+ImGui/
ncurses+Tree-sitter — as **"Not started, not in VS0... an open, undecided question."** That
undecided-shell status hasn't changed; what's designed here is only the plugin-facing surface a
`.prn` plugin author would call, matching NORTHSTAR's own module table exactly, not the shell's
internal text-buffer representation (rope vs. gap-buffer vs. piece-table is real, separate,
shell-specific work that can't be honestly designed until the shell itself is chosen) or rendering
(if SDL2+ImGui ends up the chosen shell, `editor/ui` would plausibly be implemented on top of the
`sdl2` package above — a real, live connection worth noting, not yet decided).

```clojure
; editor/plugin — lifecycle, configuration, command palette
(defn register-command [(name : String @ :region/scratch) (handler : (Fn [] Unit))])
(defn get-config [(key : String @ :region/scratch)] : (Option String))

; editor/buffer — read/insert/delete/select text ranges in the active buffer
(defn active-text [(dest : Arena @ Region)] : String @ Region)
(defn insert [(pos : I32) (text : String @ Region)] : (Result Unit BufferError))
(defn delete-range [(start : I32) (end : I32)] : (Result Unit BufferError))
(defn selection [] : (Option (I32 I32)))

; editor/events — subscribe to editor actions
(defenum EditorEvent (OnSave) (OnChange) (OnKeybind (key : String @ Region)))
(defn subscribe [(event : EditorEvent) (handler : (Fn [] Unit))])

; editor/ui — gutter decorations, inline diagnostics, status bar, popups
(defn set-gutter-marker [(line : I32) (glyph : String @ :region/scratch)])
(defn show-diagnostic [(line : I32) (severity : DiagnosticSeverity) (msg : String @ Region)])
(defn set-status-bar [(text : String @ Region)])
(defn show-popup [(text : String @ Region) (x : I32) (y : I32)])
```

## Explicitly not designed yet — real gaps, not silently filled

- ~~**Collections beyond `Vec`/`Map` literals**~~ — resolved above (`vec`/`map` packages), once
  `expr`/`awk` made the dependency real instead of speculative.
- **`net`** — nothing in the source spec, NORTHSTAR, or the `gfd` planning above touches raw
  networking (the METALVERSE panel binding above calls into GFD's existing signalapi client
  code, not a PARENA-native HTTP client) — still not designed, still not guessed at.
- **Cross-target divergence** — every signature above is written target-agnostic (`FileHandle`,
  not `int fd` or `java.io.RandomAccessFile`); NORTHSTAR's own "Multi-target compilation" table
  says C/JVM/TS/Wasm each get a different `Arena`-under-the-hood, but VS0 only targets C — how
  `io/open` maps to each target's real file-handle type is real, unstarted, target-specific work
  that only matters once a target beyond C exists.

## Related

- `NORTHSTAR.md` — "Standard library" section names this same gap; this doc is that gap's actual
  design pass. "Core idioms" section is the source of all five grounded function calls above.
