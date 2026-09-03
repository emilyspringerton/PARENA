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
15. `net/http` — depends on `net/tcp`, `string` (not `map` — headers are two parallel `Vec
    String`s, same real reason json.prn's `JObject` avoids `Map`: `K`/`V` generics fail VS0's
    emitter today. Client-side only (`http-get`/`http-post`/`http-request`); no URL parsing,
    callers pass host/port/path separately; real host glue + FFI verified live 2026-08-25)
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
42. `compress/lz4` — depends on `vec` only (real, pure PARENA — see its own section below)
43. `pitviper/protocol` — depends on `net/tcp`, `string`, `vec`, `compress/lz4`
44. `profile` — depends on `core` only (FFI-bound; `heap-snapshot` is real/native, region-native)
45. `staticanalysis` — depends on `string`, `vec` (FFI-bound)
46. `git` — depends on `shell`, `pty`, `vec` (wraps the real `git` CLI, not a from-scratch object model)
47. `media/tts` — depends on `sdl2`, `array` (F5-TTS sidecar client; the sidecar process itself is separate, unstarted work)
48. `pitviper/quicklook` — depends on `pitviper/protocol`, `mapbuilder/tools`, `media/codec`, `media/audio`
49. `net/vpn`, `net/packetradio`, `net/mesh` — depend on `string` only (each FFI-bound)
50. `cli` — depends on `string`, `vec`
51. `config` — depends on `cli`, `string`
52. `pentest/scan`, `pentest/pcap`, `pentest/webapp`, `pentest/wireless`, `pentest/crack`,
    `pentest/exploit` — depend on `string`, `vec` (each FFI-bound independently)
