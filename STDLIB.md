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
22. `thread` — depends on `core` only (FFI to real OS threads, same tier reasoning as `net/tcp`)
23. `otp/gen-server` — depends on `thread`, `map`
24. `otp/supervisor` — depends on `otp/gen-server`
25. `otp/ets` — depends on `thread`, `map`
26. `media/audio` — depends on `sdl2`, `media/codec`
27. `media/codec` — depends on `string` only (FFI-bound; the real codec library is the dependency,
    not another PARENA package)
28. `media/stream` — depends on `net/tcp`, `media/codec`, `vec`
29. `sql/ast` — depends on `string`, `map`
30. `sql/planner` — depends on `sql/ast`
31. `sql/driver` — depends on `net/tcp`, `sql/planner`
32. `ringo` — depends on `array`, `sdl2`
33. `pty` — depends on `string`
34. `shell` — depends on `pty`, `string`
35. `ssh` — depends on `string`
36. `crypto/hash`, `crypto/aes`, `crypto/ed25519` — depend on `string` only (each FFI-bound)
37. `gfd/browser` — depends on `gfd`, `vec` (FFI-bound to a real embeddable engine)
38. `ncurses` — depends on `string` only (FFI-bound; kept narrow per founder's own scope call)
39. `firefly` — depends on `vec`, `string` (no other stdlib deps — the base testing library)
40. `firefly/gomega` — depends on `firefly` only
41. `scarab` — depends on `firefly`, `firefly/gomega`
42. `compress/lz4` — depends on `string` only (FFI-bound)
43. `pitviper/protocol` — depends on `net/tcp`, `string`, `vec`, `compress/lz4`
44. `profile` — depends on `core` only (FFI-bound; `heap-snapshot` is real/native, region-native)
45. `staticanalysis` — depends on `string`, `vec` (FFI-bound)
46. `git` — depends on `shell`, `pty`, `vec` (wraps the real `git` CLI, not a from-scratch object model)
47. `media/tts` — depends on `sdl2`, `array` (F5-TTS sidecar client; the sidecar process itself is separate, unstarted work)
48. `pitviper/quicklook` — depends on `pitviper/protocol`, `mapbuilder/tools`, `media/codec`, `media/audio`
49. `net/vpn`, `net/packetradio`, `net/mesh` — depend on `string` only (each FFI-bound)
50. `cli` — depends on `string`, `vec`
51. `config` — depends on `cli`, `string`

**Priority order** (founder: "and then prioritize them" — distinct from dependency order above;
this is *build/attention* priority, dependency-respecting but not identical to it, since a
low-numbered dependency tier isn't automatically high-value on its own):

1. **Foundational, blocks everything** — `vec`, `map`, `string`, `io`, `log`, `buffer` (nothing
   else in this document is usable without these).
2. **Directly blocks the two live product asks** (GFD web/mod-surface, already-shipping; MIXFORGE/
   streaming, newly stated intent) — `sdl2`, `thread`, `net/tcp`+`net/udp`, `regex/pcre` (`awk`/
   `sed`/`grep` all need it), `gfd`.
3. **Second-order product-blocking** — `media/audio`, `media/codec`, `media/stream`, `net/http`,
   `expr`, `awk`/`sed`/`grep` themselves, `otp/gen-server`+`otp/supervisor`+`otp/ets` (concurrency
   ergonomics for whatever server process ends up running `media/stream`'s multi-destination
   fan-out).
4. **Real but not currently blocking a stated product** — `array`/`linalg`/`stats`/`dataframe`/
   `nn`/`tokenizer`/`sort` (the gpt2-alpine-c-port + numpy/pandas-equivalent work, real and
   grounded, but no live thread this session asked to build something on top of them yet),
   `regex/syntax`+`regex/nfa`+`regex/posix`+`regex/glob` (regex/pcre alone unblocks `awk`/`sed`/
   `grep`; the other three engines are completeness, not a live blocker), `editor/*` (blocked on
   NORTHSTAR's own still-undecided shell question, not on this stdlib).

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

**`gfd/browser` — a real modern web browser panel, FFI-bound to a real embeddable engine**:
founder: "so we are going to build a plugin to GGD that introduces a web browser so add any
primatives needed to build a modern web browser" → resolved via AskUserQuestion to "FFI-bind a
real embeddable engine." A modern browser (HTML/CSS parsing, layout, a JS engine, networking,
sandboxing) is Chromium/WebKit-scale — genuinely one of the most complex software artifacts that
exists, so this section designs the PARENA-facing call surface over a real, existing embeddable
engine (most plausibly Chromium Embedded Framework, the standard real choice for rendering live
web content to an off-screen texture inside a native/game app — Ultralight or WebKitGTK are
lighter real alternatives), not a native HTML/CSS/JS implementation:

```clojure
(defn create-webview [(url : String @ :region/scratch) (w : I32) (h : I32) (dest : Arena @ Region)]
  : (Result WebView BrowserError) @ Region)
(defn navigate       [(!wv : &mut WebView) (url : String @ :region/scratch)] : (Result Unit BrowserError))
(defn render-to-texture [(!wv : &mut WebView)] : &(Vec U8))   ; raw RGBA framebuffer, feeds spawn-panel's own world-anchored panel
(defn inject-js       [(!wv : &mut WebView) (script : String @ Region)] : (Result JsValue BrowserError))
(defn handle-input     [(!wv : &mut WebView) (event : Event)] : Unit)   ; mouse/keyboard forwarded from sdl2/poll-event
(defn destroy-webview [(!wv : WebView)] : Unit)
```

**Real security consideration, stated plainly rather than glossed over**: this renders arbitrary
web content (real HTML/CSS/JS execution) inside a live, multiplayer community server
(EINHORN_SURVIVAL/GTA7's own real player base) — a genuinely different risk profile from every
other `gfd` binding above, which only ever touch pre-defined world-object types. Real engines
(CEF included) run their renderer in a separate, sandboxed process for exactly this reason; the
GFD-side question of which URLs a `create-webview` call is even allowed to load (open web vs. an
allowlist) is real, unresolved policy work, not a PARENA-stdlib question this section answers.

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

**Real scope correction, stated by the founder directly after this section was first written**:
"we want to reimplement the sdl2 in parena like not just embed it" → "that can be a longer term
ask i guess." Everything above is an FFI wrapper — real SDL2 does the actual window/render/input
work, `sdl2` just calls it. That is explicitly **not** the end state wanted: the real long-term
goal is a native PARENA reimplementation of SDL2's own functionality (raw framebuffer/window
management, input polling, audio mixing — written in PARENA itself, no libSDL2.so underneath at
all). Founder's own follow-up correctly scoped this as "longer term," not this pass — the FFI
wrapper above is the real, buildable near-term package (and the thing PITVIPER's own plugin API
and the vim-editor work below actually need first); the native reimplementation is flagged here
as a real, large, separate future undertaking, not designed or started in this document.

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

### `thread` — real prerequisite this pass surfaced, not previously listed anywhere

Founder asked for OTP-ergonomics stdlib packages (below), which exposed a real gap: NORTHSTAR
specifies zero concurrency primitives — no `spawn`, no channel, no mutex, nothing. Even
"ergonomics only, no BEAM scheduler" (the founder's own resolved answer) still needs *some* real
substrate under it. `thread` is that substrate: OS threads via a pthreads-equivalent FFI binding
(same "call into a real, proven primitive" judgment as `linalg`'s BLAS/LAPACK note), not a
green-thread scheduler — a real, honest, buildable minimum, not a BEAM reimplementation.

```clojure
(defn spawn  [(f : (Fn [] Unit)) (dest : Arena @ Region)] : Thread @ Region)
(defn join   [(!t : Thread)] : (Result Unit ThreadError))
(defn channel [(dest : Arena @ Region)] : (Channel T) @ Region)
(defn send   [(ch : &(Channel T)) (v : T)] : (Result Unit ChannelError))
(defn recv   [(ch : &(Channel T))] : (Result T ChannelError))
(defn mutex  [(dest : Arena @ Region)] : Mutex @ Region)
(defn lock   [(!m : &Mutex)] : MutexGuard)
```

### `otp/gen-server` / `otp/supervisor` / `otp/ets` — Erlang-stdlib ergonomics, resolved scope

Founder: "ensure that the full erlang stdlib is included" → AskUserQuestion resolved **"OTP
ergonomics only"** — real Erlang/OTP idioms (callback-module servers, supervision-tree restart
policies, an in-memory keyed table) implemented on top of `thread`/`map` above, not on a BEAM-
style preemptively-scheduled process runtime PARENA doesn't have and this pass explicitly declined
to design (that would be its own NORTHSTAR-scale effort — the AskUserQuestion's own second option,
not the one chosen).

```clojure
; otp/gen-server — callback-module pattern: init/handle-call/handle-cast, one thread per server
(defstruct GenServer (state : &mut S) (inbox : (Channel Message)) (!worker : Thread))
(defn start [(init : (Fn [] S)) (handle-call : (Fn [S Message] S)) (dest : Arena @ Region)]
  : GenServer @ Region)
(defn call [(!gs : &GenServer) (msg : Message)] : (Result Reply GenServerError))
(defn cast [(!gs : &GenServer) (msg : Message)] : (Result Unit GenServerError))

; otp/supervisor — restart policy on child GenServer crash, real "let it crash" ergonomics
(defenum RestartPolicy (OneForOne) (OneForAll) (RestForOne))
(defn start-link [(children : (Vec GenServer) @ Region) (policy : RestartPolicy)
                   (dest : Arena @ Region)]
  : Supervisor @ Region)

; otp/ets — in-memory keyed table, mutex-guarded map/table (real ETS is a BEAM-native primitive;
; here it's honestly just a thread-safe map/table, named for the ergonomics it mirrors)
(defn new    [(dest : Arena @ Region)] : (EtsTable K V) @ Region)
(defn insert [(t : &(EtsTable K V)) (key : K) (value : V)] : Unit)
(defn lookup [(t : &(EtsTable K V)) (key : &K)] : (Option (&V)))
```

**`otp/scheduler` — real escalation past "ergonomics only," a scoped middle ground**: founder,
after the OTP-ergonomics resolution above, went further — "also go even harder into erlang with
the scheduler stuff." A full BEAM-style *preemptive* scheduler (interrupting a running process
mid-instruction on a fairness timer, no cooperation required from the code running) is a real
VM-level undertaking on par with the earlier-declined "full OTP runtime" AskUserQuestion option —
still not built here. What real "scheduler stuff" *is* buildable on top of `thread` without that:
a **cooperative work-stealing scheduler** — a fixed pool of OS threads (`thread/spawn`) each
running a work-stealing deque of queued tasks, tasks yielding at real await/blocking points rather
than being preempted. This is the same real, honest "bind the closest working real primitive"
judgment used throughout this document (BLAS/LAPACK for `linalg`, real SDL2 for `sdl2`) applied to
scheduling: cooperative-on-real-threads is a real, scoped, buildable design; full BEAM preemption
is not attempted.

```clojure
(defn start-pool [(worker-count : I32) (dest : Arena @ Region)] : SchedulerPool @ Region)
(defn submit [(!pool : &SchedulerPool) (task : (Fn [] Unit))] : (Result Unit SchedulerError))
(defn yield-point [] : Unit)   ; cooperative yield -- called at real blocking points (channel
                                 ; recv, mutex lock) so one long task can't starve the pool
```

### `media/audio` / `media/codec` / `media/stream` — FFI-bound, resolved scope

Founder: "and full audio apis" → "we are going to build djsoftware in PARENA too" → "MIXFORGE"
(searched the whole monorepo for a prior spec per the founder's own "i dunno if we have the docs
for that... its a very old thread" — confirmed genuinely nothing exists, MIXFORGE is new, not
recovered) → "and full audio and video streaming codex" → "we are going to build media servers in
this too" → AskUserQuestion resolved **"FFI-bind real libs"** — same judgment as `linalg`'s
BLAS/LAPACK note, now applied explicitly to codecs rather than assumed. Video codecs (H.264/AV1-
class) and full media-server engineering are each real, independent, FFmpeg/Icecast-scale efforts
— this section designs the PARENA-facing call surface only; the actual encode/decode/mux work
happens inside whatever real C library gets bound (most plausibly `libavcodec`/`libavformat` for
codec, a real audio backend for `audio`), not reimplemented natively.

```clojure
; media/audio — the real, grepped `sdl2` audio calls, promoted to their own package since
; DJ-software mixing is a genuinely separate concern from windowing/input
(defn open-device [(dest : Arena @ Region)] : (Result AudioDevice AudioError) @ Region)
(defn load [(path : String @ :region/scratch) (dest : Arena @ Region)]
  : (Result AudioClip AudioError) @ Region)     ; via media/codec below for compressed formats
(defn play [(!dev : &AudioDevice) (clip : &AudioClip) (gain : F64)] : (Result Unit AudioError))
(defn mix [(clips : &(Vec (AudioClip F64))) (dest : Arena @ Region)]
  : AudioClip @ Region)   ; (AudioClip, per-clip gain) pairs — the real MIXFORGE crossfade primitive

; media/codec — thin FFI wrapper, function names mirror the real bound library, same "same APIs"
; judgment already applied to sdl2 above
(defn decode [(path : String @ :region/scratch) (dest : Arena @ Region)]
  : (Result MediaFrame CodecError) @ Region)
(defn encode [(frame : &MediaFrame) (format : CodecFormat) (dest : Arena @ Region)]
  : (Result String CodecError) @ Region)

; media/stream — the real, stated reason this is native and not a third-party SaaS: "dual stream
; to multiple services... building our own solution for that for overhead and security reasons" —
; owning the relay avoids per-destination proxy overhead and keeps stream keys/credentials off a
; third-party relay
(defn connect-destination [(url : String @ :region/scratch) (key : String @ :region/scratch)
                            (dest : Arena @ Region)]
  : (Result StreamDest NetError) @ Region)   ; built on net/tcp below
(defn publish [(frame : &MediaFrame) (destinations : &(Vec StreamDest))]
  : (Result Unit StreamError))               ; fan-out to every connected destination at once
```

### `sql/ast` / `sql/planner` / `sql/driver` — building blocks, implementation deferred

Founder: "we need the building blocks for the sql drivers" → "we can deferr that for now i think"
→ "we need the building blocks for the sql drivers" → "and the query planners" → "so we need AST
stuff" → "i guess." Net read, taken at face value rather than resolved by guessing: design the
package *surfaces* now (so `dataframe/read-csv`-adjacent code and any future ETS/gen-server-backed
service has a real interface to target), leave the actual parser grammar, query-planning
algorithm, and wire-protocol driver implementations as real, separate, deferred work — the
founder's own "defer for now" stands for the deep implementation, not the shape.

```clojure
; sql/ast — parsed query representation, same "shared AST, multiple consumers" shape as
; regex/syntax above
(defenum SqlStmt
  (Select (columns : (Vec String) @ Region) (from : String @ Region) (where : (Option SqlExpr) @ Region))
  (Insert (table : String @ Region) (values : (Map String SqlValue) @ Region))
  (Update (table : String @ Region) (set : (Map String SqlValue) @ Region) (where : (Option SqlExpr) @ Region))
  (Delete (table : String @ Region) (where : (Option SqlExpr) @ Region)))

(defn parse [(query : String @ :region/scratch) (dest : Arena @ Region)]
  : (Result SqlStmt SyntaxError) @ Region)

; sql/planner — depends on sql/ast; turns a SqlStmt into a real execution plan (index scan vs.
; full scan, join order) -- genuinely deferred: real query planning is its own field of study,
; not a same-pass reference implementation the way linalg's naive matmul was.
(defn plan [(stmt : &SqlStmt) (schema : &TableSchema) (dest : Arena @ Region)]
  : (Result QueryPlan PlanError) @ Region)

; sql/driver — depends on net/tcp; a connection interface any real backend (Postgres wire
; protocol, SQLite's own C API via FFI) implements, matching io's own FileHandle-abstraction
; judgment: target-agnostic at the signature level, real wire-protocol work per backend.
(defn connect [(dsn : String @ :region/scratch) (dest : Arena @ Region)]
  : (Result Connection ConnError) @ Region)
(defn execute [(!conn : &mut Connection) (plan : &QueryPlan) (dest : Arena @ Region)]
  : (Result (Vec (Map String SqlValue)) ConnError) @ Region)
```

### `ringo` — the matplotlib equivalent

Founder: "and matplotlib" → "give at qute parena name" → "apparently parena is a beetle" → "so
give it a qute beetle name maybe RINGO." Named directly by the founder, not invented here. Same
layering judgment as `array`/`linalg`/`stats`/`dataframe` above: matplotlib itself plots numpy
arrays onto a real rendering surface, so `ringo` depends on `array` for data and the already-
built-in `sdl2` for the surface — not a third, separate graphics backend.

```clojure
(defn figure [(w : I32) (h : I32) (dest : Arena @ Region)] : Figure @ Region)   ; opens an sdl2/create-window under the hood

(defn plot    [(!fig : &mut Figure) (x : &NDArray) (y : &NDArray)] : (Result Unit ShapeError))
(defn scatter [(!fig : &mut Figure) (x : &NDArray) (y : &NDArray)] : (Result Unit ShapeError))
(defn bar     [(!fig : &mut Figure) (labels : &(Vec String)) (values : &NDArray)] : (Result Unit ShapeError))
(defn hist    [(!fig : &mut Figure) (data : &NDArray) (bins : I32)] : Unit)

(defn set-title [(!fig : &mut Figure) (title : String @ Region)] : Unit)
(defn set-labels [(!fig : &mut Figure) (x-label : String @ Region) (y-label : String @ Region)] : Unit)

(defn show [(!fig : &Figure)] : Unit)   ; sdl2/poll-event loop until window closed, matches PITVIPER's own loop shape
(defn save [(fig : &Figure) (path : String @ :region/scratch)] : (Result Unit IoError))   ; via media/codec for PNG encode
```

Real, honest limitation, same pattern as everywhere above: `bar`/`hist`'s actual pixel-space
layout (axis scaling, tick placement, label text rendering) is real, non-trivial rendering work —
these are the real function signatures a plotting program calls, not a claim that the layout math
is solved here.

### `mapbuilder/tools` / `mapbuilder/layout` / `mapbuilder/template` / `world` — visual builder affordances

Founder: "can we build map builder affordances into the stdlib if thats a thing?" → resolved via
AskUserQuestion to a level/world map editor (not a `map`-the-dictionary builder pattern) → "like
maybe that s not stdlib i dunno" → clarified twice more: "like the actual affordances of how the
actual editor interface gets build and the affordances that editor gives to the user" → "like
whatever android has as an equivalent or the objective c whatever the iphone auto templating
stuff" → "to make it easy to design interfaces for both pc and mobile." Three real, distinct
affordance categories, each grounded in existing real code rather than invented:

**`mapbuilder/tools`** — click/drag/select/undo, the literal interaction primitives, grounded
directly in PITVIPER's own real mouse-drag-selection code shipped this session
(`cmd/pitviper/main.go`'s `selection` struct with `startRow`/`startCol`/`endRow`/`endCol`,
`pixelToCell`, and the `lastSelected` clipboard buffer) — generalized from text-cell selection to
arbitrary placeable-object selection:

```clojure
(defenum Tool (Place) (Paint) (Select) (Erase))

(defn pixel-to-cell [(x : I32) (y : I32) (cell-size : I32)] : (I32 I32))   ; direct generalization
                                                                              ; of PITVIPER's own fn
(defn begin-drag [(tool : Tool) (x : I32) (y : I32)] : DragState)
(defn update-drag [(!drag : &mut DragState) (x : I32) (y : I32)] : Unit)
(defn end-drag [(!drag : DragState) (canvas : &mut Canvas)] : (Vec PlacedObject) @ Region)

(defn undo [(!canvas : &mut Canvas)] : (Result Unit HistoryError))   ; real command-pattern stack
(defn redo [(!canvas : &mut Canvas)] : (Result Unit HistoryError))
(defn copy-selection [(canvas : &Canvas) (sel : &(I32 I32 I32 I32)) (dest : Arena @ Region)]
  : (Vec PlacedObject) @ Region)   ; the lastSelected-buffer idea, generalized past text
```

**`mapbuilder/layout`** — the real "android/iOS auto templating stuff" ask: constraint-based
auto-layout, the actual reason those two platforms use constraints instead of raw pixel
coordinates — one layout definition has to resolve correctly across many real screen sizes (PC
window resize, phone portrait/landscape). Grounded in the real, well-known ConstraintLayout/Auto
Layout model (align-to, pin-to-edge, aspect-ratio, chains), not invented terminology:

```clojure
(defenum Constraint
  (AlignTo    (target : ViewId) (edge : Edge))
  (PinToEdge  (edge : Edge) (margin : I32))
  (AspectRatio (ratio : F64))
  (Chain      (members : (Vec ViewId) @ Region) (direction : ChainDirection)))

(defn add-constraint [(!layout : &mut Layout) (view : ViewId) (c : Constraint)] : Unit)
(defn solve [(layout : &Layout) (viewport-w : I32) (viewport-h : I32) (dest : Arena @ Region)]
  : (Map ViewId (I32 I32 I32 I32)) @ Region)   ; ViewId -> resolved (x y w h), one solve per resize
```

Real, honest limitation: the actual constraint-solving algorithm (Cassowary, the same
linear-arithmetic constraint solver both Auto Layout and ConstraintLayout are built on) is real,
separate, well-documented-elsewhere work — `solve`'s signature is real, its body isn't attempted
here.

**`mapbuilder/template`** — named prefab/scene instantiation, grounded directly in
GoblinFoxDragon's own real, already-shipped per-scene procedural generators
(`server/worldapi/scenes.go`'s `meadowChunk`/`hillsChunk`/`cavesChunk`/`swampChunk`/`urbanChunk`,
each a real named function generating a chunk's content) — a "template" here is exactly that
pattern, generalized into a registry instead of one hardcoded Go switch:

```clojure
(defn register-template [(name : String @ :region/scratch) (generator : (Fn [TemplateParams Arena] (Vec PlacedObject)))]
  : Unit)
(defn instantiate [(name : String @ :region/scratch) (params : TemplateParams) (dest : Arena @ Region)]
  : (Result (Vec PlacedObject) TemplateError) @ Region)
```

**`world`** — the underlying data model these tools place objects onto, grounded in real,
already-shipped structs rather than invented: SHANKPIT's own `packages/world/terrain.h`
(`TerrainHeightfield{width, height, cell_size, origin_x, origin_z, float *heights}`,
`terrain_set_height`/`terrain_get_height`/`terrain_sample_height`) and `packages/common/block_map.h`
(`block_map_entry_t{name, block_id}`), plus GoblinFoxDragon's `server/worldapi/dragonfly_gen.go`
(`WorldBlock{X, Y, Z int; BlockName string}`) and `heightmap.go`'s `HeightmapChunk`/`ColumnHeight`.
`mapbuilder/tools`' own `Canvas`/`PlacedObject` types are built on this, not a separate world
model per game:

```clojure
(defstruct Terrain (heights : (Vec F64) @ Region) (width : I32) (height : I32) (cell-size : F64))
(defn set-height [(!t : &mut Terrain) (x : I32) (z : I32) (h : F64)] : Unit)
(defn get-height [(t : &Terrain) (x : I32) (z : I32)] : F64)

(defstruct PlacedObject (block-name : String @ Region) (x : I32) (y : I32) (z : I32))
```

**Founder's own hedge stands, restated honestly**: "maybe that s not stdlib i dunno" is a real,
open question this design doesn't resolve either way — `mapbuilder/tools`/`layout`/`template` are
designed here as stdlib packages (matching `editor/*`'s own precedent: interaction primitives a
program imports), but whether the actual visual builder *application* (the equivalent of Android
Studio's Layout Editor or Xcode's Interface Builder, as a standalone tool) belongs in this repo at
all, versus being its own separate application built on these primitives (same distinction already
drawn for MIXFORGE and the editor shell), is not decided by this pass.

### `pty` / `shell` / `ssh` / `crypto/*` — the concrete dogfooding path into PITVIPER

Founder: "then crunch on PARENA until we can dog food it into pitviper to fix our issue" — the
literal strangler-fig mechanism NORTHSTAR.md already names, applied to a real, current target:
PITVIPER's own two real bugs fixed this session (the `System32\bash.exe` WSL-stub misfire, Git
Bash not being found off PATH) both live in one place, `internal/pty/pty_windows.go`'s shell-
resolution logic. If PARENA had a `pty`/`shell` package covering the same ground, that logic could
move to PARENA-authored code over PITVIPER's plugin boundary instead of staying hand-written Go —
this section designs that package, direct precedent already shipped and real, not invented.

**`pty`** — spawn a subprocess attached to a pseudo-console, generalized from PITVIPER's own real
`Open()`/ConPTY (Windows)/`openpty` (POSIX) code:

```clojure
(defn open  [(shell : String @ :region/scratch) (cols : I32) (rows : I32) (dest : Arena @ Region)]
  : (Result Pty PtyError) @ Region)
(defn read  [(!p : &mut Pty) (dest : Arena @ Region)] : (Result String PtyError) @ Region)
(defn write [(!p : &mut Pty) (data : String @ Region)] : (Result I32 PtyError))
(defn resize [(!p : &mut Pty) (cols : I32) (rows : I32)] : (Result Unit PtyError))
(defn close [(!p : Pty)] : Unit)
```

Real, honest note on cross-target dispatch: NORTHSTAR's `#target` form branches by *compilation
target* (C/JVM/TS/Wasm), not by host OS — `pty/open`'s actual C-target body needs ConPTY vs.
`openpty` chosen by `#ifdef _WIN32`, ordinary C preprocessor branching *inside* the C-target
`inline-c` block, not a second PARENA-level dispatch mechanism. Flagged here since it's the first
package in this document where that distinction actually matters.

**`shell`** — the actual shell-resolution policy, a direct, real port of PITVIPER's own
`isWslStub`/`findGitBash`/fallback-chain logic (commits `908ac6a`, `8db557c`) onto `pty`:

```clojure
(defn resolve [(explicit : (Option String) @ :region/scratch)] : String @ :region/scratch)
  ;; explicit arg > $SHELL > Git Bash off PATH (skipping the WSL stub, same isWslStub check) >
  ;; Git Bash at its well-known install paths (same findGitBash check) > cmd.exe/zsh/bash fallback
(defn spawn-bash [(cols : I32) (rows : I32) (dest : Arena @ Region)] : (Result Pty PtyError) @ Region)
(defn spawn-zsh  [(cols : I32) (rows : I32) (dest : Arena @ Region)] : (Result Pty PtyError) @ Region)
```

**`ssh`** — FFI-bound to `libssh2` (real, established, embeddable SSH client library — same FFI-
bind judgment as `linalg`'s BLAS/LAPACK and `media/codec`'s libavcodec, not a from-scratch SSH
protocol implementation):

```clojure
(defn connect [(host : String @ :region/scratch) (port : I32) (user : String @ :region/scratch)
                (dest : Arena @ Region)]
  : (Result SshSession SshError) @ Region)
(defn exec    [(!sess : &mut SshSession) (cmd : String @ Region) (dest : Arena @ Region)]
  : (Result String SshError) @ Region)
(defn open-pty [(!sess : &mut SshSession) (cols : I32) (rows : I32) (dest : Arena @ Region)]
  : (Result SshChannel SshError) @ Region)   ; interactive shell over SSH, same Pty-like read/write shape
```

**`crypto/hash`** / **`crypto/aes`** / **`crypto/ed25519`** — FFI-bound to OpenSSL/libsodium (same
judgment, not from-scratch cryptographic primitives — hand-rolled crypto is a well-known way to
introduce real, serious vulnerabilities, so this explicitly does not attempt it):

```clojure
(defn sha256 [(data : String @ Region) (dest : Arena @ Region)] : String @ Region)   ; hex digest

(defn aes-encrypt [(key : String @ Region) (plaintext : String @ Region) (dest : Arena @ Region)]
  : (Result String CryptoError) @ Region)
(defn aes-decrypt [(key : String @ Region) (ciphertext : String @ Region) (dest : Arena @ Region)]
  : (Result String CryptoError) @ Region)

(defn ed25519-keygen [(dest : Arena @ Region)] : KeyPair @ Region)
(defn ed25519-sign   [(key : &KeyPair) (msg : String @ Region) (dest : Arena @ Region)]
  : String @ Region)
(defn ed25519-verify [(pubkey : String @ Region) (msg : String @ Region) (sig : String @ Region)]
  : Bool)
```

### `ncurses` — real, kept narrow

Founder: "i guess ncurses needs to be added to stdlib," immediately followed by "all in on PARENA
stdlibs and only build out the ones we need to dogfood the fixes for PITVIPER" — real scope
discipline applied to this package specifically: PITVIPER is its own SDL2-rendered terminal
*emulator*, not an ncurses consumer, so this isn't itself part of the PITVIPER-dogfooding path.
Kept here only as a minimal, real, FFI-bound wrapper (same judgment as `sdl2`/`ssh`/`crypto`) for
whichever real terminal *program* — tmux included — ends up wanting one, not expanded further
while the founder's own priority is the compiler (below).

```clojure
(defn init [] : (Result Screen NcursesError))
(defn print [(!scr : &mut Screen) (y : I32) (x : I32) (text : String @ Region)] : Unit)
(defn refresh [(!scr : &mut Screen)] : Unit)
(defn getch [(!scr : &mut Screen)] : I32)
(defn endwin [(!scr : Screen)] : Unit)
```

### `firefly` / `firefly/gomega` / `scarab` — testing, Go's package + Ginkgo/Gomega on top, beetle-named

Founder: "add testing to the stdlib" → "like the go testing module" → "and then build bdd ginkgo
style affordances on top" → "or in addition however that works" → "ginkgo and gomega" → "whatever
the BEETLE cute name equivalents of a testing library and a test runner" → "first in the base
testing framework which needs to be in place to builld the bdd framework and test runner" → "or
whatever goomega ginkgo are." Three real packages, matching the real Go ecosystem's own layering
exactly (Ginkgo and Gomega are two separate, real Go modules — `github.com/onsi/ginkgo`,
`github.com/onsi/gomega` — Ginkgo the BDD test-structure DSL *and test runner*, Gomega the matcher
library, each usable without the other), beetle-named per the founder's own naming convention
(`ringo`, above): **`firefly`** (fireflies are real beetles, family Lampyridae — "illuminating"
test results is the pun) for the base library, **`scarab`** for the runner/BDD layer (scarab
beetles' own real mythological association with cycles — a test runner that cycles through suites
on every run). Built in the order asked: base library first, since the runner/BDD layer is real,
scoped follow-up work built *on* it, not the other way around.

**`firefly`** — the base package, Go's own `testing.T` shape: a mutable test-state handle passed
into every test function, `errorf` records a failure and continues, `fatalf` records a failure and
stops that one test (matches Go's own `t.Error` vs. `t.Fatal` distinction exactly):

```clojure
(defstruct T (name : String @ Region) (failed : Bool) (messages : (Vec String) @ Region))

(defn errorf [(!t : &mut T) (msg : String @ Region)] : Unit)
(defn fatalf [(!t : &mut T) (msg : String @ Region)] : Unit)   ; also unwinds this one test, not the whole run
(defn skip   [(!t : &mut T) (reason : String @ Region)] : Unit)

(defstruct TestCase (name : String @ Region) (run : (Fn [&mut T] Unit)))
(defstruct TestReport (passed : I32) (failed : I32) (skipped : I32))

(defn run-tests [(cases : &(Vec TestCase)) (dest : Arena @ Region)] : TestReport @ Region)
```

**`firefly/gomega`** — depends on `firefly` only. The real matcher-chain shape (`Expect(x).To
(Equal(y))`), not a flat `assert-eq`-only surface — Gomega's own actual value is exactly this
composable matcher design:

```clojure
(defstruct Expectation (actual : &Any) (t : &mut T))

(defn expect [(actual : &Any) (!t : &mut T)] : Expectation)
(defn to           [(exp : &Expectation) (matcher : (Fn [&Any] Bool))] : Unit)   ; fails !t via errorf on mismatch
(defn equal        [(expected : &Any)] : (Fn [&Any] Bool))
(defn be-true       [] : (Fn [&Any] Bool))
(defn be-nil        [] : (Fn [&Any] Bool))
```

**`scarab`** — depends on `firefly`, `firefly/gomega`. The real Ginkgo shape:
`Describe`/`Context`/`It` nesting, `BeforeEach`/`AfterEach` hooks, plus the runner itself:

```clojure
(defn describe    [(name : String @ :region/scratch) (body : (Fn [] Unit))] : Unit)
(defn context     [(name : String @ :region/scratch) (body : (Fn [] Unit))] : Unit)   ; alias of describe, Ginkgo's own convention
(defn it          [(name : String @ :region/scratch) (body : (Fn [&mut T] Unit))] : Unit)
(defn before-each [(hook : (Fn [] Unit))] : Unit)
(defn after-each  [(hook : (Fn [] Unit))] : Unit)
(defn run-suite   [(dest : Arena @ Region)] : TestReport @ Region)   ; walks the registered Describe/Context/It tree
```

Real, honest limitation: `describe`/`context`/`it` need to register into a shared, ambient test
tree that `run-suite` later walks — real, non-trivial runtime bookkeeping (Ginkgo's own real
implementation does this with global mutable state + reflection-adjacent tricks), not specified in
depth here; the signatures are real, the registration mechanism's own implementation is flagged as
real follow-up work once VS0 can run enough Parena to build it against.

### `pitviper/protocol` / `compress/lz4` — the custom remote-IDE protocol, resolved scope

Founder: "basically i am extending my IDE which is actually this VPS" → "i am using pitviper to
bring the affordances in a more gui way when we dont need to live in ssh necessarily" → "but if i
can just pop open a gui nerd tree style thing for the current directory" → "and then it opens
pitviper native vim on the local windows computer" → "and then it uses git to push the changes up
to the server if we need to make custom edits" → separately, AskUserQuestion confirmed a **custom
PITVIPER server** over adopting an existing remote-graphics protocol (Sixel/iTerm2 images), plus
"all the packet hacks we can" / "lz4ify the fuck out of everything." Real, stated motivation for
why this is worth building at all, not a speculative nice-to-have: "previously if i wanted to make
file changes i would go on github" — the whole point is replacing a slow, disconnected GitHub-web-
UI round-trip with a fast, local-editor loop directly against the VPS's own live state. Founder's
own framing, live: "we are building crisper feedback loops with pitviper." The full real workflow,
now concrete enough to design a real protocol against, not speculative:

1. GUI popout (client-side, PITVIPER's own SDL2 rendering) requests a directory listing from a
   small **server-side daemon** running on the VPS.
2. Selecting a file fetches its real content over `ssh` (already designed above), opens it in
   PITVIPER's own native, PARENA-authored vim-like editor — **locally, on the Windows machine**,
   not rendered remotely. This is a real, deliberate architecture choice, not a detail: the "GUI
   popout" protocol is for *browsing* (directory trees, panels), editing itself stays a real local
   editor with real local responsiveness, the same reason VS Code's own Remote-SSH mode syncs file
   content locally rather than streaming a remote text-editing UI.
3. Edits happen locally; round-tripping back to the server is real, existing `git push` — not a
   new PARENA-native git library, invoked as a real subprocess through `shell`/`pty` above. Scoped
   deliberately narrow: reusing what already works (git) rather than reimplementing it.

```clojure
; pitviper/protocol — depends on net/tcp, string, vec
(defenum Request
  (ListDir (path : String @ Region))
  (FetchFile (path : String @ Region)))   ; the actual bytes travel over ssh/exec, not this channel

(defenum DirEntry (File (name : String @ Region)) (Dir (name : String @ Region)))
(defenum Response
  (DirListing (entries : (Vec DirEntry) @ Region))
  (Error      (msg : String @ Region)))

(defn serve   [(port : I32) (root : String @ :region/scratch)] : (Result Unit NetError))   ; the real "PITVIPER server" daemon
(defn connect [(host : String @ :region/scratch) (port : I32) (dest : Arena @ Region)]
  : (Result ProtocolSession NetError) @ Region)
(defn request [(!sess : &mut ProtocolSession) (req : Request) (dest : Arena @ Region)]
  : (Result Response NetError) @ Region)
```

**`compress/lz4`** — depends on `string` only, FFI-bound to the real, tiny, widely-embedded LZ4
reference implementation (same FFI-bind judgment as `ssh`/`crypto`/`media/codec` above — LZ4 is
picked specifically, not zstd/gzip, because the founder's own ask was about keeping a live,
interactive protocol *fast*, and LZ4's real, well-known niche is exactly "favor compression/
decompression speed over ratio" for latency-sensitive links, not archival size):

```clojure
(defn compress   [(data : String @ Region) (dest : Arena @ Region)] : String @ Region)
(defn decompress [(data : String @ Region) (original-size : I32) (dest : Arena @ Region)]
  : (Result String CompressError) @ Region)
```

`pitviper/protocol/request`'s own real wire format would run every `Response` through
`compress/lz4`'s `compress` before sending, `decompress` on receipt — real, direct application of
"lz4ify the fuck out of everything," not a separate unused package.

**Real, honest limitation**: this designs the protocol's real message shapes and the real
compression choice; the server daemon's own file-system-watching/live-update behavior (does the
GUI tree refresh automatically on remote changes, or only on demand?) is real, unresolved product
design, not answered here — same "flagged, not resolved" pattern as everywhere else in this doc.

### `profile` / `staticanalysis` — gnarly C/C++-grade tooling, FFI-bound to the real thing

Founder: "PARENA should help address mempry issues" — already this language's own core value
proposition, not a new package: region typing (NORTHSTAR's "Memory model" section) catches the
exact bug class (escaping pointers, use-after-free-shaped errors) at compile time, which is a
stronger guarantee than any of the tools below give a C/C++ program *after the fact*. Then: "profiling
needs to be built into PARENA std lib" → "gnarly c and cpp style profiling" → "all of the state of
the art static analysis tools too built into the stdlib."

**`profile`** — FFI-bound to real, established profiling infrastructure (same judgment as `ssh`/
`crypto`/`media/codec`/`compress/lz4` above, not a from-scratch profiler): Linux `perf_event_open`
for real sampling-based CPU profiling (the same real mechanism `perf record` itself uses), with a
`callgrind`-style instrumented mode as the real, honest "slower but exact call-graph" alternative
(Valgrind's own callgrind is the real prior art named directly):

```clojure
(defn start-cpu-profile [(dest : Arena @ Region)] : (Result Profiler ProfileError) @ Region)
(defn stop-cpu-profile  [(!p : Profiler) (out-path : String @ :region/scratch)]
  : (Result Unit ProfileError))   ; writes a real perf.data-compatible or pprof-compatible file
(defn heap-snapshot [(dest : Arena @ Region)] : HeapSnapshot @ Region)   ; per-region byte counts, real and cheap since Arena already tracks used/capacity
```

`heap-snapshot` is the one real, native (non-FFI) piece: unlike CPU profiling, PARENA's own
region/arena model already tracks exactly what a heap profiler wants (bytes allocated per
region, arena-by-arena) — no external tool needed for that half, a real, load-bearing
consequence of the language's own design, not a coincidence.

**`staticanalysis`** — FFI-bound to real, established static analyzers (clang-tidy, cppcheck,
real prior art named directly, not invented tool names) for the checks region typing itself
doesn't cover (style, real C-target-specific footguns once code is emitted, dead-code detection):

```clojure
(defn run [(target-c-file : String @ :region/scratch) (dest : Arena @ Region)]
  : (Result (Vec Diagnostic) AnalysisError) @ Region)

(defstruct Diagnostic (severity : Severity) (message : String @ Region) (line : I32))
(defenum Severity (Info) (Warning) (Error))
```

Real, honest scoping note, not glossed over: `staticanalysis/run` operates on VS0's own *emitted
C* (post domain-3), catching issues in the C target specifically — it is not a second Parena-
source-level analysis pass competing with `region_analyze` itself, which already is the real,
compile-time-enforced static analysis for the properties that actually matter most (region safety),
stronger than anything an external C linter can see once the region information itself has been
erased by emission.

### `git` — as a real PARENA stdlib, not just shell/pty subprocess calls

Founder: "we need to start building our own GIT gui in the browser on top of iduna" → "write it in
rails before we PARENA it" → "as a parena stdlib." The founder's own real sequencing for the
GitHub-alternative web UI itself is Rails first (own admitted reasoning: "i guess we build it in
ruby on rails thats what github and gitlab do" → "i dunno why tho" — pattern-matched off real
prior art, not a deeper technical reason, real and honestly stated as such) — that web app is
explicitly **not** designed here. What *is* real PARENA stdlib scope right now: the same "as a
parena stdlib" instruction applied to `pitviper/protocol`'s own already-noted git-push-back path
(above: "invoked as a real subprocess through `shell`/`pty`... reusing what already works") —
promoted from an inline note into its own real package, since founder-flagged "as a parena stdlib"
is a real, separate ask from wrapping a couple of subprocess calls ad hoc:

```clojure
(defn status [(repo-path : String @ :region/scratch) (dest : Arena @ Region)]
  : (Result GitStatus GitError) @ Region)
(defn diff   [(repo-path : String @ :region/scratch) (dest : Arena @ Region)]
  : (Result String GitError) @ Region)
(defn add    [(repo-path : String @ :region/scratch) (paths : &(Vec String))] : (Result Unit GitError))
(defn commit [(repo-path : String @ :region/scratch) (message : String @ Region)] : (Result Unit GitError))
(defn push   [(repo-path : String @ :region/scratch) (remote : String @ :region/scratch)
               (branch : String @ :region/scratch)]
  : (Result Unit GitError))
```

Real, honest scoping, same judgment already used for `ssh`/`crypto`/`compress/lz4`: this wraps the
real `git` CLI via `shell`/`pty` (spawn `git status`/`git diff`/etc., parse real stdout) rather
than reimplementing Git's own object model, pack-file format, or wire protocol from scratch —
those are real, substantial, separate undertakings (this is exactly the kind of thing the eventual
Rails-then-PARENA GitHub-alternative web UI would itself need built out for real, once that
project actually starts) that this pass explicitly declines to attempt.

### `media/tts` — F5-TTS as a local sidecar, resolved scope

Founder: "we need to build a FIRE TTS model into pitviper" → AskUserQuestion confirmed the real,
specific model: **F5-TTS** (`SWivid/F5-TTS`, a real 2024 open-source flow-matching TTS model) →
"its gotta be tiny and local" → real, stated tension flagged directly rather than glossed over:
F5-TTS's real checkpoints run a few hundred MB and need a real PyTorch/ONNX inference runtime —
genuinely local (no cloud API call), not genuinely tiny in the sense of something FFI-bindable
into a native Go+SDL2 binary the way `ssh`/`crypto` above are. AskUserQuestion resolved this as a
**local sidecar service**, not a scope swap to a smaller model: F5-TTS runs as its own small local
Python process (the real F5-TTS repo, unmodified), PITVIPER/PARENA talk to it over a local
socket — text in, raw audio out, played via `sdl2`'s own `queue-audio` (already designed above,
same `NDArray`-as-PCM-buffer reasoning `io/read-floats` already established for GPT-2 checkpoint
weights).

```clojure
(defn synthesize [(text : String @ :region/scratch) (dest : Arena @ Region)]
  : (Result NDArray TtsError) @ Region)   ; PCM samples, fed straight into sdl2/queue-audio
(defn connect-sidecar [(socket-path : String @ :region/scratch)] : (Result Unit TtsError))
```

Real, honest limitation: the F5-TTS sidecar process itself (starting it, health-checking it, the
actual Python-side inference code) is real, separate, unstarted work — this designs the PARENA-
side client call only. Real, honest sequencing note also flagged plainly: PARENA can't execute
code yet (VS0 domains 3-4 only cover the exact shape `examples/valid_only.prn` uses, not a real
program like this), so PITVIPER's own near-term TTS integration — if wanted before VS0 progresses
further — would need to be real Go code talking to the same sidecar, not this `.prn` design,
mirroring how every other "dogfood PARENA into PITVIPER" package in this document is currently
signatures-and-intent, not something actually running inside PITVIPER today.

### `pitviper/quicklook` — real, concrete interaction model, macOS Quick Look as the named precedent

Founder, real and concrete, not exploratory: "when we have the nerd tree we will have hotkeys and
or buttons or some kind of affordance to let us either edit or view - ok edit is double click into
the vim like editor - view is like SPOTLIGHT on the mac - clicking on or using jk to select a file
and then hitting space downlads the file and displays it just like spotlight" → "start with
markdown" → "but we will do images" → "videos" → "audio" → "all built on PARENA." Real macOS
precedent named directly (Quick Look, triggered by Space in Finder/Spotlight) — not a redesigned
interaction model, a real, already-proven one applied here. Depends on `pitviper/protocol` (the
`FetchFile` request already designed) and `mapbuilder/tools`'s own real `pixel-to-cell`/selection
plumbing for the j/k-and-click file-tree navigation itself.

```clojure
(defenum PreviewKind (Markdown) (Image) (Video) (Audio) (Unsupported))

(defn detect-kind  [(path : String @ :region/scratch)] : PreviewKind)   ; real, by extension -- .md/.png/.mp4/.wav etc.
(defn open-preview  [(path : String @ :region/scratch) (dest : Arena @ Region)]
  : (Result Unit PreviewError) @ Region)   ; fetches via pitviper/protocol, dispatches by PreviewKind
(defn open-editor    [(path : String @ :region/scratch)] : (Result Unit PreviewError))   ; double-click, real vim-like editor open
```

Sequenced exactly as asked, each a real, separate rendering technology, not incremental variations
of the same one:

- **Markdown (first)** — real text layout (headings/lists/code blocks/emphasis), rendered via
  `sdl2`'s own text path, no image/video decode needed — the real reason it's the honest starting
  point, not just "easiest."
- **Image** — a real, separate technology: decode via `media/codec` (already designed, FFI-bound),
  render the decoded frame as an `sdl2` texture.
- **Video** — depends on `media/codec`'s own frame-by-frame decode plus real playback timing (not
  just one decoded frame) — genuinely the most involved of the four, real follow-up work once
  image preview is real and working.
- **Audio** — plays through `media/audio`'s own `play` (already designed above), no new decode
  path beyond what `media/codec` already covers.

Real, honest limitation, same pattern as the rest of this document: this designs the real,
requested interaction model and the real dispatch surface; the actual Markdown-layout algorithm,
image/video decoder integration depth, and playback-timing code are each genuinely separate
implementation work, not resolved by this pass.

### `net/vpn` / `net/packetradio` / `net/mesh` — real domains, real named prior art

Founder: "also we need any networking primatives for a vpn and beyond" → "also our domains are
mesh networks and packet radios" — a real, direct statement of what EINHORN_INDUSTRIAL's own
networking work actually covers, not a hypothetical. Three packages, each FFI-bound to real,
established, named technology (same judgment as everywhere else in this document — none of these
protocols get reimplemented from scratch):

```clojure
; net/vpn — FFI-bound to WireGuard (real, modern, deliberately simple protocol with real
; embeddable C/Go reference implementations — the honest choice over OpenVPN/IPsec's own much
; larger real implementation surface)
(defn create-tunnel  [(config : WgConfig) (dest : Arena @ Region)] : (Result Tunnel VpnError) @ Region)
(defn tunnel-send     [(!t : &mut Tunnel) (data : String @ Region)] : (Result Unit VpnError))
(defn tunnel-recv     [(!t : &mut Tunnel) (dest : Arena @ Region)] : (Result String VpnError) @ Region)

; net/packetradio — FFI-bound to AX.25 (real, decades-old amateur-radio packet protocol) + APRS
; (real, built on AX.25) framing
(defn ax25-encode [(payload : String @ Region) (dest : Arena @ Region)] : String @ Region)
(defn ax25-decode [(frame : String @ Region) (dest : Arena @ Region)] : (Result String Ax25Error) @ Region)
(defn aprs-parse  [(packet : String @ Region) (dest : Arena @ Region)] : (Result AprsPacket AprsError) @ Region)

; net/mesh — FFI-bound to real mesh routing (Meshtastic-style LoRa mesh, or B.A.T.M.A.N.-adv for
; a wired/wifi mesh -- both real, named, not invented)
(defn join-mesh   [(config : MeshConfig) (dest : Arena @ Region)] : (Result MeshNode MeshError) @ Region)
(defn broadcast   [(!node : &mut MeshNode) (data : String @ Region)] : (Result Unit MeshError))
(defn mesh-recv   [(!node : &mut MeshNode) (dest : Arena @ Region)] : (Result (String String) MeshError) @ Region)   ; (sender-id, data)
```

Real, honest limitation: which specific mesh technology (Meshtastic/LoRa vs. B.A.T.M.A.N.-adv)
depends on real hardware/deployment decisions (LoRa radios vs. wifi mesh) this pass doesn't have
enough information to resolve — both real signatures are given, actual backend selection is real
follow-up work once a specific deployment is chosen, not guessed at here.

### `cli` / `config` — Cobra/Viper equivalents, and the `parena` CLI's own eventual self-hosting target

Founder: "we need to build parena cli to start systamatizing it" → "so alsoo the cli stuff needs
to be in stdlib - viper and cobra equivalents" → "we will build the parena cli in PARENA of
course." Real, named Go prior art (`spf13/cobra` for command trees, `spf13/viper` for layered
config) — the real reason these are designed together: Viper's own actual value is precedence-
ordered config sources (flags > env > file > defaults), which only matters once a real `cli`
package's flags exist to be one of those sources.

```clojure
; cli — Cobra-equivalent
(defstruct Command
  (name        : String @ Region)
  (short       : String @ Region)
  (run         : (Fn [&(Vec String)] Unit))
  (subcommands : (Vec Command) @ Region))

(defn add-command [(!parent : &mut Command) (child : Command)] : Unit)
(defn add-flag    [(!cmd : &mut Command) (name : String @ Region) (default : String @ Region)] : Unit)
(defn execute     [(!root : &mut Command) (args : &(Vec String))] : (Result Unit CliError))

; config — Viper-equivalent, depends on cli (for flag precedence) + string
(defn load [(config-path : String @ :region/scratch) (dest : Arena @ Region)]
  : (Result Config ConfigError) @ Region)
(defn get  [(cfg : &Config) (key : String @ :region/scratch)] : (Option String))   ; real precedence: flag > env > file > default
```

Real, honest connection stated plainly: this is the real, concrete first target once self-hosting
(NORTHSTAR.md's own "Self-hosting" section) actually starts — `main.c`'s own hand-written
`argv`-scanning `parse`/`analyze`/`build` dispatch would be the first real thing rewritten against
`cli`/`config`, not a hypothetical example. Not attempted yet: self-hosting itself hasn't started
(domains 4-5 of VS0 are the real remaining prerequisite work).

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