53. `world` — depends on `vec` only
54. `mapbuilder/tools` — depends on `world`, `vec`
55. `mapbuilder/layout`, `mapbuilder/template` — depend on `mapbuilder/tools`, `vec`
56. `idvault` — depends on `net/http`, `string` (a real IDUNA REST client, same shape as
    `pitviper/protocol`'s own `net/tcp`-based client)
57. `pitviper/expand` — depends on `string`, `map`
58. `pitviper/tiling` — depends on `vec` only

**Founder: "again we need the cli to systematize i say over and over plan those deps - whatever
makes sense - plan the deps etc"** — the list above is the full, current re-sort across every
package this document now designs (57 entries, up from the last full re-sort's 38) — topological
by `import`, same rule as every earlier pass: a package appears only after everything it actually
depends on.

**Priority order, re-planned** (founder: "and then prioritize them," repeated this pass —
*build/attention* priority, dependency-respecting but not identical to it):

1. **Foundational, blocks everything, already real `.prn` source** — `vec`, `map`, `string`, `io`,
   `log`, `buffer`, `thread`, `pty`, `shell` — done, `parena parse`-verified.
2. **Directly blocks the live "PARENA eats PITVIPER" thread** — `cli`/`config` (the real, concrete
   self-hosting target once VS0 domain 5 lands — `main.c`'s own `argv` dispatch is the literal
   first rewrite candidate), `pitviper/protocol`+`compress/lz4` (the real remote-IDE workflow),
   `pitviper/quicklook`, `git`, `ssh`. None of these have real `.prn` source yet except
   `ssh`/`pty`/`shell` above — this tier is designed, not built.
3. **Second-order product-blocking, partially real** — `sdl2` (real `.prn`), `net/tcp`/`net/udp`
   (real `.prn`), `net/http` (design only), `regex/pcre` (real `.prn`, the one engine `awk`/`sed`/
   `grep` actually need), `gfd`/`gfd/browser`, `world`/`mapbuilder/tools` (real `.prn`),
   `mapbuilder/layout`/`mapbuilder/template` (design only).
4. **Real but not currently blocking a stated live thread** — `array`/`linalg`/`stats`/`dataframe`/
   `nn`/`tokenizer`/`sort` (real `.prn`, gpt2-alpine-c-port grounded), `regex/syntax`+`regex/nfa`+
   `regex/posix`+`regex/glob` (real `.prn`), `editor/*` (real `.prn`, blocked on NORTHSTAR's own
   still-open shell question — now *resolved* as SDL2-based, not "undecided" anymore, this note
   was stale), `otp/*`, `media/*`, `sql/*`, `firefly`/`firefly/gomega`/`scarab` (real `.prn`),
   `crypto/*` (real `.prn`), `ncurses` (real `.prn`), `profile`, `staticanalysis`, `net/vpn`+
   `net/packetradio`+`net/mesh`, `pentest/*`, `idvault`, `pitviper/expand`.

**Real status, corrected — this note was stale and understated real progress**: VS0 domains 1-4
are done and CI-green (lexer/parser, region analyzer, C emitter, memory verification) — `parena
build examples/valid_only.prn -o out.c` produces real, running, ASan/Valgrind-clean C. Domain 5
(the CLI runner's own remaining polish) is the one real gap left in VS0 itself. What's still real
and true: only that one narrow shape (`examples/valid_only.prn`'s own `with-arena`/`let`/`alloc`
pattern) actually compiles — none of the ~57 stdlib packages above run yet, including the ~30 that
already have real, `parena parse`-verified `.prn` source. "Built out in Parena" still means *real,
parseable source exists*, not *executable* — that gap is now about the emitter's own real
generality (VS0 domain 3 only understands one program shape), not about domains 1-4 being
unstarted, which they no longer are.

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
(defn new     [(dest : Arena @ Region)] : (Vec T) @ Region)
(defn push!   [(v : &mut (Vec T)) (item : T)])
(defn get     [(v : &(Vec T)) (idx : I32)] : (Option (&T)))
(defn len     [(v : &(Vec T))] : I32)
(defn set-at! [(v : &mut (Vec T)) (idx : I32) (item : T)])   ; real, honest gap in this
  ; design doc itself, not just the implementation -- found missing (2026-08-21) via
  ; world.prn's own real `vec-set-at!` (Terrain.set-height writing a new height into
  ; its own heights vec by index), never designed here alongside new/push!/get/len.
  ; Real runtime: parena_runtime.h's own vec_set_at_, same out-of-bounds-is-a-silent-
  ; no-op safety convention `get` above already has (no real error-reporting channel a
  ; void-returning runtime function has to use here).

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

### `fp` — Ramda-equivalent functional-programming toolkit, depends on `vec`

Founder, real-time: "add stdlibs that all the functional programming js bros use" → "like after
underscore thewent really far out into functional programming i cant remember the library" →
"we need those stdlibs". Real, identified precedent: [Ramda](https://ramdajs.com) — the real JS
library that lineage describes (Underscore → Lodash → Ramda going further into pure FP than either:
auto-curried by default, immutable, point-free composition as the whole point, not a bolt-on).
Depends on `vec` (this session's own newly-real `(Vec T)`/`vec/new`/`vec/push!`/`vec/get`/
`vec/len`) for every collection-shaped signature below, the same way `regex`/`grep`/`awk` already
assumed it before `vec` itself was designed.

```clojure
; the three real Ramda-shaped collection operations every other one below builds on
(defn map    [(f : (Fn [T] U)) (v : &(Vec T)) (dest : Arena @ Region)] : (Vec U) @ Region)
(defn filter [(f : (Fn [T] Bool)) (v : &(Vec T)) (dest : Arena @ Region)] : (Vec T) @ Region)
(defn reduce [(f : (Fn [Acc T] Acc)) (init : Acc) (v : &(Vec T))] : Acc)

; composition -- Ramda's own real point: point-free pipelines, not currying for its own sake
(defn identity [(x : T)] : T)
(defn always   [(x : T)] : (Fn [] T))          ; Ramda's own const/K-combinator
(defn compose  [(f : (Fn [B] C)) (g : (Fn [A] B))] : (Fn [A] C))   ; right-to-left, Ramda's own convention
(defn pipe     [(f : (Fn [A] B)) (g : (Fn [B] C))] : (Fn [A] C))   ; left-to-right, the readable-pipeline sibling
(defn flip     [(f : (Fn [A B] C))] : (Fn [B A] C))
(defn tap      [(f : (Fn [T] Unit)) (x : T)] : T)   ; side-effect passthrough, Ramda's own real debug idiom

; predicate combinators -- Ramda's own real, small, genuinely useful set
(defn both       [(f : (Fn [T] Bool)) (g : (Fn [T] Bool))] : (Fn [T] Bool))
(defn either      [(f : (Fn [T] Bool)) (g : (Fn [T] Bool))] : (Fn [T] Bool))
(defn complement [(f : (Fn [T] Bool))] : (Fn [T] Bool))

; struct/field access -- Ramda's own real prop/pluck, natural fit over get-field
(defn prop  [(field : :Keyword) (x : &T)] : &U)               ; wraps get-field for a first-class-value use site
(defn pluck [(field : :Keyword) (v : &(Vec T)) (dest : Arena @ Region)] : (Vec U) @ Region)

; collection-shaping -- the other real, common Ramda exports worth naming explicitly
(defn uniq    [(v : &(Vec T)) (dest : Arena @ Region)] : (Vec T) @ Region)
(defn flatten [(v : &(Vec (Vec T))) (dest : Arena @ Region)] : (Vec T) @ Region)
(defn zip     [(a : &(Vec T)) (b : &(Vec U)) (dest : Arena @ Region)] : (Vec (T U)) @ Region)   ; tuple element -- real VS0 gap, see below
```

Real, honest scope decisions, not glossed over:

- **`curry` (Ramda's own headline feature — every function auto-curried by arity) is deliberately
  NOT designed here.** Real auto-currying needs runtime arity introspection (how many arguments
  does this `Fn` value actually expect, so a partial call can return a new closure awaiting the
  rest) — VS0 has no reflection/arity-inspection primitive of any kind, and no real, generic
  "closure that captures already-bound arguments" mechanism either (every `(Fn [...] ...)` value
  today is a bare C function pointer — `%s (*)(void)`/similar in `emit_defn`'s own parameter-type
  handling — not a real closure struct that could carry captured state). Faking curry as
  fixed-arity-only special cases (`curry2`, `curry3`, ...) was considered and rejected as exactly
  the kind of "pretend more precise than it is" this whole document avoids elsewhere — flagged
  as real, unstarted, and blocked on a genuine closure-representation feature, not silently
  skipped.
- **`map`/`filter`/`reduce`/`pluck`/`uniq`/`flatten`/`zip` above all take a callback or produce a
  `Vec` of a *different* element type than their input (`T` → `U`, or `T` → `(Vec T)` → `T`)** —
  real, additional generics pressure beyond what `(Vec T)`'s own single-type-parameter erasure
  (landed this session) covers; `T`/`U`/`Acc` in every signature above are illustrative of the
  *real* target shape, not something the current single-type-erasure `(Vec T)` support can resolve
  today without further emitter work (a real, separate, next increment, not conflated with this
  design pass).
- **`zip`'s own `(Vec (T U))` return type uses a tuple element shape** — STDLIB.md's own VS0 gap
  list already names tuple types as unimplemented (gap analysis further below); `zip` is included
  here for real completeness against Ramda's own actual export list, not because it's more ready
  to build than anything else in this section.
- **`assoc`/`dissoc`/`merge`/`path`/`pathOr`/`cond`/`when`/`unless`/`juxt`/`groupBy`/`sortBy`/
  `partial`** — real, further Ramda exports, explicitly NOT designed in this pass (this is already
  a large single addition); flagged as real follow-up scope, not an exhaustive Ramda port attempted
  in one sitting.

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

**Real VS0 emitter gaps found and fixed compiling this file (2026-08-21)**, none specific to
`string` itself — general emitter gaps this file happened to be the first real source to reach:
(1) `if` in tail position with a `loop` (or `let`/`do`/`when`/`cond`/`match`/`with-arena`) as one of
its own branch VALUES (`is-valid-i32-text?`'s own `(if (= n 0) false (loop ...))`) — `emit_if`'s own
pure-ternary form has no way to hold a statement-shaped construct; fixed by giving `if` in tail
position the same real statement-level composition its siblings already have in `emit_body`'s own
tail dispatch. (2) `alloc` with a real SIZE EXPRESSION rather than a string literal (`concat`'s own
`(alloc dest String (+ (length a) (length b)))`, immediately filled by a following `#target`
inline-C body) — `alloc` previously only understood a literal value. (3) `#target {:c (inline-c
"...")}` used as a MID-BODY statement (`concat`'s own real body: allocate, fill via inline-C for
its own side effect, then return the buffer separately) rather than replacing an entire function
body, the only shape it supported before. (4) A missing `#include <stdint.h>` in every generated
file's own preamble (`length`'s own `#target` body casts to `int32_t`).

**Second real gap, found and fixed in a follow-up pass (2026-08-21)**: `parse-i32` calls
`is-valid-i32-text?`, which is defined LATER in the same file — VS0 had no forward-declaration
pre-pass for `defn`s at all (only `defenum`/`defstruct` got one), so any function calling another
defined later in the same file hit a real "implicit declaration" error under gcc, undetected by
`parena build`'s own exit code. Fixed via a real forward-declaration pre-pass emitted ahead of every
defn body, for every `defn` carrying an explicit `: ReturnType` annotation — deliberately just
`ReturnType mangled_name();` (an old-style, unspecified-argument C declaration, confirmed to compile
cleanly under this project's own `-pedantic -Werror`), not a full parameter-matching prototype,
since C only requires a function be *declared* before use, not exactly re-specified. A `defn` with
no explicit return type (inferred from its own body) still gets no forward declaration — real,
narrower, honest scope, not yet hit by any known real call site.

**Third real gap, closed in a follow-up pass (2026-08-21)**: `ParseError`/`substring`/`str-eq?`/
`is-digit?`/`char-at`/`raw-parse-i32` were referenced throughout this file but never defined
anywhere in it — the same real missing-definition-in-source-itself gap class already closed for
pcap.prn/io.prn/array.prn. None of these are designed in this doc's own "string" section either
(only `parse-i32`/`length`/`concat`/`split` are) — real, original design work, not just filling in
an already-specified shape:

```clojure
(defn char-at [(s : String @ Region) (i : I32)] : I32)   ;; raw byte value -- no Char type yet
(defn str-eq? [(a : String @ Region) (b : String @ Region)] : Bool)
(defn is-digit? [(c : I32)] : Bool)
(defn substring [(s : String @ Region) (start : I32) (end : I32) (dest : Arena @ Region)] : String @ Region)
(defstruct ParseError (message : String))
```

`char-at`/`str-eq?` are real `#target` FFI wrappers (`s[i]`/`strcmp`, the same shape `length`'s own
body already uses); `is-digit?` is a plain ASCII range check (`>= 48 <= 57`), no new libc dependency
needed. `raw-parse-i32` (an internal `atoi` wrapper, not exported — only `parse-i32` itself calls it)
needed a new `#include <stdlib.h>` in every generated file's own preamble, the same unconditional-
inclusion tradeoff already made for `<stdint.h>`.

**Real, separate bugs found and fixed IN THIS FILE'S OWN SOURCE while closing the gap above**
(not compiler bugs — the call sites themselves were wrong): `starts-with-sign?` and
`is-valid-i32-text?` both used to call `(substring s 0 1 s)` / `(is-digit? (substring s i (+ i 1)
s))` — passing `s` itself (a String) where `substring`'s own `dest : Arena` argument belongs, in a
function whose own signature has no Arena parameter at all. Rewritten to use `char-at` (no
allocation needed for a single-character check) instead. `split`'s own delimiter check used to call
`(char-at s i (char-at sep 0))` — `char-at` is a real 2-argument function, so a 3rd argument was
never valid; the real, intended check is `(= (char-at s i) (char-at sep 0))`.

**Real gap closed (2026-08-21)**: `parse-i32` used to be blocked — `(Ok (raw-parse-i32 s))` needed
to box a scalar `I32` payload, but `parse-i32`'s own signature carried no `Arena` parameter to box
into at all, the identical gap `array.prn`'s own `get`/`set!` had. Resolved the same way: an
explicit `dest : Arena @ Region` parameter added. Every function in this file — including
`parse-i32` now — compiles real gcc-clean.

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

**Real VS0 emitter gap found and fixed compiling this file (2026-08-21)**: `set-data`'s own real
body ends in `(Ok unit)` — `unit`, the `Unit` type's own singleton value, had no handling anywhere
in the emitter at all, so a bare `unit` symbol fell through to the generic scope_lookup path and
failed as an unknown identifier (array.prn's own `set!` uses the identical real shape, hitting the
same gap). Fixed as a reserved literal emitting a plain `NULL`, reporting its own type as `void *`
— already pointer-typed, so `Ok`/`Err`/`Some`'s own payload check accepts it directly with no
boxing needed at all (`NULL` is already a valid, real pointer value).

**Real, STILL-not-fixed gap, this file's own**: `raw-buffer-write!`/`raw-buffer-read` are called but
never defined anywhere reachable, nor declared via `#target` FFI the way every other host-backed
primitive elsewhere in this stdlib is — real, separate, un-started work; this file's own header
comment already flags the underlying region-escape check itself as "VS0 domain 2, not built yet."

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
(defn reshape [(a : &NDArray) (new-shape : (Vec I32) @ :region/scratch) (dest : Arena @ Region)] : (Result NDArray ShapeError) @ Region)
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

**Real VS0 emitter gaps found and fixed compiling this package (2026-08-21)**: (1) `zeros`/
`from-vec`'s own bare (non-reference) `(shape : (Vec I32) @ :region/scratch)` parameter shape —
distinct from `product`/`strides-for`'s already-working `&(Vec I32)` reference form — was rejected
outright by the compiler's own param-parsing loop; fixed to accept a compound/list type there too.
(2) `Ok`/`Err`/`Some` (and single-field `defenum` variants) previously required a pointer-typed
payload outright (the runtime's `Result`/`Option` store `void *value`) — a map-literal struct
construction (`from-vec`'s own `(Ok {:data data ...})`) or a `deref`'d scalar produce a real,
non-pointer *value*, which used to just fail. Fixed via a generated, per-payload-type
`static inline TypeName *TypeName_box(Arena *dest, TypeName v)` helper function — the temp-and-
address-of logic lives inside that real, addressable C function body, sidestepping both a GNU
statement-expression (rejected under this project's own `-pedantic -Werror` build) and hoisting a
synthesized temp-declaration into `emit_expr`'s own pure-expression-returning call graph. The arena
to box into is found via a real, honest, narrow scope search (a bound local literally named `dest`
first — this doc's own already-established "every allocating function takes an explicit dest"
convention — falling back to the first arena-typed local in scope). (3) `when` in `loop`-tail
position (`strides-for`'s whole real loop body: `(when (>= i 0) (vec/push! &s running) (recur
...))`) had no handling at all — silently mangled into a bogus call to a never-defined `when(...)`
C function, caught only by an actual gcc compile, not `parena build`'s own exit code. (4) A scalar
pushed via `vec/push!`/`vec-set-at!` onto a plain `let`-bound local Vec (`strides-for`'s/`zeros`'s
own `s`/`data`, carrying no type annotation of their own anywhere) never got boxed — the boxing
decision used to require a `g_vec_elem_hints` entry (only ever recorded for a `&(Vec T)` parameter
or a `(Vec T)` struct field), generalized to instead decide from the pushed *value*'s own already-
known resolved C type, which needs no hint at all.

**Real gap closed (2026-08-21)**: `get`/`set!` used to fail — `(Err (IndexError "out of bounds"))`
needed boxing (IndexError is a real, non-pointer struct), but neither function's own signature
carried an `Arena` parameter to box into at all. Resolved the same way `reshape` (below)/
`net/http.prn`'s `serve`/`string.prn`'s `parse-i32` already were: an explicit `dest : Arena @
Region` parameter added to both. This is a real, accepted cost of this language's own "no ambient
arenas anywhere" design (see `current-arena`'s own rejection elsewhere in this doc) — a read-only
accessor now takes an Arena solely to be able to report "out of bounds" — not a static/singleton
error-value convention, which would be a real, separate, un-attempted design direction if this
cost is ever judged too high. Both functions gcc-verified clean in isolation (isolated test file,
since the full `array.prn` itself hits a separate, unrelated, pre-existing gap further down —
`elementwise`/`add`/`mul-elementwise`'s own `fn` lambda-literal arguments, "unsupported expression
form," not yet designed at all).

**Real gap closed (2026-08-21)**: the `fn` lambda-literal gap named just above is now fixed —
`emit_expr()` generates a real, addressable, file-scope `static` C function per lambda literal
(`g_lambda_helpers`, the same real "can't emit a function DEFINITION inline at an expression's own
call site" shape `g_box_helpers`/`ensure_box_helper()` already established), with the call site
itself just referencing that function's own name — a real C function name already IS a valid
function-pointer value. Real, deliberately narrow scope, matching this language's own "no ambient
anything, explicit everywhere" convention: every lambda param needs an explicit `(name : Type)`
annotation (VS0 has no type inference at all, and this emitter has no expected-type context
threading into `emit_expr` either) — `add`/`mul-elementwise`'s own `(fn [x y] ...)` rewritten to
`(fn [(x : F64) (y : F64)] ...)` to match. No real closures either: the generated helper is a
genuine top-level `static` function, which — same as any hand-written C function — can't see the
enclosing PARENA function's own locals; an attempted capture fails honestly at the gcc stage
("use of undeclared identifier"), not silently miscompiled. Real, self-caught bug along the way:
the fix was first placed AFTER the generic symbol-headed-call dispatch in `emit_expr`, which
matches ANY symbol-headed list including `fn` and unconditionally mangled it into a bogus call to
a never-defined `fn(...)` C function — the new code was unreachable dead code until moved above
that catch-all. Verified gcc-clean AND at real runtime (a standalone harness calling the generated
functions directly, confirming `3+4=7`/`3*4=12` through the generated lambda, not just a clean
compile).

**Real, NOT-yet-fixed gaps found past this point, `elementwise`/`add`/`mul-elementwise`'s own,
separate from the lambda-literal gap just closed**: the full `array.prn` still doesn't compile
end to end. Three further, genuinely separate issues, found compiling past the now-fixed line:
(1) `same-shape?` calls `vec-eq?`, which has no real runtime implementation anywhere
(`parena_runtime.h` has no `vec_eq_`) — a real, unstarted Vec-equality feature. (2) `(not ...)` —
`elementwise`'s own `(if (not (same-shape? a b)) ...)` — isn't a known operator to
`binop_c_symbol()`, so it falls through to `emit_call()` and mangles into a bogus call to a
never-defined `not(...)` C function; VS0's own unary-`not` support doesn't exist yet (`(not x)`,
distinct from `!`/`&mut`'s own reference-marker meaning, which is real, separate syntax already
handled elsewhere). (3) `elementwise`'s own `(vec/push! &out (op ...))` — pushing a first-class
function CALL's own return value onto a Vec — doesn't get scalar-boxed even though `op`'s real
return type is `F64`/`double`: the boxing decision in `emit_call()`'s own argument loop only fires
when the pushed value's `arg_type` (from `emit_expr`) is literally the string `"int"`/`"double"`,
but a first-class call through a `(Fn ..)`-typed value (the `"((expr) arg1 arg2 ...)"` branch in
`emit_expr`) always reports `"void *"` as its own out_type — the same real, honest "no function-
signature table for indirect calls" limitation that branch's own header comment already names.
None of these three are attempted here — flagged, not silently worked around.

**All three real gaps closed (2026-08-21)**: `array.prn` now compiles end to end,
`gcc -Wall -Wextra -pedantic -Werror` clean, no exceptions. (1) `vec-eq?`: a generated,
per-scalar-element-type comparison helper (`static inline int Type_vec_eq(Vec *, Vec *)`), the
same "compiler generates a per-type helper, deduped" shape `g_box_helpers` already established —
found via the same `g_vec_elem_hints` registry `vec_get`'s own element-type reporting already
reads. A non-scalar (pointer-representable) element type is real, separate, un-attempted work,
reported as an honest compiler error rather than a real-but-possibly-wrong pointer comparison.
(2) `(not x)`: emits as a real C `(!(...))` expression, checked in `emit_expr` before the generic
symbol-headed-call dispatch (the same placement fix the `fn`-literal gap above needed). (3) the
indirect-call boxing gap: `emit_call()`'s own generic "void *" fallback now first checks whether
the callee is a scope-bound local whose own resolved C type is a function-pointer shape (`"RetType
(*)(ArgTypes)"`) and, if so, reports the real return type extracted from it — letting `vec_push_`'s
existing scalar-boxing decision see a real `"double"` instead of the generic guess.

**A fourth, genuinely separate bug found by real RUNTIME verification, not a compile check**
(2026-08-21): getting `array.prn` to compile clean was not the same as getting it CORRECT — a real
harness calling `zeros`/`set!`/`get`/`add` end to end found every summed value landing on the same
slot instead of each cell's own real value. Root cause was in `strides-for`'s own source, not the
compiler: computing `stride[i] = product(shape[i+1:])` genuinely has to walk dimensions
right-to-left, but the old body pushed each computed stride straight onto the result Vec in that
same right-to-left order — Vec has no insert-at-front, only append, so the result ended up
`[stride[n-1], ..., stride[0]]`, backwards from what `flat-index`'s own `(vec/get strides i)`
expects. Fixed with a real reformulation (not a two-pass reverse): since `stride[i-1] = shape[i] *
stride[i]`, walking `i` forward from 0 with `running` seeded at `product(shape)` and divided by
`shape[i]` at each step yields each `stride[i]` directly, in the correct order, on the first pass.
A first attempt at a two-pass reverse-then-reread fix hit two SEPARATE, real, still-open compiler
gaps instead — flagged here rather than fixed, since the single-pass reformulation above sidesteps
both cleanly: (a) two sibling, non-nested `loop` forms in the same function both declaring a C
local named `i` at the same block scope collide (`redefinition of 'i'`) — this emitter doesn't
currently scope each loop's own C declarations to avoid that; (b) `vec/get` on a plain `let`-bound
local Vec (no recorded `g_vec_elem_hints` entry outside a typed parameter/struct-field) falls back
to a raw `void *`, which `deref` then tries to dereference directly — invalid C. Verified not just
gcc-clean but numerically correct at real runtime: a standalone harness building 2×3 `NDArray`s,
running `set!`/`get`/`add`/`mul-elementwise`/`reshape`/`same-shape?` end to end, confirms every
real expected value, not just a clean compile.

**`linalg` real progress and a real, significant, NOT-yet-fixed gap found (2026-08-21)**:
gcc-verifying `matmul`/`transpose`/`dot` surfaced three real, separate things.

(1) **Real emitter gap closed**: `[e1 e2 ...]` — a Vec LITERAL used directly as a value (both
`matmul`'s own `(array/zeros [a-rows b-cols] dest)` and every `[i j]` index literal) — had no
value-position handling anywhere in `emit_expr` at all (`NODE_VEC` only had PARAMETER-list
handling), an honest "unsupported expression form." Fixed via `g_veclit_helpers`, the same real
"compiler generates a helper function" architecture `g_box_helpers`/`g_lambda_helpers`/
`g_veceq_helpers` already established: one fresh, addressable `static` C function per literal
(no dedup — two literals at different call sites aren't interchangeable) that allocates via
`vec_new(dest)` and pushes each element, boxing I32/F64 scalars the same way `vec/push!` already
does. The Arena is found via the same `find_dest_arena()` scope search Ok/Err/Some's own boxing
already uses.

(2) **Real emitter gap closed**: `unwrap` — real call sites in `linalg.prn`
(`(unwrap (array/get a idx))`), `ringo.prn`, and `nn.prn` (`(unwrap (stats/min data))`) — was never
defined anywhere. Real Rust-style `.unwrap()` semantics: aborts with a real stderr message on
Err/None (`result_unwrap_check`/`option_unwrap_check`, two new `parena_runtime.h` helpers), unboxes
the payload otherwise. The one real design constraint: VS0 has no generics, so a `Result`/`Option`'s
own payload type is erased everywhere *except* at a known `defn`'s own registration — `unwrap` is
therefore scoped to a DIRECT call to a known, already-registered top-level function (the exact real
shape every actual call site uses), not an arbitrary expression; `g_defn_return_types` grew a
`payload_type` field (resolved once, at the same two spots the return type itself already is)
purely to support this.

(3) **Real, significant, NOT-yet-fixed gap found via real runtime verification, not a compile
check** — the exact same "compiling clean isn't the same as being correct" lesson `strides-for`
already taught this same session: a harness computing a real 2×3 matmul against known values got
`C[0][0]` right and every other cell silently wrong (0). Root cause, found by inspecting the
generated C directly: `(loop [i 0] ...)`-style loop variables are declared as C `double` — not
`int` — because a bare integer literal like `0` always reports its own type as `"double"`
(`NODE_NUMBER`'s own emit_expr case, documented as "VS0 has no int-vs-float distinction yet — a
real, honest simplification," a whole-language, deliberate tradeoff, not a bug in itself). That's
silently fine for arithmetic and comparisons (C's own usual conversions paper over it) but genuinely
wrong the moment such a loop variable is BOXED into a Vec meant to hold `I32` values (e.g. as an
array index, via the new `g_veclit_helpers` above): it gets `vec_box_f64`'d (8-byte double), then
read back elsewhere via `flat-index`'s own `(int *)`-cast `deref` — reinterpreting 4 of those 8
bytes as a raw `int`, real memory-level type confusion, not merely "wrong value." **Deliberately
NOT fixed here**: the real fix touches loop-variable (or number-literal) type inference broadly —
a genuinely cross-cutting change with real regression risk across all ~51 real `(loop [...] ...)`
call sites in this stdlib and everything this session already gcc/runtime-verified, not a narrow,
contained fix like every other gap this pass closed. `matmul`/`transpose` compile
`gcc -Wall -Wextra -pedantic -Werror` clean (the `array/get`/`array/set!` call sites were updated
to pass their own new `dest` argument, a real, separate, necessary fix, kept) but are **not**
verified numerically correct and should not be treated as done — `dot` (verified: real
`{1,2,3}·{4,5,6} = 32`, no index-Vec construction involved) is the one function in this file
confirmed both gcc-clean and correct.

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

**`column` gcc-clean, `select` blocked on a new, real, deeper gap (2026-08-21)**: `column`'s own
`(Err (ColumnNotFoundError name))` needed the same real `dest : Arena @ Region` fix `array.prn`'s
own `get`/`set!` already had — closed the same way, `ColumnNotFoundError` itself defined (the same
real missing-definition class already closed elsewhere). A real, separate, previously undocumented
**general control-flow gap closed along the way**: `select`'s own `(return (Err e))` — bailing the
whole function early from inside a `loop`'s own `match` clause on the first error, not just this
one iteration — used `return`, found genuinely never implemented anywhere (`firefly.prn`'s own
header comment already names this as a known missing primitive). Real, and simpler than it first
looks: a plain C `return` statement already exits the enclosing function immediately regardless of
loop-nesting depth — normal C semantics, no propagation logic needed (unlike `recur`, which
genuinely does need one, since a PARENA `loop` is a real C `while(1)`, not a native early-exit
target). Verified both gcc-clean and correct at real runtime (a harness confirming the early-return
path fires exactly when expected, and the fall-through path still completes the whole loop when it
doesn't).

**`column` now real, gcc-clean, AND runtime-verified; `select` still blocked, now on a THIRD, real,
separate gap (2026-08-21, continued)**: the pointer-representable-Vec-element investigation above
led to two real, general, self-caught emitter bugs, both now fixed:

1. **The single-token `&name` form's own hint lookup never matched its own registration key.**
   `vec_call_target_hint()`'s own `&`-unwrap only ever handled the two-SIBLING-NODE `& (expr)` form
   (e.g. `&(get-field a :data)`) — the far more common single-TOKEN `&name` form (no space, e.g.
   `&names`/`&v`/`&exps`) fell through to the generic `emit_expr()` call instead, which has its
   OWN, separate, unrelated real handling for a bare `&x` token — returning `"&(%s)"`-wrapped text
   (e.g. `"&(names)"`) as the lookup key, a real, different string than the plain `"names"` a hint
   was ever actually REGISTERED under. The lookup silently asked the hint table the wrong question
   — not because no hint existed, but because the key never matched — so `(vec/get &names i)`
   always missed its own real, correctly-registered hint. This bug predates today entirely; it
   simply never got exercised by any single-token `&name`-shaped `vec/get` call site this session
   had already gcc-verified (every one so far used either the two-node form or no `&` at all, the
   Vec already being a reference).

2. **`vec_get`'s own hint-informed cast added a wrong extra level of pointer indirection for a
   pointer-representable element.** A scalar (I32/F64) element is genuinely boxed (`vec_box_i32`/
   `vec_box_f64`), so the Vec's own stored item IS a pointer to an arena-allocated cell — casting to
   `"ElemType *"` and `deref`-ing it back is correct. A pointer-representable element (String ->
   `char *`, or any registered struct/enum) is NEVER boxed at all (`is_boxable_struct`'s own check
   explicitly excludes anything already ending in `'*'`, "already directly usable as `void *`") —
   the Vec's own stored item genuinely IS the raw pointer itself, so the old, uniform `"%s *"`
   formula silently added a SECOND, wrong level of indirection (`"char * *"`), producing a real,
   invalid cast — the deeper root cause behind the `select`-blocking error reported previously.
   Fixed: `vec_get`'s own reported type is now `hint->elem_type` directly when that type is already
   a pointer, no extra `" *"` — the correct, matching cast for a value that was never boxed. Real,
   deliberate consequence: `deref` on such a value now fails HONESTLY (a real, correct type
   mismatch — dereferencing an already-final `char *` gives a single `char`, not the string), rather
   than silently emitting invalid C. `column`'s own real source updated to match (String reads used
   directly, no `deref`) and re-verified both gcc-clean and correct at real runtime (found/not-found
   cases, confirmed against a real `DataFrame`).

**`select` now real, gcc-clean, AND runtime-verified too — the third AND a fourth gap, both closed
(2026-08-21, continued)**: past both fixes above, `select` hit a genuinely THIRD, separate gap:
`resolve_declared_type()` didn't understand a parenthesized `(&Type)` reference nested inside a
`Result`/`Option`'s own payload-type slot (`column`'s own real return type is `(Result (&Column)
ColumnNotFoundError)`), so the real, general extension added earlier this session (`unwrap`'s own
payload-type lookup, generalized so a `match` on a direct call to a known function can type an
`Ok`/`Some` clause's own bound value the identical way) couldn't resolve `column`'s own payload type
either, and `col` inside `select`'s own match clause stayed a generic `void *`. **Fixed**: when
`resolve_declared_type()` receives a `NODE_LIST` holding exactly one child that's itself a
single-token `&Type` symbol, the parens are redundant — it now just recurses into itself on the
inner symbol, reusing the already-correct real logic the bare (unparenthesized) `&Type` case
already has, rather than duplicating it.

That fix alone still weren't enough — surfaced a real, FOURTH, separate, self-caught bug: `select`'s
own `Ok` clause body is `(do (vec/push! ...) (vec/push! ...))`, a real, honest `void`-typed tail
(`vec_push_` is one of the few runtime calls this emitter tracks as genuinely returning C `void`).
`emit_match_clause_body`'s own plain-value fallback used to unconditionally ASSIGN a clause's value
into `result_var` — real, invalid C the instant that value is `void` ("void value not ignored as it
ought to be"). Fixing that alone surfaced a second layer of the same problem: `result_var` itself
used to be DECLARED with the literal type `"void"` in this case, and C simply has no valid `void
result_var;` declaration at all ("declared void") — a real, KNOWN type that just isn't declarable as
a local variable, distinct from the separate "genuinely unknown type" (`NULL`) case already handled.
Both fixed: a void-typed clause value is now emitted as a bare `(void)(...)` statement instead of an
assignment (never touching `result_var` at all), and `result_var`'s own declaration uses a real,
valid, inert placeholder type (`int`) instead of the undeclarable literal `"void"` whenever the
clause's own real type is void — the same fix applied to both `emit_match_core`'s own real body and
`emit_match`'s own top-level return-mode handling (a `void`-typed match, like a `void`-typed loop,
now correctly falls off the end of its own enclosing function rather than attempting a `return`).

`select` is now fully verified: a real harness selecting a real multi-column subset (confirming
correct column values AND correct name ordering) and confirming the real `Err` path fires for an
unknown column name, both via the real, compiled `select` function, not a hand-written equivalent.

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

**Fully real, gcc-clean, AND runtime-verified (2026-08-21)** — every function in this file
(`layernorm`/`gelu`/`softmax`/`relu`/`leaky-relu`/`sigmoid`/`tanh-activation`) compiles
`gcc -Wall -Wextra -pedantic -Werror` clean and produces correct real numbers, confirmed against
independently-computed reference values (`relu`/`leaky-relu` against hand-picked inputs;
`sigmoid`/`tanh-activation`/`gelu` against libm's own `exp`/`tanh`; `softmax` against its own real
invariant, output summing to 1.0; `layernorm` against a hand-computed mean/variance/normalize).

Real, new gaps closed getting here, none specific to this file: (1) `exp`/`tanh` needed real
definitions — added as `exp-of`/`tanh-of`, real, deliberate `#target` FFI wrapping libm (founder,
real-time, this same session: "use your escape hatch when u need to" — unlike `stats/sqrt-of`, a
real exp/tanh implementation is genuine, separate numerical work, not a same-pass reference
formula worth hand-rolling). Named with the `-of` suffix, not the more obvious bare `exp`/`tanh`,
for the identical real reason `sqrt-of` already isn't bare `sqrt` — both are real gcc builtins,
and a same-named PARENA function's own forward declaration collides with gcc's own built-in
knowledge of them. Every generated file now unconditionally includes `<math.h>` (the same real,
honest tradeoff already made for `<stdint.h>`/`<stdlib.h>`). (2) `leaky-relu`'s own original
design captured its own `alpha` parameter inside an inline `fn` literal — a real closure, which
this session's own non-capturing `fn`-literal support can't express (the same real redesign
`firefly/ladybug.prn`'s own `equal`/`be-close-to` needed earlier this same session) — rewritten as
a real, separate, named `leaky-relu-fn` helper taking `alpha` explicitly, with `leaky-relu` itself
keeping its own real per-element loop (no closures anywhere means no real partial application
either, so it can't route through `map-elementwise` the way `sigmoid`/`tanh-activation` do). (3)
`softmax`'s own original body nested a `loop` directly inside a `let` binding value position (`(let
[total (loop ...)] ...)`) — the same real, still-open "loop as an arbitrary sub-expression" gap
`stats/std`'s own `sum-of-squares` extraction already worked around earlier this session; worked
around here the identical way, via two real, separate helper functions (`sum-vec`/
`divide-vec-by`) whose own tail position IS the loop directly. Those two helpers also incidentally
worked around a SECOND, real, separate, still-open gap while at it: `vec/get` on a plain
`let`-bound local Vec (no recorded element-type hint) falls back to a raw `void *`, which `deref`
then can't safely dereference — routing every real read of `softmax`'s own `exps` local through a
genuine `&(Vec F64)` PARAMETER (which DOES get a real hint) sidesteps it entirely. (4) a real,
general `vec_push_` boxing-decision bug self-caught along the way: a value's own reported type can
genuinely be the bare string `"void"` (an honest side effect of gap (3) above, before the
workaround), which the newly-generalized struct-boxing logic (see `firefly`'s own STDLIB.md entry)
briefly, wrongly treated as a boxable struct type, generating invalid C (`static inline void
*void_box(Arena *dest, void v)`) — excluded now, a narrow, defensive fix, not a fix for gap (3)
itself.

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

**Real VS0 emitter gap found and fixed compiling this file's own real implementation (2026-08-21)**:
`glob-match`'s whole body is a `cond` (`(cond (test1 result1) (test2 result2) ... (true default))`)
— Lisp's own classic multi-clause conditional, found genuinely never implemented anywhere in the
emitter at all (three more real, already-written stdlib files use this same shape: string.prn's
`split`, map.prn's `find-slot`, expr.prn's `apply-binop`). Before this fix, `cond` silently fell
through to the generic call path and mangled into a bogus call to a never-defined `cond(...)` C
function — `parena build`'s own exit code never caught it, only an actual gcc compile did. Fixed as
two real forms: a pure ternary-chain (folding right-to-left, matching `if`'s own composition) for
`cond` used as a plain value expression, plus a separate statement-level recursive composition for
`cond` in `loop`-tail position (needed because `recur` emits a real `continue;` statement, which can
never live inside a ternary — `string/split`'s and `map/find-slot`'s own real `cond` usage are both
this second shape).

**Second real gap, fixed in a follow-up pass (2026-08-21)**: `matches` calls `glob-match`, which is
defined LATER in the same file — the same real forward-declaration-pre-pass gap `string.prn` above
surfaced first, not specific to this file; closed by the same fix (a real forward declaration for
every `defn` carrying an explicit return type, emitted ahead of every defn body).

**Third real gap, found and fixed in a follow-up pass (2026-08-21)**: `glob-match`'s own
`(string/length pattern)` — a qualified call into string.prn's own real, already-defined `length` —
still failed with a NEW, different error once combined with string.prn via multi-file build:
`implicit declaration of function 'string_length'`. Real, structural, and much wider-reaching than
this one file: mangle() alone just blindly turns every `/` into `_`, so `string/length` became the
literal C identifier "string_length" — never what `length` (defined inside `(module string)`, but
never itself prefixed by that module name when compiled) actually compiles to. This compiler's
multi-file build has no real per-module C symbol table at all (every combined file's own top-level
forms share one flat C namespace), so a qualified call can only ever correctly resolve by falling
back to the bare, unqualified function name. Fixed via `mangle_call_name()`: try the bare (last
`/`-segment) name first, but only use it if a real defn by that exact bare name is already known
(via the same forward-declaration registry the second gap above populates) — otherwise fall back to
the old, full-text mangle unchanged, so already-working call sites (`vec/push!`, and `string/concat`
in a single-file build with no real `concat` combined in, which still correctly falls back to the
runtime's own hardcoded `string_concat` helper) are untouched. Confirmed via real usage counts this
affects far beyond glob.prn: `map/*`, `array/*`, `io/*`, `stats/*`, `sdl2/*`, `pty/*` qualified
calls appear throughout the stdlib, all previously mis-resolving the same way.

**Real gap closed (2026-08-21)**: `glob-match`'s own `char-eq?`/`char-at-eq?`/`match-bracket-class`
— the same real missing-definition-in-source-itself gap class already closed for pcap.prn/io.prn/
array.prn/string.prn — are now real, defined functions. `char-eq?`/`char-at-eq?` are thin wrappers
over `string/char-at` (a single-character glob token like `"*"`/`"?"`/`"["` arrives as a real
1-character String literal, this language having no separate Char type — see string.prn's own
header comment). `match-bracket-class` is a real, honestly-scoped bracket-class matcher: literal
character runs (`[abc]`) and one-character-wide contiguous ranges (`[a-z]`) freely mixed within one
class (`[a-cx-z]`), plus leading negation (`[!abc]`/`[^abc]`) — not a general regex-class grammar
(no escaped `]`, no multi-byte/unicode ranges). Two new internal helpers needed along the way:
`find-close-bracket` (the real index scan for the class's own terminating `]`) and
`bracket-class-matches?` (the real membership test). Verified gcc-clean AND at real runtime: a
17-case harness covering `*`/`?`/literal classes/ranges/negation/empty-pattern/exact-match all pass
their real expected result, not just a clean compile.

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
(defn serve [(port : I32) (handler : (Fn [&HttpRequest Arena] HttpResponse)) (dest : Arena @ Region)]
  : (Result Unit NetError))   ; matches signalapi/IDUNA's own "one handler fn per route" shape
```

Real, honest limitation, same pattern as `array`/`linalg`'s BLAS/LAPACK note above: real TLS
(`net/http`'s `get`/`post` against `https://` URLs, which IDUNA/signalapi both require in
production) needs a real TLS implementation underneath — not specified here, genuinely separate
work, most plausibly an FFI binding to a real C TLS library rather than a from-scratch
implementation, the same judgment call already made for `linalg`.

**Real VS0 emitter gaps found and fixed compiling this file (2026-08-21)**, both general emitter
gaps this file happened to be the first real source to reach: (1) `(Map K V)` as a struct-field
type (`HttpRequest`/`HttpResponse`'s own real `headers : (Map String String) @ Region` field) —
`resolve_declared_type()` handled `(Result ..)`/`(Option ..)`/`(Vec ..)` compound types but never
`Map` at all. Erased to a plain `void *`, not a named struct — real, deliberately narrower than
`Vec`'s own treatment: there's no real runtime `Map` struct backing it yet (`map.prn`'s own real,
intended implementation is itself blocked on real generics, a separate, much larger feature), so
this only lets a Map-typed field/return be NAMED, not constructed or manipulated. (2) A bare `Arena`
(no `@ region`) as a `(Fn [...] ...)` argument type (`serve`'s own real `handler` parameter:
`(Fn [&HttpRequest Arena] HttpResponse)`) — every OTHER real Arena usage in this emitter goes
through the separate `Type @ Region` / `has_region_marker()` path, which a `Fn`-type's own argument
list doesn't use; `resolve_base_type_name()`'s own bare-symbol table never included "Arena" at all.
Now resolves to a real `Arena *`, matching every other real Arena value's own C representation.

**Second real gap, closed in a follow-up pass (2026-08-21)**: `serve` calls `(net/tcp/listen port
dest)`, a real cross-module qualified call into `net/tcp.prn`'s own `listen` — `net/tcp.prn` itself
had its own, separate gap (`TcpListener`/`TcpStream`/`NetError` referenced throughout
`net/tcp.prn`/`net/udp.prn` but never defined anywhere in either file, the same real missing-
definition-in-source-itself gap class already closed for pcap.prn/io.prn/array.prn/string.prn), now
closed: both files get real, minimal, opaque-fd `defstruct`s (`TcpListener`/`TcpStream`/`UdpSocket`,
each just an `fd : I32`) plus a real `NetError` defenum, and `net/udp.prn` additionally gets a real,
minimal `SocketAddr` (`host`/`port`) struct. **Real, deliberate scope note**: `NetError` is defined
SEPARATELY in each of `net/tcp.prn`/`net/udp.prn` rather than shared via a common import — this
compiler's multi-file build has no real per-module C namespace, so combining ALL THREE of
`net/tcp.prn`/`net/udp.prn`/`net/http.prn` in one build would hit a real duplicate-definition error;
safe for every real, intended combination that actually exists (`net/http.prn` only ever imports
`net/tcp`, never `net/udp` at the same time).

Verified: `net/tcp.prn` and `net/udp.prn` each now compile standalone past their own type-definition
gaps (remaining errors are real, expected, un-implemented host FFI primitives — `tcp_listen`/
`tcp_accept`/`udp_bind`/etc. — the same "FFI declared, host implementation not written yet" boundary
every other `#target`-bound file in this stdlib already has). `net/http.prn`'s own `get`/`post`
(combined with `net/tcp.prn`) now compile all the way down to a THIRD, different, real gap: both
call `connect-from-url`/`build-get-request`/`parse-http-response`/`build-post-request` — real,
genuinely un-designed helper functions (URL parsing, HTTP request serialization, HTTP response
parsing) that would need designing essentially from scratch, a real, substantial undertaking well
beyond this pass's own scope, not attempted here.

**`current-arena` closed for `serve` (and `array.prn`'s own `reshape`) in a follow-up pass
(2026-08-21)**: both called the already-documented, never-designed `(current-arena)` builtin (see
`firefly.prn`'s own comment on that gap elsewhere in this doc — a deliberate design decision, not an
oversight: VS0 has no ambient/implicit arenas anywhere, every allocation traces to an explicit
`Arena @ Region` parameter, on purpose). Fixed the same real, already-established way `firefly.prn`'s
own `skip` was: `serve` now takes an explicit `dest : Arena @ Region` parameter (the caller starting
the server owns picking a real, long-lived region for it, since `serve` itself runs forever), and
`reshape` now takes the same `dest` parameter every other allocating function in `array.prn` already
does. Verified: `reshape` compiles real gcc-clean in isolation (6 of `array.prn`'s functions now
verified: `product`/`strides-for`/`zeros`/`flat-index`/`from-vec`/`reshape`).

**Real gap closed in a follow-up pass (2026-08-21)**: `serve`'s own accept-loop body — `(loop []
...)` used directly as a match clause's own body — used to be a real, separate gap in
`emit_match_clause_body`'s own statement-form dispatch (`if`/`let`/`do`/`match` were handled, `loop`
wasn't). Fixed by factoring `emit_loop_core()` out of `emit_loop()` itself (the exact same real
"share the outer's already-owned result_var, no fresh declaration" composition
`emit_match_core()`'s own nested-match case already uses) — a `loop`'s own final value now becomes a
real assignment into a match's shared result variable instead of a `return`. Verified via an
isolated repro (real gcc-clean). While gcc-verifying this fix, also found and fixed a real, SEPARATE,
general bug it surfaced: `emit_match_clause_body`'s own `let`/`do` handling processed non-last body
forms via raw `emit_expr()` — the same "no statement dispatch" class of gap already fixed elsewhere
— AND, once that was fixed, a discarded statement whose own value happens to be a bare variable
reference (or any side-effect-free expression) produced a real gcc `-Wunused-value` ("statement with
no effect") error; reproducible in a PLAIN function body with no match/loop involved at all,
confirming it was a real, general, pre-existing gap. Fixed by wrapping every discarded statement
this emitter produces in `(void)(...)`, the standard, idiomatic C way to mark a value as
deliberately discarded.

**Real gap closed in a second follow-up pass (2026-08-21)**: once `current-arena` was fixed above,
`serve`'s own `(loop [] (match (net/tcp/accept ...) ((Ok !conn) (do ... (recur))) ((Err e) (Err
e))))` — a `match` used directly as a `loop`'s own TAIL, with `recur` inside one of its clause
bodies — hit the identical class of gap ONE level up: `emit_loop_tail` understood `if`/`cond` in
tail position but not `match` at all. Fixed by recursing into `emit_match_core()` from
`emit_loop_tail`'s own new `match` case, threading the loop's real `loop_locals`/`loop_var_count`
through so `recur` inside a match clause body (a new case added to `emit_match_clause_body` itself)
correctly continues the right loop. A real, self-caught bug surfaced alongside this: a clause
resolving to a plain terminal value previously only assigned into `result_var`, never `break` — fine
for a standalone match, but when nested inside a loop's own tail this left nothing to stop the
enclosing `while(1)`, silently looping back around instead of terminating; fixed by emitting `break`
whenever a real loop context is present. Verified via an isolated repro (real gcc-clean) and via the
full `net/tcp.prn` + `net/http.prn` combined build: `serve` now compiles with `parena build` and gcc
reports only the already-documented FFI/helper-function gaps above — no remaining structural/
compiler errors at all.

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

**Renderer/draw calls: CLOSED 2026-08-26** — real host implementation shipped (PARENA commit
`7968229`), founder real-time: "continue working on parena editor." `create-renderer`/
`destroy-renderer`/`set-draw-color`/`render-clear`/`render-fill-rect`/`render-present` are real,
matching `pty.prn`'s own established raw-primitive-returns-scalar shape; `Window`/`Renderer` are
real opaque `I32` handles into a real host-side table (`runtime/parena_runtime.h`'s
`g_sdl2_windows`/`g_sdl2_renderers`) rather than a raw-pointer struct field — see that file's own
header comment for why. Verified with a real end-to-end test (`tests/test_sdl2.c`, `make
test-sdl2`) that opens a real window under a real (self-launched, scratch) Xvfb X server, creates a
real renderer, and draws a real multi-frame PITVIPER-shaped cell grid (`SetDrawColor`+`FillRect`
per cell, matching `cmd/pitviper/main.go`'s own `renderFrame`) — clean under ASan+UBSan.

**Text rendering: CLOSED 2026-08-26, same day** — real SDL2_ttf host implementation shipped
(PARENA commit `55a70fc`), same founder direction, continued. `ttf-init`/`ttf-quit`/`open-font`/
`close-font`/`render-text`/`measure-text-width`/`measure-text-height` are real, using PITVIPER's
own real font (JetBrains Mono, the same font its own F11 "shiny font" toggle loads via SDL2_ttf).
`Font` is a real opaque `I32` handle, same shape as `Window`/`Renderer`. `render-text` is a real,
honest v0 — a fresh surface→texture→blit→free every call, not a glyph-atlas/texture cache (real,
separate future work once an editor loop's own frame budget actually needs per-glyph caching,
PITVIPER's own `buildGlyphAtlas` the real precedent for that). Also confirmed live, not assumed:
VS0's emitter does not support tuple return types yet (a real "unsupported return type form"
error), so `measure-text-width`/`measure-text-height` are two separate calls rather than one
`(I32 I32)`-returning one — worth knowing for any future package design in this document.
Verified with a real end-to-end test loading the real font, confirming a real nonexistent-path
open correctly fails, drawing real text, and confirming real positive glyph-cell measurements —
clean under ASan+UBSan.

**Real, honest limitation, still open**: game controller support (`SDL_GameControllerOpen`/
`GetAxis`/`GetButton`, real, grepped, used by BRAWLPIT/REDGARDEN/GoblinFoxDragon) is still out of
scope — an editor shell doesn't need it, flagged for whichever future program actually does.

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
ncurses+Tree-sitter — as **"Not started, not in VS0... an open, undecided question."**

**`editor/buffer`: the shell question is answered, real host implementation shipped 2026-08-26**
(PARENA commit `ca10ff6`) — the shell IS SDL2 (`stdlib/sdl2.prn`'s own real window/renderer/text
work, shipped the same day), and `editor/buffer.prn` is the real, first in-PARENA text buffer it
edits. Real design departure from the sketch below, forced by two real, confirmed VS0 emitter
limits: no struct field mutation exists yet (no `set-field!` anywhere in this compiler), so
`Buffer{text, cursor}` is never mutated in place — `insert`/`delete-range`/`insert-at-cursor`/
`backspace-at-cursor` all return a real, NEW `Buffer` via `Result`, the caller rebinds its own
local. Tuple return types aren't supported either (a real "unsupported return type form" error,
confirmed live) — `selection : (Option (I32 I32))` below doesn't compile as written and was
dropped for v0 (nothing calls it yet); `cursor-pos` (a plain `I32`) replaces it as the real,
minimal thing a keyboard-driven single-line edit loop actually needs. Verified with a real
end-to-end test (`tests/test_editor.c`, `make test-editor`) that drives a real edit sequence
through SDL's own real event queue (`SDL_PushEvent`) and confirms the buffer holds the real,
correct final text. `editor/plugin`/`editor/events`/`editor/ui` below are still real,
separate, unstarted work — not attempted in this pass, no reason yet to revisit their own
design sketches.

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

**Real VS0 emitter gaps found and fixed compiling this file (2026-08-21)** — none specific to
`shell` itself, general emitter gaps this file happened to be the first real source to reach: a
NESTED `match` used as another match's own clause body (`resolve`'s own real policy chain:
`(match explicit (... s) (None (match (getenv "SHELL") (... s) (None ...))))` — a real, idiomatic
"chain of Option checks, fall through on None" pattern). The original clause-body emission called
`emit_expr()` directly, with no handling for `match` as a bare value, so the nested match mis-parsed
into a baffling "unknown identifier" error far from the real cause. Fixed by refactoring
`emit_match()` into a public entry point (owns the one real result-variable declaration) plus a
reusable core the clause-body composer recurses into directly, targeting the SAME, already-owned
result variable — no second declaration, the same real "declare once, learn the type from every
branch including nested ones" property `if`/`cond` already have elsewhere in this emitter. The exact
same underlying "raw `emit_expr()` where a statement dispatch is needed" gap was found a THIRD time
in `emit_loop_tail`'s own `when` handling (a non-last body form that's itself statement-shaped, e.g.
a `match`) — fixed by delegating those non-last forms to `emit_body()` itself instead of hand-rolling
a second, narrower dispatcher.

**Second real bug found and fixed in a follow-up pass (2026-08-21)**: this file's real body called a
real function literally named `getenv` — a genuine naming COLLISION with libc's own real `getenv`
(declared in `<stdlib.h>`, unconditionally included in every generated file), producing a real
"conflicting types" gcc error; a real bug in this file's own source, not a compiler gap. Renamed
`env-lookup`, matching `lookup-path`'s own real naming style right below it in the same file.

**`pty`'s own missing `Pty`/`PtyError` closed in the same follow-up pass**: every function in `pty`
used `Pty`/`PtyError` in its own real signature but the file never actually defined either — the
same real missing-definition-in-source-itself gap class already closed for pcap.prn/io.prn/
net/tcp.prn. `Pty` is a real, minimal, opaque fd wrapper, same shape as those files' own handles.

**CLOSED 2026-08-26** — real host implementation shipped (PARENA commit `d069439`), founder
real-time: "work on the pure parena editor port of pitviper in the parena repo." Both files
rewritten to `net/tcp.prn`'s own established raw-primitive-returns-scalar shape;
`runtime/parena_runtime.h` gained real `forkpty`-based `pty_open_impl`/`pty_read_impl`/
`pty_write_impl`/`pty_resize_impl`/`pty_close_impl` and real `env_get_impl`/`exec_lookpath_impl`/
`file_exists_impl`. Verified with a real end-to-end test (`tests/test_shell.c`, `make test-shell`)
that actually forks a real bash process attached to a real pty, writes a real command, and reads
real output back — not a mock — clean under ASan+UBSan. Two general VS0 emitter gaps found along
the way (both worked around at the stdlib level, not fixed in the compiler): top-level *private*
helper names aren't module-scoped either (only exported ones are — `pty.prn`'s own private
`raw-open` etc. collided with `io.prn`'s identically-named private helpers once compiled together,
fixed via a `pty-raw-` prefix), and bare `defenum` variant constructors resolve globally,
last-declaration-wins, not scoped to their own enum (`io.prn`'s `IoError.Other` and this file's own
new `PtyError.Other` collided the same way, fixed by naming the latter `PtyIoError` instead). See
`tests/test_shell.c`'s and `stdlib/pty.prn`'s own header comments for the full detail.

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

### `firefly` / `firefly/ladybug` / `scarab` — testing, Go's package + Ginkgo/Gomega on top, beetle-named

**2026-08-20 update**: `firefly/gomega` renamed to `firefly/ladybug` — "gomega" broke this very
section's own beetle-naming convention (it's Go's own library name, not a beetle); ladybugs
(*Coccinellidae*) are real beetles and a direct pun (a *ladybug*-family matcher library is the
thing that finds *bugs*). `firefly/gomega` is kept as a back-compat alias with its exported API
unchanged. The whole framework has also been published as its own standalone repo,
`github.com/emilyspringerton/ladybug` (golden-indexed `LADYBUG-NORTH`), the same way Ginkgo and
Gomega are separate repos from the Go toolchain — this section stays the design source of truth,
that repo is the publish target.

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

**`firefly` + `firefly/ladybug`: now real, gcc-clean, AND runtime-verified (2026-08-21)** —
founder, real-time: "can i please start to see some ladybug scarab tests too please?" A real,
complete example test suite now exists and actually passes: `examples/stats_ladybug_test.prn`,
4 real BDD-style cases against `stats.prn`'s own `mean`/`std`/`min`/`max`/`sqrt-of`, run through
`firefly/run-tests` for real — `passed=4 failed=0 skipped=0`, confirmed at runtime, not just a
clean compile (and confirmed the matcher chain has real teeth: a separate harness deliberately
fed `equal`/`be-close-to` values that should fail, and both were correctly caught).

`firefly/ladybug`'s own design above is `&Any`-typed (a real matcher has to work across every type
under test) — genuinely unbuildable as written: VS0 has no `Any` reference type or generics, and
`deep-eq?`/`is-none?` were never defined. **Specialized to F64** instead (this session's own
verified numeric stdlib work — array/stats/linalg — is entirely F64-valued, so this is real,
useful scope, not a toy): `equal`/`be-close-to` (a real, standard float-tolerance matcher, the
`BeNumerically("~", x, delta)` every real matcher library needs the moment it compares an
irrational/computed value like `std`'s own sqrt-derived result). `be-nil`/generic `&Any` support
remain real, separate, un-attempted work, blocked on VS0 growing generics.

A second real redesign was needed along the way: the original `equal`/`be-close-to` returned
CLOSURES (`(fn [actual] (= actual expected))`, capturing `expected`) — this session's own `fn`-
literal support deliberately builds non-capturing lambdas (a generated lambda is a real top-level
`static` C function, which can't see an enclosing function's own locals), so this never compiled.
Real closures are a genuinely separate, large feature (VS0's whole `(Fn [..] ..)` C representation
is a bare function pointer everywhere else, not a closure object) — not attempted. Matchers are
data instead: a real `Matcher` tagged union (`Equal`/`CloseTo`), with `to` pattern-matching on it
via a real `match` — same visible call-site shape (`(to &exp &(equal y dest))`), no closures
anywhere.

**Four real, general VS0 emitter gaps closed getting this example to compile and actually pass**,
none specific to this test file — general gaps it happened to be the first real source to reach:
(1) multi-field defenum pattern DESTRUCTURING in `match` (e.g. `(CloseTo expected tolerance)`) —
construction has supported 2+ real fields since earlier this session, but a match clause's own
pattern parsing only ever captured the FIRST bound name; a second bound name fell through to
"unknown identifier." (2) a single-field defenum pattern's own bound value is now scope-tracked
with its real field type (not a generic `void *`) when that type is known, so a real `(deref ...)`
at the use site — the same established convention `dataframe.prn`'s own `(deref col)` already
relies on — correctly casts through to the real type instead of comparing `double` against
`void *`. (3) a bare symbol naming a real, known, already-registered top-level `defn`, used as a
VALUE (not called) — e.g. `{:run test-mean-of-known-values}`, assigning a named test function to
`TestCase.run` — had no handling; `scope_lookup` only ever finds parameters/locals, never
top-level functions. A real C function's own bare name already IS a valid function-pointer value,
the same real fact a generated lambda's own name already relies on. (4) `vec/push!` onto a Vec of
real STRUCT values (not scalars) — e.g. pushing a constructed `TestCase` — was never boxed at all;
the boxing decision only ever fired for the literal strings `"int"`/`"double"`. Generalized to use
the same `ensure_box_helper()` machinery Ok/Err/Some already rely on for any non-pointer,
non-scalar value type, found via the same `find_dest_arena()` scope search.

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

**`compress/lz4`** — depends on `vec` only. LZ4 is picked specifically, not zstd/gzip, because the
founder's own ask was about keeping a live, interactive protocol *fast*, and LZ4's real, well-known
niche is exactly "favor compression/decompression speed over ratio" for latency-sensitive links,
not archival size. Original design here specified an FFI-bound wrapper around the real, tiny,
widely-embedded LZ4 reference implementation (`String`-in/`String`-out) — founder, real-time,
overriding that: **"but it needs to be a pure parena implementation."**

**Now real, gcc-clean, AND runtime-verified (2026-08-21)** — the real, structural blocker the
FFI-bound design above was originally scoped around (`Vec` storing `void *` items, no real
boxing/unboxing path for raw `I32` byte values) is closed, by this same session's own earlier work
(the general scalar-boxing widening, plus the `vec_get`/hint-lookup fixes). A real, pure-PARENA
LZ4-STYLE compressor now exists — literal runs + back-reference matches, the same real idea LZ4's
own format is built on:

```clojure
(defstruct Token (offset : I32) (match-len : I32) (literal : I32))
(defn compress   [(input : &(Vec I32)) (dest : Arena @ Region)] : (Vec Token) @ Region)
(defn decompress [(tokens : &(Vec Token)) (dest : Arena @ Region)] : (Vec I32) @ Region)
```

Real, honest scope, distinct from the original `String`-in/`String`-out design above: this is a
real LZ4-STYLE compressor, not a byte-for-byte reimplementation of liblz4's own wire format (its
own frame headers, block-size negotiation, and literal/match-length bit-packing remain real,
separate, un-attempted work) — `Token` is this file's own real, honest intermediate
representation. Match-finding is real and correct but naive (brute-force scan over every earlier
position, O(n²)) — the same "correct first, fast later" judgment this whole stdlib already uses
throughout (`linalg`'s own naive `matmul`, `sort`'s own insertion sort) — a real hash-chain match
finder is a genuine, separate performance follow-up once something real needs it. Verified via a
real round-trip harness: repeated phrases, run-length/overlapping-copy data (offset < match-len,
the trickiest real LZ4 case — confirmed correct), mostly-unique text, and edge cases (empty input,
single byte) all round-trip byte-exact; a 100-byte highly repetitive input compresses to 3 tokens,
confirming real compression, not just correctness. A real, general emitter gap surfaced and fixed
along the way: `&mut (ComplexType)` (e.g. `copy-match`'s own `&mut (Vec I32)` parameter) had no
real parameter shape to compile through at all — the two-token `&mut Type` branch only ever
accepted a single-symbol target, and the `&(ComplexType)` branch only ever accepted bare `&`, never
`&mut`, as its own leading token. A second, separate, self-caught bug: a `loop` used as a
function's own tail whose own body never resolves a real terminal value on any path (a `when`-only
tail where every branch either recurs or stops, `copy-match`'s own exact shape) used to still
unconditionally emit a `return` statement, returning a genuinely uninitialized value from a
`void`-declared function — fixed in both `emit_loop` and `emit_match`.

`pitviper/protocol/request`'s own real wire format would still run every `Response` through
`compress/lz4`'s `compress` before sending, `decompress` on receipt — real, direct application of
"lz4ify the fuck out of everything" — though the wire-format integration itself (converting a real
`String` payload to/from `(Vec I32)` bytes at that boundary) is real, separate, un-attempted work.

**Real, honest limitation**: the server daemon's own file-system-watching/live-update behavior
(does the GUI tree refresh automatically on remote changes, or only on demand?) is real, unresolved
product design, not answered here — same "flagged, not resolved" pattern as everywhere else in
this doc.

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

### `pentest/*` — Kali-equivalent toolkit, FFI-bound to the real, standard tools

Founder: "std libs for full pen test tools" → "backtrack" → "or whatever the newest swiss army
knife bootable linux" (BackTrack's real, current successor: **Kali Linux**, the real, standard
pentest distro) → "all the most popular tools" → "wireshark" → "all that shit" → "rainbow tables"
→ "built in to PARENA stdlib." Real, named prior art throughout, not invented tool names — every
package below FFI-binds the actual real tool Kali itself ships, same judgment as `ssh`/`crypto`/
`compress/lz4` elsewhere in this document. Standing note, not a strategic pivot: this is authorized-
testing tooling for EINHORN_INDUSTRIAL's own infrastructure (IDUNA, EINHORN_SURVIVAL, the various
nginx-fronted sites), same incidental-tooling role security-adjacent work has had all along, not a
change in what this org actually is.

```clojure
; pentest/scan — FFI-bound to nmap
(defn scan-ports [(target : String @ :region/scratch) (dest : Arena @ Region)]
  : (Result (Vec PortResult) ScanError) @ Region)

; pentest/pcap — FFI-bound to Wireshark's own capture engine (tshark/libpcap, the same real
; library dumpcap and tshark are themselves built on)
(defn start-capture [(iface : String @ :region/scratch) (dest : Arena @ Region)]
  : (Result Capture PcapError) @ Region)
(defn read-packet   [(!cap : &mut Capture) (dest : Arena @ Region)] : (Option Packet) @ Region)
(defn filter        [(!cap : &mut Capture) (bpf-expr : String @ :region/scratch)] : (Result Unit PcapError))

; pentest/webapp — FFI-bound to sqlmap (SQL injection testing) + Nikto (web vuln scanning)
(defn sql-injection-test [(url : String @ :region/scratch) (dest : Arena @ Region)]
  : (Result (Vec Finding) WebScanError) @ Region)
(defn web-vuln-scan      [(url : String @ :region/scratch) (dest : Arena @ Region)]
  : (Result (Vec Finding) WebScanError) @ Region)

; pentest/wireless — FFI-bound to the Aircrack-ng suite
(defn capture-handshake [(iface : String @ :region/scratch) (bssid : String @ :region/scratch)
                          (dest : Arena @ Region)]
  : (Result Handshake WirelessError) @ Region)

; pentest/crack — FFI-bound to John the Ripper / Hashcat, plus real rainbow-table lookup
; (rcracki-mt-style: a precomputed hash-chain table traded for real disk space instead of
; per-attempt hash compute, the actual real technique named)
(defn crack-hash        [(hash : String @ Region) (wordlist-path : String @ :region/scratch)]
  : (Option String))
(defn rainbow-table-lookup [(hash : String @ Region) (table-path : String @ :region/scratch)]
  : (Option String))

; pentest/exploit — FFI-bound to Metasploit. The one genuinely sensitive package in this
; section, stated plainly rather than glossed over: this is real exploitation-framework
; tooling, authorized-testing-only by design and intent, same real-world norm every other
; Metasploit integration (including Metasploit's own official API) already operates under.
(defn run-module [(module-name : String @ :region/scratch) (target : String @ :region/scratch)
                   (dest : Arena @ Region)]
  : (Result ExploitResult ExploitError) @ Region)
```

Real, honest limitation, same pattern as the rest of this document: every tool above already has
its own real, mature CLI/library surface (nmap's XML output, tshark's own packet-dissection API,
Metasploit's own RPC API) — the actual FFI/subprocess-parsing glue code for each is real, separate
implementation work, not written here; these are the real call signatures a PARENA program would
use once that glue exists.

#### `yoko` — a real, PARENA-native module/payload framework, not an FFI wrapper (renamed from pentest/exploit/native)

Founder, real-time: "instead of calling out to c for all of the pentest tools" → "like we need
metasploit in PARENA" → "not calling out to it" → "its fint to get it to compile for now" → "but
we really need thhe actaul toools" → (clarified) "some of the underlying stuff" → "sure" → "call
out to c" → "you have to" → "we need to write in parena so we can compile in parena". Distinct
from `pentest/exploit` above (which stays exactly as designed — a real, useful FFI binding to
Metasploit's own mature module database, not being replaced): this is a real, native module/
payload TYPE SYSTEM and orchestration layer, grounded in Metasploit's own actual architecture
(a module has a target, a rank, options, a `check` and an `exploit` step; a payload has a
platform/arch and generates raw bytes; a successful exploit produces a session) — with only the
genuine low-level boundary (raw socket delivery, actual shellcode bytes) going through `#target`,
the same real "PARENA orchestrates, `#target` touches the actual wire" split every other FFI-
adjacent package in this document already uses (`gfd.prn`, `csv.prn`, `pentest/scan.prn`, etc.).

```clojure
(defenum Rank (Excellent) (Great) (Good) (Normal) (Average) (Low) (Manual))
(defenum Platform (Linux) (Windows) (MacOS) (Generic))

(defstruct Target       (name : String @ Region) (platform : Platform))
(defstruct ModuleOption (key : String @ Region) (value : String @ Region))
(defstruct Payload      (name : String @ Region) (platform : Platform) (bytes : (Vec I32) @ Region))
(defstruct ExploitModule (name : String @ Region) (rank : Rank) (target : Target)
                          (options : (Vec ModuleOption) @ Region))
(defstruct Session      (id : I32) (target : Target) (active : Bool))

(defn check            [(m : &ExploitModule)] : Bool)   ; real check-before-fire, #target at the wire
(defn generate-payload [(name : String @ :region/scratch) (platform : Platform) (dest : Arena @ Region)]
  : Payload @ Region)                                    ; real shellcode gen is #target, real host work
(defn exploit          [(m : &ExploitModule) (p : &Payload) (dest : Arena @ Region)]
  : Session @ Region)                                    ; real delivery is #target, real host work
```

`stdlib/yoko.prn` implements exactly this shape, real, `parena build`-verified
(compiles today) — `check`/`generate-payload`/`exploit`'s own bodies are `#target` stubs (the
"you have to call out to C for the underlying stuff" part, explicitly OK'd), while `ExploitModule`/
`Payload`/`Session`/`Rank`/`Platform` and everything that composes them are real, native PARENA
types the region analyzer and emitter actually check. Real, honest scope: the `#target` bodies are
stubs today (no real shellcode generation or wire delivery implemented) — the ask was explicitly
"fine to get it to compile for now," not a claim that this fires real exploits yet.

### `idvault` / `pitviper/expand` — real, deliberately deferred, not designed deep

Founder: "and vault integration for password management" → checked directly, confirmed real: IDUNA
already has a working Vault (`internal/vault/store.go`, real REST API — `/api/v1/vault/init`+
`unlock`+`lock`+`status`+`items` CRUD, client-side-encrypted `Item`/`RawItem`). Real signature only
(no deep design pass), per PITVIPER's own new §7 principle (docs/NORTHSTAR.md) the founder stated
immediately after: "keep it an agnostic tool until we really need to tighten all of those feedback
loops":

```clojure
(defn unlock [(password : String @ :region/scratch) (dest : Arena @ Region)]
  : (Result VaultSession VaultError) @ Region)
(defn get-item [(!sess : &VaultSession) (id : I32) (dest : Arena @ Region)]
  : (Result String VaultError) @ Region)
```

`pitviper/expand` — real precedent named nowhere directly by the founder but real and worth
citing: macOS's own Text Replacement / the real, popular open-source **Espanso** tool (trigger
string → expansion, works inside any terminal). Same deliberately-shallow treatment as `idvault`
above — real signature, not a deep design pass, until real usage justifies more:

```clojure
(defn register-snippet [(trigger : String @ :region/scratch) (expansion : String @ Region)] : Unit)
(defn expand-if-match   [(buffer : String @ Region)] : (Option String))
```

### `pitviper/tiling` — real i3wm interaction model

Founder: "add i3 window management affordances to the stdlib." Real, named precedent (i3 — the
real, well-known keyboard-driven Linux tiling window manager), applied to PITVIPER's own real
panes (already has a real pane concept per `docs/NORTHSTAR.md`'s own "native pane model... does
not wrap another multiplexer" design constraint — §5): i3's own real model is a tree of splits
(horizontal/vertical), each leaf a window, keyboard-driven focus movement and window movement
between splits, plus workspaces as named, switchable sets of that tree.

```clojure
(defenum SplitDirection (Horizontal) (Vertical))
(defenum Layout (SplitContainer (direction : SplitDirection) (children : (Vec Layout) @ Region))
                (Leaf (pane-id : I32)))

(defn split       [(!layout : &mut Layout) (pane-id : I32) (direction : SplitDirection)] : Unit)
(defn focus-move  [(!layout : &mut Layout) (direction : SplitDirection)] : Unit)   ; i3's own real h/j/k/l-style focus movement
(defn switch-workspace [(name : String @ :region/scratch)] : Unit)
```

Real, honest limitation: same deferred-depth treatment as `idvault`/`pitviper/expand` above, real
signatures only, not deep — PITVIPER's own real pane model already exists in Go (per its own
NORTHSTAR), this designs what a future PARENA-native tiling layer would look like once dogfooded
in, not a redesign of what's already shipped.

### `cache` — Russian-doll nested caching, Rails' own real precedent

Founder: "add russian doll nested cashing to the stdlib." Real, named prior art: Rails'
"Russian doll caching" — a fragment's own cache key is derived not just from its own identity and
version, but from every child fragment's *current* key too, so touching a child (bumping its own
version) changes the child's key, which changes the parent's composed key, which is a real cache
miss on the parent next time it's fetched — cascading invalidation for free, from key composition
alone, with no explicit "walk up and invalidate every ancestor" bookkeeping anywhere.

```clojure
(defn open       [(dest : Arena @ Region) (namespace : String @ :region/scratch)] : Cache @ Region)
(defn key        [(name : String @ :region/scratch) (version : I32) (children : (Vec String) @ Region)] : String @ Region)
(defn fetch      [(!c : &Cache) (k : String @ :region/scratch) (compute : (Fn [] String))] : String @ Region)
(defn invalidate! [(!c : &Cache) (k : String @ :region/scratch)] : Unit)
(defn touch!     [(!c : &Cache) (name : String @ :region/scratch)] : I32)   ; bumps + returns the new version
```

`key` is the real Russian-doll mechanism: it composes `name`/`version` with every string already
in `children` (each child's own most recent `key` result, collected by the caller) into one
composed key — a real, honest string composition (e.g. `name/version/child1-key/child2-key`), not
a cryptographic content hash; VS0 has no hashing primitive in scope yet, and a Rails-style
composed key is real, working prior art on its own without one. `fetch` is the classic
cache-fetch-or-compute-and-store idiom (`Rails.cache.fetch(key) { compute }`): look `k` up in `c`,
return it on a hit, otherwise call `compute`, store the result under `k`, and return it. `touch!`
is the explicit, real invalidation primitive for a leaf fragment that has no cache-derived key of
its own (e.g. a value read fresh from a database row) — bump its version so every parent whose
composed `key` call includes this fragment's name picks up a new value next call.

Real, honest limitation: the actual storage backend (in-memory map, on-disk, shared across
processes) is real, host-side, undecided work — same `#target` FFI-declared-here,
implemented-elsewhere shape every other stdlib package in this doc already uses for its own real
substrate (`thread`'s pthreads, `sql/driver`'s real driver). `(Vec String)`/`(Fn [] String)`-typed
parameters above are real VS0 gaps too (collections-as-parameters, and callback parameters with a
non-`Unit` return) — not glossed over, this doc's own job is to describe the target shape even
where the compiler doesn't reach it yet.

### `container/lxc` / `container/cgroup` — LXC + cgroups v2, real isolation primitives

Founder, real-time (from the Moltbook/OpenClaw hardening thread — `docs/NORTHSTAR_MOLTBOOK_
INTEGRATION.md` §4's "dedicated, non-privileged OS user/container" requirement): "we probably need
to bring in a round of hardening first" → "LXC primatives" → "container primatives" → "chgroups"
→ "built into the standard library" → "cli first" → "plan the stdlib deps" → "then march onward".
Planning pass only, per the founder's own "plan the stdlib deps" — not yet implemented.

Two real, separate, layerable primitives, not one blob:

**`container/lxc`** — high-level container lifecycle, FFI-bound to real `liblxc`
(`lxc/lxccontainer.h`, the same C API `lxc-create`/`lxc-start`/`lxc-stop` shell out to), matching
this doc's own established FFI-binding pattern (`sdl2`, `media/audio`, `pentest/*`, `git` above —
`#target` inline-C declares the real host symbols, this doc doesn't pretend a pure-PARENA
reimplementation of LXC itself):

```clojure
(defn create   [(name : String @ :region/scratch) (template : String @ :region/scratch)] : (Result Container LxcError) @ Region)
(defn start    [(!c : &mut Container)] : (Result Unit LxcError))
(defn stop     [(!c : &mut Container)] : (Result Unit LxcError))
(defn destroy  [(!c : &mut Container)] : (Result Unit LxcError))
(defn set-config [(!c : &mut Container) (key : String @ :region/scratch) (value : String @ :region/scratch)] : (Result Unit LxcError))
(defn running? [(c : &Container)] : Bool)
```

**`container/cgroup`** — real cgroups v2, direct filesystem interface (no liblxc dependency —
useful standalone for anything that just needs its *own* process resource-limited, like an
OpenClaw daemon, without a full container). Cgroups v2's real interface is a hierarchy of plain
files under `/sys/fs/cgroup/<name>/` — `cgroup.procs` (write a PID to join), `memory.max`,
`cpu.max`, `pids.max`, `memory.current` (read-only, for monitoring). This is real file I/O, so it
rides directly on the `io` package's `open`/`read`/`write` calls already designed above — not a
new I/O mechanism:

```clojure
(defn create-group  [(dest : Arena @ Region) (name : String @ :region/scratch)] : (Result CGroup LxcError) @ Region)
(defn set-memory-max [(!g : &mut CGroup) (bytes : I32)] : (Result Unit LxcError))   ; writes memory.max
(defn set-pids-max   [(!g : &mut CGroup) (max : I32)] : (Result Unit LxcError))     ; writes pids.max
(defn add-process    [(!g : &mut CGroup) (pid : I32)] : (Result Unit LxcError))     ; writes cgroup.procs
(defn current-memory [(g : &CGroup)] : (Result I32 LxcError))                       ; reads memory.current
```

Real, honest limitation, not glossed over: **both packages are currently blocked on the same
pre-existing gap already named elsewhere in this doc — VS0's C emitter has no working file-I/O
primitives yet** (the same caveat `csv.prn`'s own header carries: file I/O today only exists as
`#target` inline-C escape hatches, not real PARENA-native calls the region analyzer can reason
about). `container/cgroup` needs real file I/O by definition (writing `memory.max`, reading
`memory.current`); `container/lxc` needs a working FFI-call mechanism for non-`#target` scoped
calls into `liblxc`'s real C API surface (VS0's existing `#target` pattern *can* express this today
the same way `sdl2`/`pentest/*` already do — the harder ask isn't calling into `liblxc`, it's the
uid/gid/namespace privilege boundary itself, which is an OS-level concern, not a compiler one).
Nothing here needs a new compiler feature beyond what `io`/`#target` already cover once `io` itself
is real — this is a design-complete, implementation-blocked-on-`io` package, not a new gap.

Not designed here, flagged for later: raw namespace primitives (`clone`/`unshare` with
`CLONE_NEWPID`/`CLONE_NEWNET`/`CLONE_NEWUSER`/etc.) below LXC's own abstraction — LXC's own real C
API already wraps this, and nothing in the OpenClaw/Moltbook hardening ask needs going lower than
LXC's own container abstraction.

**`container/docker`** — founder, real-time: "docker APIS" → "parena underneath" → "into EMILY
os". A second, real, separate container backend alongside `container/lxc`, not a replacement —
LXC and Docker solve overlapping but distinct problems (LXC: lightweight OS-level isolation for
one long-lived daemon like an OpenClaw instance; Docker: portable, image-based deployment — the
real shape "Emily OS" (the planned Arch-based, PARENA-built, Raspberry Pi/k3s distro — real backlog
item, S189-56d) needs for shipping *workloads* onto a master+worker cluster, not just isolating
one process on this box). Real API surface: the Docker Engine API is a plain HTTP API over a Unix
socket (`/var/run/docker.sock`) — this rides directly on the `net/http` package already designed
above (a Unix-socket HTTP client is the one real gap `net/http`'s own design doesn't cover yet,
flagged here rather than silently assumed) rather than needing a new FFI binding the way
`container/lxc` does:

```clojure
(defn pull       [(image : String @ :region/scratch)] : (Result Unit DockerError))
(defn create     [(dest : Arena @ Region) (image : String @ :region/scratch) (name : String @ :region/scratch)] : (Result DockerContainer DockerError) @ Region)
(defn start      [(!c : &mut DockerContainer)] : (Result Unit DockerError))
(defn stop       [(!c : &mut DockerContainer)] : (Result Unit DockerError))
(defn remove     [(!c : &mut DockerContainer)] : (Result Unit DockerError))
(defn logs       [(dest : Arena @ Region) (c : &DockerContainer)] : (Result String DockerError) @ Region)
```

"PARENA underneath" / "the PARENA method": same discipline as every other package in this doc —
this is a real API design grounded in Docker's own actual Engine API shape, not a shell-out
wrapper around the `docker` CLI binary pretending to be a native binding. Same honest blocker as
`container/lxc`/`container/cgroup` above: needs `net/http`'s own Unix-socket gap closed first, on
top of the pre-existing file-I/O gap `io` itself still has. Design-complete, not a new compiler
gap — three real container-adjacent packages now queued behind the same one real prerequisite
(`io`/`net/http` landing for real), not three separate unstarted problems.

## VS0 compiler gaps blocking `mapbuilder/tools.prn` + `world.prn` — real, tested, itemized (2026-08-20)

Founder, real-time: "ship redgarden map editor" → "allowing building custom modes" →
"implemented into PARENAS as a plugin first redgarden feature" → "plan any missing stdlibs
first." REDGARDEN's map editor (and the custom-game-mode building it's meant to unlock, the
stated goal being closer to Warcraft 3's World Editor lineage than a fixed level format) is
scoped to be REDGARDEN's first real PARENA plugin feature, mod-surface-first per this whole
project's own standing architecture principle. `mapbuilder/tools.prn` and `world.prn` are
already real, complete `.prn` source (not aspirational notes) — this section is what actually
running `parena build` against them, today, found blocking full compilation, checked directly
rather than guessed at from reading the source alone. Each gap below is a real, itemized,
independent VS0 emitter feature, not a single monolithic "make it work":

1. **Multi-field `defenum` payload variants.** The first real blocker hit:
   `mapbuilder/tools.prn`'s own `CanvasCommand` has `(EraseCmd (obj : PlacedObject) (idx :
   I32))` — two payload fields. VS0's `defenum` (shipped this session) deliberately caps
   variants at one payload field, reusing Result/Option's own single `void *value` shape. A
   real fix needs a per-variant anonymous payload struct (reusing the `StructField`
   machinery `defstruct` already has) plus a real destination-arena argument on the
   constructor for the multi-field case, since the payload struct needs somewhere to live.
2. **Map-literal struct construction (`{:field val ...}`).** Every struct construction in
   `mapbuilder/tools.prn` uses this form (`{:tool tool :start-x x ...}` for `DragState`,
   `{:block-name "unset" :x x :y 0 :z y}` for `PlacedObject`), not the positional
   `(StructName val1 val2 ...)` form VS0's own `defstruct` currently emits construction
   support for. The target struct type has to be inferred from context (the enclosing
   `let` binding's or function's own declared type), not named explicitly in the literal
   itself — a real, separate type-inference question `defstruct`'s own positional-call
   design deliberately sidestepped.
3. **Namespaced variant construction (`EnumName/VariantName`).** `CanvasCommand/PlaceCmd`
   and `CanvasCommand/EraseCmd` qualify the variant with its own enum name at the
   construction call site; VS0's current `find_enum_variant()` looks up a variant by its
   bare name only, assuming global uniqueness across every registered enum in the file —
   real, honest, and already wrong the moment two enums share a variant name, which this
   exact file doesn't hit yet but a real qualified-lookup path should handle anyway.
4. **Real `Vec` collection operations** (`vec/new`, `vec/push!`, `vec/len`, `vec-pop!`).
   `vec`/`map` are marked resolved above in this same doc's own gap list, but that resolution
   is a design pass, not emitter support — VS0's actual emitter has no notion of `(Vec T)` as
   a real, constructible, indexable runtime type yet; every `(Vec ..)` return type seen so far
   this session only ever appeared nested inside a `Result`/`Option` that already stopped the
   emitter from needing to look inside it.
5. **Reference types (`&T` / `&mut T`).** `&Canvas`, `&mut DragState`, `&mut Terrain` appear
   throughout both files as parameter types — VS0 has no borrow/reference type at all yet
   (already flagged as a real, separate gap by `pentest/pcap.prn`'s own `&mut Capture`
   parameter earlier this session).
6. **Tuple types** (`(I32 I32)` as `pixel-to-cell`'s own return type, `(I32 I32 I32 I32)` as
   a rectangle parameter type elsewhere in this doc). No tuple representation exists in the
   emitter at all — would need its own real C struct-per-arity scheme, similar in spirit to
   `defstruct` but anonymous and arity-keyed rather than named.
7. **`set!` mutation syntax.** `(set! (get-field !drag :cur-x) x)` — VS0 has no assignment
   form to an existing binding or field at all yet (every real value so far in this compiler
   is bound once via `let`/a parameter and never reassigned in place).
8. **`F64` as a recognized primitive type.** Both `gfd.prn` (`spawn-panel`'s `x`/`y`/`z`
   parameters) and `world.prn`'s own `Terrain`/`set-height`/`get-height` need a real
   floating-point type beyond `NODE_NUMBER`'s own untyped `"double"` inference — the
   smallest, most mechanical item on this list (a straight `F64` → `"double"` addition
   alongside `I32`/`String` in `resolve_declared_type()` and the plain-parameter-type
   branch), genuinely no different in shape from work already shipped this session.

Real, honest note on sequencing: items are listed in roughly the order `parena build` actually
surfaces them against these two real files, not in order of implementation difficulty — #8
(`F64`) is the smallest, most mechanical of the eight and could reasonably be picked up
independently of the rest; #1-#3 are all real extensions of features already shipped this
session (`defenum`/`defstruct`); #4-#7 are each a genuinely new language feature, not an
extension of an existing one, and #5 (reference types) in particular has real implications for
the region analyzer (domain 2), not just the emitter (domain 3) — borrowing changes what "does
this escape its region" even means, a question this list doesn't attempt to answer.

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

## `math` / `mishri` — new packages, real TypeScript-target proving ground (2026-08-30)

Founder real-time: "using the MISHRI construct add apis for rewriting MISHRI in parena we want to
add all the building bloccks to the std lib as best we can and the MISHRI deps - check the
repository XCVBNM-OR for that manifest and then start working on the parena ts emitter using
MISHRI as proving ground we can start to replace the MISHRI js deps little by little using the
dependent stdlibs plannning pattern check the parena stdlib md file." Real manifest checked:
`XCVBNM-OR/MISHRI_CONSTRUCT.txt` (a downloaded copy of the real construct-bundle artifact
PAPERCRAFT/MISHRI's own CI already produces, see PAPERCRAFT's own `MODDING.md`/MISHRI's own
`.github/workflows/ci.yml`) — its own `package.json` section is the real dependency list this
section plans against.

**Real, honest reframing first, not glossed over**: `MISHRI_CONSTRUCT.txt`'s own real dependency
list is `mineflayer` + five mineflayer plugins (`mineflayer-auto-eat`/`-collectblock`/
`-pathfinder`/`-pvp`/`-tool`) + `minecraft-data`. None of these are real candidates for a
PARENA-native reimplementation — `mineflayer` alone is a full Minecraft network protocol
implementation (packet framing, chunk decoding, entity physics), and `minecraft-data` is a
versioned data blob (block/item/entity tables per Minecraft version), not logic. This is the
exact same real shape PAPERCRAFT's own `PARENA/stdlib/papercraft/*.prn` mods already settled:
PARENA replaces the DECISION logic layered on top of a host game library (SDL2 for PAPERCRAFT,
mineflayer for MISHRI), never the host library itself — matching `sdl2`'s own real Tier-0 "FFI
binding, not a language primitive, but built in because the founder called it that way" precedent
already set above. **The real, valuable "MISHRI deps to replace" are therefore MISHRI's own
`src/{humanness,movement,perception,social,behavior,skills}/` modules — pure decision/behavior
functions operating on host-provided game state, the identical shape `xp_award_mod.prn`/
`item_drop_mod.prn`/`phone_mod.prn` already proved for PAPERCRAFT — not the npm dependency list
itself.**

### Real, first proof: the new TypeScript emitter (`src/emit_ts.c`)

VS0's own emitter (`emit.c`, `emit_c()`) only ever targets C99 — every real stdlib package above
is `parena parse`-verified but not runnable outside a C host. MISHRI is a real TypeScript
codebase (S206-70's own real, full TS upgrade), so a real TypeScript target is the concrete,
honest way to make "replace the MISHRI js deps little by little" real rather than aspirational.

**Real, deliberately narrow v0 scope**, same "start narrow, grow via real found-and-fixed gaps"
discipline every package above documents for the C backend: a `defn` with zero or more plain
scalar (`I32`/`F64`/`Bool`/`String`) parameters — no `Arena`/region annotations at all (real,
deliberate: TypeScript is garbage-collected, region annotations are a genuine no-op for this
target, not an unsupported feature to fake) — and a body that is exactly ONE real expression:
number/symbol literals, the same arithmetic/comparison/logical binop set the C emitter recognizes
for this shape, `if` as a ternary, a call to another top-level `defn` in the same file, or a call
to `math/random` (the one real, recognized external primitive this v0 knows, lowered directly to
`Math.random()` — see `math/random.prn` below). No `let`, no blocks, no loops, no structs/enums,
matching exactly the real, already-proven, already-shipped shape every PAPERCRAFT mod uses.
Wired into the existing `parena build <file.prn> -o <output>` command by output extension (`.ts`
routes to `emit_ts`, anything else keeps the unchanged, default C path) — no new subcommand, zero
risk to any existing `.c`-targeting caller. Real, separate C file (`src/emit_ts.c`, its own
minimal string builder and name-mangler), not a refactor of the existing, hard-won,
`-Werror`-clean C emitter into a shared multi-target abstraction — that refactor is real,
separate, much bigger, much riskier work, not attempted here. `tests/test_emit_ts.c` (14
assertions, `bazel test //tests:test_emit_ts`) covers the real success shapes above plus two real,
honest failure cases (an `Arena`-typed parameter, a `let`-block body) — both correctly rejected,
not silently guessed at.

**Real proving-ground case, verified, not just written**: `stdlib/mishri/bezier_interp.prn` ports
MISHRI's own real `HumannessLayer.bezierInterp` (smooth, human-like head-turn interpolation) —
one real, nested arithmetic expression, no `let` needed (the original's four sequential `const`
bindings collapse into one expression tree). Compiled via `parena build ... -o
MISHRI/src/generated/bezier_interp.ts` (committed generated output, same convention every
PAPERCRAFT `*_mod.c` file already uses — not regenerated at MISHRI's own build time).
`HumannessLayer.ts`'s own hand-written `bezierInterp` method now just calls the generated
function. **Verified bit-for-bit identical**, not just "looks equivalent": both the original and
generated bodies run against the same mocked `Math.random()` value per trial, across six real
`(start, end, t)` cases × 20 trials each — zero mismatches. MISHRI's own existing
`bezierInterp()`-specific test assertions (already in `tests/humanness.test.ts`, unmodified) still
pass, along with all 84 of MISHRI's own test assertions total.

### `math` — new package, depends on `core` only

```clojure
(defn random [] : F64)     ; real FFI-shaped gap, no pure-PARENA PRNG exists yet
(defn floor  [(x : F64)] : F64)
(defn sqrt   [(x : F64)] : F64)
(defn log    [(x : F64)] : F64)
(defn cos    [(x : F64)] : F64)
;; `pi` -- a real, recognized external CONSTANT (bare symbol `math/pi`, not a call), no defn to
;; export -- PARENA's own `core` has no named-constant declaration form yet.
```

The TypeScript emitter recognizes each of these exact, qualified names (`MATH_PRIM_TABLE`,
`src/emit_ts.c`) and lowers them directly to the matching real `Math.*` call/constant; no
C-emitter mapping registered for any of them yet, a real, separate follow-up. Grew from an
original single-function `random.prn` (renamed `math.prn` once `floor`/`sqrt`/`log`/`cos`/`pi`
were added for `mishri/humanness`'s own real `gaussian-noise`/`random-int` below — one file per
package, matching `string.prn`/`io.prn`'s own established convention, not one file per function).
**Real, designed-not-built follow-up** (not attempted this pass): a `vec3` sub-package
(`math/vec3` — `new`/`add`/`subtract`/`scale`/`distance-to`/`normalize`, mirroring the real `vec3`
npm package MISHRI's own `MovementManager.ts`/`SkillManager.ts` already depend on for 3D position
math) — the one genuinely reusable, non-MISHRI-specific numeric package a real "MISHRI deps"
audit surfaces, matching `array`/`linalg`'s own real "small, composable numeric package" shape
already established above, not attempted in this pass since neither `bezier_interp.prn` nor
`humanness.prn` needed it.

### Real, third proof: the new Java emitter (`src/emit_java.c`, 2026-08-30)

Founder real-time, immediately after the TypeScript emitter and the (correctly declined) "Java
applet" pivot: "break out the java emitter for PARENA" — directly continuing the C-first-then-
JVM/TypeScript/WebAssembly multi-target roadmap `CLAUDE.md` already committed to. Same real,
deliberately narrow v0 scope as `emit_ts.c` (see that section above for the full real rationale,
not repeated here): scalar `I32`/`F64`/`Bool`/`String` params only, no `Arena`/region annotations,
one-expression body, same binop/`math/*`-primitive tables (`java.lang.Math` has identical real
method names to JS's own `Math` — `Math.random`/`floor`/`sqrt`/`log`/`cos`/`PI` — a real,
convenient overlap this emitter leans on rather than re-deriving). Own independent string builder
and name-mangler, same "each target module stays independent" discipline `emit_ts.h` already
documents — not shared code with `emit.c` or `emit_ts.c`.

**Two real, Java-specific differences from the TypeScript target**, both load-bearing, not
cosmetic: (1) `=` lowers to Java's own `==` (not TypeScript's `===` — Java has no triple-equals
token at all), correct real value-equality for this v0's own narrow primitive-only type set; (2)
Java has no top-level free functions, so every top-level `defn` from one compile invocation is
wrapped in one real `public final class <ClassName> { ... }`, where `<ClassName>` is derived by
the caller (`main.c`'s own `java_class_name_from_path()`) from the real output file's own
basename — a genuine `javac` hard requirement (the public top-level class must match the
filename), not a style choice. Wired into the same `parena build <file.prn> -o <output>` extension
dispatch (`.java` routes here, `.ts`/anything-else unaffected). `tests/test_emit_java.c` (23
assertions, `bazel test //tests:test_emit_java`) mirrors `test_emit_ts.c`'s own coverage plus the
two real Java-specific shapes above.

**Real proof, verified with an actual `javac`, not just written**: the exact same, unmodified
`stdlib/mishri/bezier_interp.prn` and `stdlib/mishri/humanness.prn` sources already proven for the
C and TypeScript targets were compiled a third time, to `BezierInterp.java`/`Humanness.java`, and
actually compiled with a real JDK (`javac 25.0.4`) to `.class` files with zero errors — then run
against a small smoke-test `Main.java` confirming sane real output (`chance(1.0)` → `true`,
`chance(0.0)` → `false`, `gaussianNoise(x, sigma=0.0)` → exactly `x`, matching the real, expected
zero-noise-when-sigma-is-zero identity). Real, honest: unlike the TypeScript target, this Java
output isn't wired into any live consumer yet (MISHRI is a TypeScript codebase, not Java) — this
is a real, deliberate "one PARENA source, three real compiled targets" compiler-capability proof,
not a live integration, matching the founder's own repeated "verified, not just written" bar.

### `base4` — new package, real port of `examples/engine.py.txt`'s own symbolic algebra

Founder uploaded `examples/engine.py.txt` directly to this repo (a real, complete "CUSTOM BASE-4 /
BINARY ALGEBRA LAB" — a 4-symbol alphabet `0`/`1`/`-`/`+` mapped to 2-bit values `00`/`01`/`10`/
`11`, explored as a base-4 number system, a symbolic algebra, and a state machine), then: "add to
parena stdlibs."

```clojure
(defn symbol-zero  [] : I32 0)   ;; the four named symbol codes -- a symbol IS its own I32 value
(defn symbol-one   [] : I32 1)
(defn symbol-minus [] : I32 2)
(defn symbol-plus  [] : I32 3)

(defn base4-xor [(a : I32) (b : I32)] : I32)       ;; real bit-xor reuse, no reimplementation
(defn base4-and [(a : I32) (b : I32)] : I32)       ;; real bit-and reuse
(defn base4-or  [(a : I32) (b : I32)] : I32)       ;; real bit-or reuse
(defn base4-add [(a : I32) (b : I32)] : I32)       ;; (mod (+ a b) 4)
(defn base4-subtract [(a : I32) (b : I32)] : I32)  ;; (mod (+ (- a b) 4) 4) -- see below

(defn base4-iterate [(start : I32) (op : (Fn [I32 I32] I32)) (steps : I32)] : I32)
(defn base4-cycle-length [(start : I32) (op : (Fn [I32 I32] I32))] : I32)
```

**Real, narrow v0 scope, deliberate**: ports the algebra core the Python original's own closing
message names as the actually interesting question ("what happens when we define different
operations over these four states") — NOT the base-4-number/binary-stream generation, frequency
counting, or substring pattern search, which are real, separate, string/Vec-heavy exploratory
display code, not stdlib-primitive shaped. `base4-xor`/`base4-and`/`base4-or` are direct reuse of
PARENA's own existing `bit-xor`/`bit-and`/`bit-or` primitives — no reimplementation needed, since
each symbol already IS its own real 2-bit `I32` value.

**Real, deliberate correctness fix over a naive transliteration**: `base4-subtract` uses
`(mod (+ (- a b) 4) 4)`, not the bare `(mod (- a b) 4)` a literal reading of the Python original's
own `(av - bv) % 4` might suggest — Python's `%` always returns a non-negative result for a
positive modulus, but PARENA's `mod` compiles to C's own `%`, which returns a result with the same
sign as the dividend. Adding 4 before the final mod guarantees a real, correct 0-3 result matching
the Python original's own real observable behavior, not just its literal source text — verified in
`tests/test_base4.c`, not just asserted in a comment.

`base4-iterate`/`base4-cycle-length` use VS0's real typed `Fn`-callback-parameter support
(`(Fn [I32 I32] I32)`) to take `base4-xor`/`base4-and`/etc. as real first-class arguments, matching
the Python original's own `operation` parameter design directly. `base4-cycle-length` surfaces a
real, provable property the Python original only ever discovers empirically (running 20 fixed
steps and scanning for the first repeat): since the state space is exactly `{0,1,2,3}`, every real
orbit under one of these operations must cycle within 4 steps (pigeonhole) — verified, not just
claimed: `AND`/`OR` give cycle length 1 for every start (both idempotent, `a&a=a`/`a|a=a`), `XOR`
gives 1 or 2, `ADD` gives 1, 2, or 4, all hand-traced and asserted in `tests/test_base4.c` (24
assertions, `make test-base4`, `-Werror` clean).

### `base4/vector` — new package, LO's real Phase 1 stdlib target

Founder real-time, 2026-08-30: "continue working on lo adding to the stdlib libs necessary to
make the language actually function... theoretically ffi into parena is acceptable if it
satisfies the design." `LO/NORTHSTAR.md`'s own phased plan names LO/`qi` as emitting real `.prn`
text that calls INTO existing PARENA stdlib rather than LO's compiler hand-generating base4 bit
logic inline — this package is that real target for `LO/GRAMMAR.md`'s §5.1 `Arith` operators and
§5.3 `EQ`/`DOT` applied to vectors (base4/algebra.prn only ever covered bare scalars).

```clojure
(defn dimlen [(v : &(Vec I32))] : I32)                                            ;; alias over vec/len
(defn vec-eq [(a : &(Vec I32)) (b : &(Vec I32))] : Bool)
(defn vec-xor [(a : &(Vec I32)) (b : &(Vec I32)) (dest : Arena @ Region)] : (Option (Vec I32)) @ Region)
(defn vec-and [(a : &(Vec I32)) (b : &(Vec I32)) (dest : Arena @ Region)] : (Option (Vec I32)) @ Region)
(defn vec-or  [(a : &(Vec I32)) (b : &(Vec I32)) (dest : Arena @ Region)] : (Option (Vec I32)) @ Region)
(defn vec-add [(a : &(Vec I32)) (b : &(Vec I32)) (dest : Arena @ Region)] : (Option (Vec I32)) @ Region)
(defn vec-subtract [(a : &(Vec I32)) (b : &(Vec I32)) (dest : Arena @ Region)] : (Option (Vec I32)) @ Region)
(defn dot [(a : &(Vec I32)) (b : &(Vec I32)) (dest : Arena @ Region)] : (Option I32) @ Region)
```

**Real, deliberate scope boundary**: covers elementwise vector ops, equality, and dot product only
— GRAMMAR.md §5.2's `STACK`/`MATMUL` (matrix construction/multiplication) and §5.4's PCRE-lite
pattern matcher are real, separately-sized follow-ups (`LO/BACKLOG` S208-03/S208-04), not attempted
here, same "start narrow" discipline as every other package in this file.

**Real design decision the source material never made**: `dot` needs a real multiply-then-
accumulate definition and `LoLanguageSpec.pdf` only ever names the operator (🎯), never defines
real multiplication over the 4-symbol state space. This package's own choice: base4-AND as
elementwise "multiply" (the one op of the five that behaves like a real product on the 2-bit
encoding), folded with base4-ADD (mod-4 accumulate) as the "sum" — flagged as this file's own
call, not the source's, in `vector.prn`'s own header comment.

**Real, genuine compiler gap found while building this, same class linalg.prn's own header
already documents (not a new bug, a new confirmed instance)**: `dot`'s loop accumulator seeded
from the integer literal `0` gets C-typed `double` by VS0's emitter, so its `Some acc` boxing goes
through `double_box`, not an I32 box — the real generated C silently violates `dot`'s own declared
`(Option I32)` return type. Confirmed directly in `tests/test_base4_vector.c`: a hand-traced dot
product of `2` reads back as `0` via the only correct-per-signature `int*` cast, and only recovers
`2.0` via the wrong `double*` cast. **Not fixed here** — same real, cross-cutting loop-variable/
number-literal type-inference change linalg.prn's own header already named as out of scope for a
single-file pass. `vec-xor`/`vec-and`/`vec-or`/`vec-add`/`vec-subtract`/`vec-eq`/`dimlen` do NOT
hit this (none box a loop-seeded scalar through `Some`/`Ok` directly) and are real, verified
end-to-end: 24 assertions, `make test-base4-vector`, `-Werror` clean.

### `base4/matrix` — new package, S208-06, LO's real STACK/MATMUL stdlib target

`LO/GRAMMAR.md`'s §5.2 `STACK`/`MATMUL`/matrix-`DIMLEN` over LO's real 2-row matrix examples.

```clojure
(defstruct Base4Matrix (data : (Vec I32) @ Region) (rows : I32) (cols : I32))  ;; flat, row-major

(defn rows [(m : &Base4Matrix)] : I32)
(defn cols [(m : &Base4Matrix)] : I32)
(defn dims-eq [(a : &Base4Matrix) (b : &Base4Matrix)] : Bool)   ;; a.cols == b.rows
(defn stack2 [(row0 : &(Vec I32)) (row1 : &(Vec I32)) (dest : Arena @ Region)] : (Option Base4Matrix) @ Region)
(defn matrix-eq [(a : &Base4Matrix) (b : &Base4Matrix)] : Bool)
(defn matmul [(a : &Base4Matrix) (b : &Base4Matrix) (dest : Arena @ Region)] : Base4Matrix @ Region)
```

**Real shape decision**: flat, row-major `(Vec I32)` + `rows`/`cols`, matching `array.prn`'s own
already-proven `NDArray` convention — not a `Vec`-of-`Vec` (real, separate, unproven generic
capability, sidestepped rather than assumed to work).

**Real, deliberate scope boundary**: `stack2` builds exactly a 2-row matrix — every real matrix
example in `LoLanguageSpec.pdf` is 2 rows. Real N-row stacking needs variadic `defn` params or the
`Vec`-of-`Vec` question above; a real, separate follow-up, not built here.

**Two real, genuine compiler gaps found and handled while building this, both flagged in
`matrix.prn`'s own doc comments, neither silently patched over**:
1. Two sibling top-level `loop` forms in the same function reusing the same binding name (`i`)
   emit as colliding C `double i = 0;` declarations at the same brace level — `gcc` correctly
   rejects it as a redefinition. Worked around with distinct names (`i0`/`i1`); a real, live
   VS0 loop-emission boundary (each `loop` needs its own C block scope or truly fresh temp names),
   not a logic error in this file.
2. `matmul`'s own per-cell accumulator hits the exact same class of bug `base4/vector.prn`'s own
   `dot` already confirmed: seeded from an integer literal, C-typed `double`, boxed via
   `vec_box_f64` into a `Vec` declared `(Vec I32)`. Confirmed directly in
   `tests/test_base4_matrix.c` (hand-traced product `[[2,3],[0,1]]`, each cell reads back wrong
   via the only correct-per-signature `int*` cast, correct only via the wrong `double*` cast).
   **Not fixed here** — same cross-cutting fix `linalg.prn`/`base4/vector.prn` already scoped out
   of a single-file pass. `stack2`/`matrix-eq`/`dims-eq`/`rows`/`cols` don't hit this and are real,
   verified: `make test-base4-matrix`, `-Werror` clean.

### `base4/pattern` — new package, S208-07, LO's real PCRE-lite pattern matcher

`LO/GRAMMAR.md`'s §5.4 wildcard/quantifier/anchor pattern matching over base4 vectors.

```clojure
(defenum Quant (One) (ZeroOrMore) (OneOrMore) (Optional))
(defstruct PatternElem (is-wild : Bool) (state : I32) (quant : Quant))

(defn elem-lit  [(state : I32) (quant : Quant)] : PatternElem)
(defn elem-wild [(quant : Quant)] : PatternElem)
(defn is-match [(elems : &(Vec PatternElem)) (anchored-start : Bool) (anchored-end : Bool)
                (target : &(Vec I32)) (dest : Arena @ Region)] : Bool @ Region)
```

Real backtracking-matcher architecture DIRECTLY reused from `regex/pcre.prn`'s own already-proven
design (candidate end-positions as a real `(Vec I32)`, most-preferred/longest first, explicit
loop/recur, no closures), adapted to base4 states instead of characters.

**Real, deliberate scope boundary**: covers exactly `LoLanguageSpec.pdf`'s own one fully-worked
pattern example (start/end anchors, literal, wildcard, and all four quantifiers). `ALT` and
`GROUP` are real, separately-sized follow-ups (`S208-09`) — `ALT` needs multi-branch backtracking
across whole sub-patterns, `GROUP` needs a real capture-boundary concept this file's flat
element-sequence design doesn't have.

**Real, deliberate design choice**: no `Atom` defenum (`Lit I32 | Wild`) — plain `PatternElem`
fields instead. Found live that VS0's emitter doesn't yet correctly type-infer an I32 bound via
`match`-destructuring a boxed defenum-variant payload (emitted as `void *`, pointer-compared
against an int — confirmed `-Werror`-failing). Same for `match-seq`'s own return: a raw `I32`
sentinel (`-1` for no match) instead of `(Option I32)`, for the identical reason. Both are new,
confirmed instances of the same class of gap `base4/vector.prn`/`base4/matrix.prn` already
documented, not new kinds of bugs.

**Real, second, separate gap found and fixed while building this**: building test vectors from
*within* `.prn` source (not an external C harness) via plain `(vec/push! &mut v 1)` on a declared
`(Vec I32)` silently boxes the literal via `vec_box_f64`, not `vec_box_i32` — every numeric
literal in VS0 is unconditionally typed `double`, and this is the first file in this stdlib to
build `Vec I32` test data from PARENA source itself rather than an external C driver. Fixed with
a new `push-i32!` — same real `#target`/`vec-i32-at` fix shape (a function whose own declared
signature, not the numeric-literal codegen path, is what VS0 resolves types from).

**Real, live bug found and fixed via this file's own `self-test`, not just asserted correct**: the
initial `OneOrMore` implementation had a genuine off-by-one (the mandatory first match's own
position offset was dropped when computing later candidate end-positions) — caught because
`self-test`'s own hand-traced expected result didn't match, not glossed over.

**Real "write the main in pure PARENA" milestone** (founder real-time, since `parena-c` has no
real `(defn main ...)` → C `int main` emission convention yet — confirmed directly against
`src/emit.c`, per `selfhost/main.prn`'s own already-documented header comment, so a genuinely
standalone PARENA executable isn't possible today): `self-test` is the real test ORCHESTRATION,
written entirely in `.prn` source — `tests/test_base4_pattern.c` is reduced to the thinnest
possible external shim, calling `self-test` once and checking the one returned `Bool`. `make
test-base4-pattern` green, `-Werror` clean.

**S208-09 follow-up, `ALT`/`GROUP` added**: `elem-group` — a real, zero-width GROUP (🗜) boundary
marker (matches without consuming a target state, checked before quantifier dispatch in both
`match-one` and `match-elem` since a group needs different arithmetic than a normal `One`
element). Real, honest scope: no capture-VALUE extraction yet — nothing downstream needs a
captured substring, so this only guarantees a group boundary doesn't break matching around it.
`is-match-alt` — real, but deliberately narrower than GRAMMAR.md's own flat single-Vec-with-
embedded-ALT-token shape: binary alternation over two SEPARATE pattern `Vec`s (try `elems-a`, then
`elems-b`), matching `regex/pcre.prn`'s own `match-alt` leftmost-first order. N-way (3+)
alternation and a true flat-embedded-ALT-token form are named, real follow-ups, not built here.

### `papercraft/note-version-mod` — new package, iCloud-style version-management decisions

Founder real-time: "add parena primitives for managing versions of notes have it plug into
papercraft - like the backend of icloud however it would manage different versions of a
document." Real, narrow v0 filling the gap `notes_mod.prn`'s own header comment already named
("a note's own real, variable-length, user-authored TEXT has no real storage/persistence story
in VS0 yet") — this does NOT store note text or version blobs (a real, separate host/persistence
concern, still open); it owns the real decisions an iCloud-style versioning backend needs.

```clojure
(defn max-versions-per-note [] : I32 50)
(defn coalesce-window-seconds [] : I32 30)
(defn on-papercraft-should-coalesce-edit [(seconds-since-last-edit : I32)] : Bool)
(defn on-papercraft-version-to-evict [(current-version-count : I32)] : I32)   ;; -1 = no eviction needed
(defn on-papercraft-has-version-conflict [(edit-based-on-version : I32) (current-version : I32)] : Bool)
```

Real, iCloud-observed-behavior default: edits within 30s of the current version coalesce (fold
in place) rather than forking a new version — matches iCloud's own real "typing doesn't create a
version per keystroke" behavior. Eviction is oldest-first once the bounded 50-version cap is hit
(matching `note-slot-count`'s own bounded-not-unbounded convention). Conflict detection is the
same real optimistic-concurrency check every versioned-document backend uses (iCloud, Google
Docs, git's fast-forward check): an edit conflicts exactly when its base version is no longer
current, not merely because two edits happened close in time (that's the non-conflicting
coalescing case above). 8 real, hand-traced assertions, `make test-papercraft-note-version`
green, `-Werror` clean.

### `datetime` — new package, Go-style reference-time layout formatting

Founder real-time: "add stdlibs for date time use the same magic string as golang."

```clojure
(defn is-leap-year? [(year : I32)] : Bool)
(defn days-in-month [(year : I32) (month : I32)] : I32)      ;; month 1-12
(defn day-of-year [(year : I32) (month : I32) (day : I32)] : I32)
(defstruct DateParts (year month day hour minute second : I32))
(defn unix-parts [(epoch-seconds : I32)] : DateParts)
(defn format-go-layout [(epoch-seconds : I32) (layout : String @ Region) (dest : Arena @ Region)] : String @ Region)
```

Real, honest, narrower than Go's own `time` package: only `2006`/`01`/`02`/`15`/`04`/`05` are
supported (enough for ISO-8601-shaped timestamps) — `06`/`03`/single-digit forms/`PM`/`pm`,
month/weekday names, timezone offsets, and fractional seconds are real, separate follow-ups.

**Real, genuine compiler gap found and worked around**: `src/emit.c` has several fixed `char
buf[1024]` buffers involved in emitting `#target`/`inline-c` bodies — a first-draft, all-in-one
inline-c version of `format-go-layout` (~1.3KB) silently truncated mid-token at offset ~1033, no
error. Worked around architecturally, not just patched: `unix-parts`'s calendar breakdown stays a
short `#target` (via `gmtime` + a compound-literal pointer, since `alloc` only supports `String`
and a GNU statement-expression `({...})` is rejected under this project's own `-pedantic` build),
and the actual layout-substitution scan is written in pure PARENA control flow instead of one
giant C block.

**Real dual-target result, checked directly**: `is-leap-year?`/`days-in-month` now reach `burrow`
too — but only after fixing two real, genuine `BURROW` gaps found live (not pre-existing known
issues): bare `true`/`false` literals had no handling in `emit_go.go` at all, and a trailing `?`/
`!` in a defn name produced an illegal Go identifier (fixed by mirroring `src/emit.c`'s own
`?`/`!` -> `_` mangling). Both fixed with real regression tests, verified end-to-end (built and
ran the real generated Go). `day-of-year` does NOT reach burrow (uses `loop`, a real, separate,
already-known boundary). `format-go-layout` is C-only regardless — no String-building in burrow
yet, same boundary `k8s.prn` already hit. `make test-datetime` green, `-Werror` clean.

### `http/router` — new package, LO FRAMEWORK_NORTHSTAR.md's Phase B proof point

Founder real-time: "ok well write the deps in parena" — build the "batteries included" LO
framework's real dependencies in PARENA now, ahead of LO's own Phase 2 (`qi`) landing.

```clojure
(defn route-matches? [(pattern : String @ Region) (path : String @ Region) (dest : Arena @ Region)] : Bool @ Region)
(defn extract-param [(pattern : String @ Region) (path : String @ Region) (param-name : String @ Region) (dest : Arena @ Region)] : String @ Region)
```

Real, minimal Sinatra/Express-style router: literal `/` segments plus a single `:name` capture
convention. Deliberately does NOT reuse `base4/pattern.prn`'s own backtracking matcher — that
operates on base4 state vectors, not characters, and `FRAMEWORK_NORTHSTAR.md` named the byte-to-
base4 encoding question as real and undecided; this file answers it by not waiting on it.

**Real, genuine, previously-latent bug found and fixed while building this, confirmed via a real
segfault**: `vec-string-at`'s cast (`*(char **)vec_get(v, idx)`, a double dereference) is wrong —
a `String` element is already a `char *`; `string/split` pushes it directly with no extra boxing
step (unlike `vec_box_i32`/`vec_box_f64` for scalars, which genuinely need a fresh heap slot).
The correct read-back is a single cast. The exact same bug existed in `regex/pcre.prn`'s own copy
of `vec-string-at` — never caught there because its only caller (`join-strings`) has no real
caller anywhere in this stdlib. Both fixed. `make test-http-router` green, `-Werror` clean.

### `editor/document` — new package, real document management (founder: "asap")

Ties two already-real, already-verified pieces together: `editor/buffer.prn`'s own text/cursor
Buffer, and `papercraft/note_version_mod.prn`'s own coalesce/version decision logic (S215-02).

```clojure
(defstruct Document (buf : Buffer) (current-version : I32) (last-edit-epoch : I32))
(defn new-document [(now-epoch : I32) (dest : Arena @ Region)] : Document @ Region)
(defn apply-edit [(doc : Document) (new-text : String @ Region) (now-epoch : I32) (dest : Arena @ Region)] : Document @ Region)
(defn document-text [(doc : &Document)] : String @ Region)
(defn document-version [(doc : &Document)] : I32)
(defn document-cursor [(doc : &Document)] : I32)
```

Real behavior, verified: edits within `note_version_mod.prn`'s own 30s coalesce window overwrite
the current version in place; a gap past that window forks a real new version (`current-version`
increments). Real, honest scope: single-document only (a multi-document registry is a real,
separate follow-up); version HISTORY bytes aren't stored here — `note_version_mod.prn`'s own
scope is decision logic only, a real host still persists each version elsewhere (per
`JEWEL/docs/NORTHSTAR_SARENA_NOTEBOOK.md`'s own word-processor pivot section, IDUNA). `make
test-editor-document` green, `-Werror` clean.

### `editor/registry` — new package, S217-03 multi-document registry

Real open/switch/list on top of `editor/document.prn`.

```clojure
(defstruct Registry (docs : (Vec Document) @ Region) (current-index : I32))
(defn new-registry [(dest : Arena @ Region)] : Registry @ Region)
(defn open-document [(reg : Registry) (now-epoch : I32) (dest : Arena @ Region)] : Registry @ Region)
(defn switch-document [(reg : Registry) (index : I32)] : Registry @ Region)
(defn current-document [(reg : &Registry)] : Document)
(defn document-count [(reg : &Registry)] : I32)
```

Real, functional-update shape (matches `document.prn`'s own `apply-edit`): each operation returns
a NEW `Registry` rather than mutating in place. `switch-document` leaves the registry unchanged
on an out-of-range index — verified. Same real `vec-T-at` `#target` escape hatch this stdlib's
elem-type-hint gap already needs elsewhere, this time for a struct (`Document`) element. Closing
a document, renaming, and real persistence are named, separate follow-ups. `make
test-editor-registry` green, `-Werror` clean.

### `mishri` — new package tree, real dependency order (design only past `bezier-interp`)

Topological by `import`, same rule as the main re-sort above. Real status column, honest about
what's actually built vs. designed:

1. **`mishri/bezier-interp`** — depends on `math` only. **Real, built, TS-emitter-verified** (see
   above).
2. **`mishri/humanness`** — depends on `math`, `mishri/bezier-interp`. **Real, partially built**
   (2026-08-30, founder real-time: "continue rewriting MISHRI using parena using parena mods"):
   `chance`/`random-int`/`gaussian-noise` (MISHRI's own real `chance`/`randInt`/`addNoise`) are
   real, TS-emitter-compiled, and live in MISHRI's own production TypeScript source — the same
   real verification bar `bezier-interp` set (bit-for-bit identical against the original across
   many cases, `Math.random()` mocked deterministically, MISHRI's own full test suite still
   green). Grew `math`'s own real primitive table (`floor`/`sqrt`/`log`/`cos`/`pi`) to support
   `gaussian-noise`'s own real Box-Muller body — its original `u1`/`u2` `const` bindings each
   appear in exactly one place in the rest of the body, so they collapse into two inline
   `(math/random)` calls with zero `let` needed, same real technique `bezier-interp`'s own
   collapsed `offset`/`mid`/`u` bindings already used. **Still real, design-only, and still
   genuinely blocked**: `delay`/`throttleAPM` (`Promise`/`setTimeout`-based async control flow —
   PARENA/VS0 has no async primitive of any kind yet, closer to `thread`/`otp/gen-server`'s own
   real FFI-to-OS-primitives shape above than to anything built so far) and `maybeTypo` (a real
   `Map`-shaped QWERTY-adjacency lookup — this v0 emitter has no struct/map/String-indexing
   support at all yet). Neither blocker is closer to resolved than before this pass — the real
   progress here is everything that DIDN'T need them.
3. **`mishri/movement`** — depends on `math/vec3` (design only, see above), `mishri/humanness`.
   Design only: `MovementManager.ts`'s own real pathfinding-offset/manual-walk decision logic.
   Real blocker: genuinely needs `mishri/humanness`'s own `delay`/async support first.
4. **`mishri/behavior`** — depends on `mishri/humanness`. Design only:
   `BehaviorOrchestrator.ts`'s own real utility-AI scoring (`_scoreWander`/`_scoreMine`/etc.) —
   the single most PARENA-native-shaped piece of MISHRI's entire codebase (pure scalar scoring
   functions over host-provided game state, the *exact* shape this whole emitter was built for)
   once the async blocker above is resolved.
5. **`mishri/skills`**, **`mishri/social`**, **`mishri/perception`** — depend on
   `mishri/humanness`, real host game-state types (`Bot`/`Entity`/`Block` — TypeScript-side types
   this v0 emitter has no concept of representing or accepting as parameters at all yet, a real,
   separate, further-out gap past the scalar-only boundary this pass drew). Design only, real,
   honestly the furthest-out tier — none of these are close to buildable against this v0 emitter's
   own current, real, narrow scope.

**Real, current status, not overclaimed**: four real functions (`bezier-interp`, `chance`,
`random-int`, `gaussian-noise`) are built, TS-emitter-compiled, and live in MISHRI's own
production TypeScript source. Everything else in this section is real, dependency-ordered
design — the honest next real increments, not built yet, same "real status" discipline the main
package list's own status notes already use throughout
this document.

## Related

- `NORTHSTAR.md` — "Standard library" section names this same gap; this doc is that gap's actual
  design pass. "Core idioms" section is the source of all five grounded function calls above.

### `mag/gematria` — new package, PARENA playground port of the "mag book"

Founder real-time: "can we turn the mag book into parena playground?" Real port of
`QUEENSALLYONLINEBOOKOFMAGIFICATIONANDUNICOR`'s squish/gematria pipeline, re-derived from the
already-verified Go reference (`gpt2-alpine-c/pkg/towerprint`, itself pinned against the original
Python/notebook output — SECTION 147/S147-01), not re-derived from Python blind.

```clojure
(defn squish [(s : String @ Region) (dest : Arena @ Region)] : String @ Region)
(defn az-digit [(code : I32) (grp-len : I32)] : I32)
(defn za-digit [(code : I32) (grp-len : I32)] : I32)
(defn fingerprint-az [(s : String @ Region) (grp-len : I32)] : I32)
(defn fingerprint-za [(s : String @ Region) (grp-len : I32)] : I32)
```

Real, honest, narrower than the Go port: `fingerprint-az`/`-za` accumulate into a plain `I32`,
not Go's `math/big.Int` — overflows for long words, correct and verified for short-to-medium
ones. `squish` is a deliberate `#target` implementation (fixed-size output buffer, single write
index) rather than built via `Vec`, since squish only ever shrinks a string. Real, found-live
compiler gap, same class already documented across `base4/*.prn`: a plain
`(let [n (string/length s)] ...)` binds `n` as `void *`, not `I32` — worked around by calling
`(string/length s)` inline at each use. `self-test` pinned against `towerprint_test.go`'s own
real "SALLY" vector (AZ dec `20330`, ZA dec `71661`) — verified exact match. `make
test-mag-gematria` green, `-Werror` clean. Tower/row-chunking (the visual "magic tower") not
ported — a real, separate follow-up.

### Color emoji in the editor (`editor/render.prn`) — real investigation, real prerequisite named (2026-09-03)

Founder, kanban cruise queue: "emojis need to work in pitviper and parena editor what do we need
to build a custom emoji font or use image files or something?" Real, checked answer for this
half of the ask (PITVIPER's own half is separately real, shipped, and test-verified — see
`PITVIPER/internal/font/emoji.go`'s own updated header comment): **no custom font or image files
needed here either.** `sdl2.prn`'s own `open-font` is already fully generic (any real TTF/OTF
path, any point size) — loading Noto Color Emoji (`/usr/share/fonts/truetype/noto/
NotoColorEmoji.ttf`, confirmed installed) as a second real `Font` handle needs zero new FFI
surface, the same real capability PITVIPER's own Go code already proved works (SDL2_ttf 2.20+
renders a color-glyph font's embedded color data directly).

**Real, honest, separate prerequisite found, not glossed over**: `editor/render.prn`'s own
`render-tokens` renders one whole highlighted token span at a time via a single `render-text`
call against one `Font`. Mixing in a second, emoji-specific font requires detecting which
*characters within a span* are emoji-range codepoints and splitting the span into sub-runs
rendered through the right font each — but `stdlib/string.prn`'s own `char-at` is explicitly,
by design, byte-indexed (its own doc comment: "returns a character as its raw byte value (I32)"),
not UTF-8-codepoint-aware. A real emoji character is multi-byte UTF-8; there is no real function
anywhere in this stdlib today that decodes a UTF-8 byte sequence into its actual Unicode
codepoint, confirmed by grep — `string.prn` has no `codepoint`/`utf8`/`rune`-named function at
all. This is a real, separate, more fundamental gap than the font-loading side, and needs its own
scoping pass (a real UTF-8 decoder, likely a new `string/utf8-decode-at` or similar, following
the same real "port it faithfully, verify against known test vectors" discipline every prior
stdlib addition here already uses) before `editor/render.prn` can honestly support mixed
emoji/text spans. Not attempted this pass — named here so the real, concrete unblock (reuse
PITVIPER's exact proven font-loading design, once codepoint iteration exists) isn't lost.

## bstree — a real, working ordered index (2026-09-03)

Real answer to the kanban priority-queue card "9933: INDEXING primitives built into PARENA to
power IDUNA OG unified search - btries etc". `stdlib/bstree.prn` is a real, compiling, live-
tested (`make test-bstree`) String-keyed, I32-valued binary search tree — insert (real
insert-or-update, not insert-only), get (`Option I32`), `contains?`.

Two real, decisive design constraints checked directly before writing this file, not assumed:
(1) VS0's emitter has no generic type parameters — `vec.prn`'s own `(Vec T)` and `map.prn`'s own
`(Map K V)` both fail to compile today ("unsupported return type symbol"), confirmed live — so
`bstree.prn` commits to concrete `String`/`I32` types, the same real workaround `json.prn`'s own
`JObject` already established for the identical reason. (2) Nodes live in one flat
`(Vec BSTNode)` with `left`/`right` as integer INDICES, not raw pointers — matching `map.prn`'s
own "flat, array-backed, no pointer-chasing" design, and sidestepping any open question about
whether VS0 supports self-referential struct types at all (never needed to find out).

Real, honest scope: this is a plain, unbalanced BST — real O(log n) average case, real O(n)
worst case on already-sorted insertion order, no rotation/rebalancing. A genuine multi-way,
disk-oriented B-tree (the literal "btries" the card names) is real, larger, separate follow-up
work, not built here. Wiring this into IDUNA's own unified-log search
(`internal/http/handlers/logs.go`, a real, current linear `Scan`) is ALSO separate, unattempted
work — IDUNA is a Go host, so that integration needs `BURROW`'s native Go emission target, the
same real reason already established for IDUNA_PRO's own extensibility plan.

## idunapro/cli-mod — a real, working v0 proof-of-concept CLI decision layer (2026-09-03)

Real first slice of kanban cruise-queue card "9988: emily for business CLI written in GO with
BURROW," picked up specifically because this session's own newly-shipped `match`/`Result` support
in BURROW's Go emission target (see `BURROW/CHANGELOG.md`'s 2026-09-03 (3) entry) made a real,
small, end-to-end proof worth building rather than continuing to plan in the abstract.

`stdlib/idunapro/cli_mod.prn` — `interpret-health-response` takes a real HTTP status code and a
real body-ok flag (the calling Go host does the actual HTTP GET and JSON parse — VS0's Go target
has no real JSON/String story yet, so that correctly stays host-owned) and returns
`(Result String String)`: `Ok` for a real 200-and-ok response, `Err` with a distinct message for
a bad status code vs. a 200-with-not-ok body. `exit-code-for-health` wraps it with a real
`match`, consuming the `Result` (not just constructing one) to decide the CLI's own real exit
code — proving both directions of this session's match/Result port in one real file.

Compiled via `burrow build ... -o *.go` into `IDUNA_PRO/internal/burrowgen/idunapro_cli_gen.go`
(same package-naming convention DUNG's own `internal/burrowgen` already established), called
directly from a new, real Go host binary `IDUNA_PRO/cmd/idunapro/main.go` (`idunapro health
<base-url>`) — no cgo/FFI boundary, a real Go import, matching DUNG's own precedent. Live-verified
end to end against IDUNA's real, currently-running `:8080` instance (`idunapro health
http://localhost:8080` → "IDUNA_PRO instance is healthy", exit 0) and against a real unreachable
host (connection-refused message, exit 1) — both the success and failure paths of the compiled
PARENA decision logic driving a real process exit code.

Real, honest scope: one subcommand, not a CLI framework. `defenum`/`loop`/`Vec`/struct
construction all remain unstarted in BURROW's Go target — a fuller CLI (auth, kanban, apples,
subscriptions) needs `loop` for iteration at minimum before more real subcommands are worth
adding. This is the narrowest real slice proving the whole pipeline (PARENA decision logic →
BURROW Go emission → real Go host → real network call → real exit code) actually works, not a
claim that "the CLI" itself is finished.

Two real, found VS0 emitter quirks worked around, not fixed, and named in the file's own header
comment: a `let` nested inside a non-first `cond` clause body reports a real, honest "can't be
used directly in expression position" error even though the same `let`-in-an-if-branch shape
compiles fine elsewhere; and `(get-field (vec/get &v i) :field)` needs the `vec/get` result
bound via `let` first, or the emitted C dereferences an uncast `void *`.
