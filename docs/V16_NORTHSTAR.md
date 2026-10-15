# NORTHSTAR — V16 (a native-PARENA JavaScript engine)

**Status (2026-10-15): a working, tested engine exists and runs as WebAssembly.** Phases 1-2 (the
generics decision, a JS lexer, an expression parser) shipped 2026-09-03/04; Phases 3-5 (interpreter,
memory strategy, stdlib slice) were taken as ONE narrow vertical spike on 2026-10-15, founder
real-time: *"spike out the v16 js execution engine it must run in WASM totally parena powered all
arenas"* -> `stdlib/v16/engine.prn`, `examples/v16_wasm/`, `tests/v16/`. See "The 2026-10-15 spike"
below for what it is, what it proves and, in particular, what it deliberately is not. Everything
below that section is the original scoping text, kept because the size comparison and the
out-of-scope list are still true.

**Founder real-time (2026-09-04): "continue that work to develop a full webview alternative
(iterate)" -> corrected "i mean webkit alternative".** Real, honest scope note: this reframes
V16's own eventual real destination (a full WebKit-class engine — HTML/CSS/layout/rendering, not
just a JS runtime) but does NOT change the real, incremental phased plan below. A full WebKit
alternative is a strict superset of this document's scope; the JS engine is one component of it.

## The 2026-10-15 spike: a JS engine in PARENA that runs in WebAssembly, all arenas

**What exists** (all of it checked by `make test-v16-engine` natively and `make test-v16-wasm`
against the compiled module — 19 corpus programs + limit checks; ASan/UBSan clean; ~3,000 mutated
programs fuzzed with zero crashes/hangs):

- `stdlib/v16/engine.prn` (~3,500 lines of PARENA, one module): a pull lexer, a single-pass
  Pratt-style compiler straight to bytecode (no AST), and a stack VM. Language: var/let/const with
  real block scoping, functions + arrows + **closures** (shared captured variables via Lua-style
  open/closed upvalue cells), if/while/do-while/for/for-of/for-in/switch/break/continue,
  try/catch/throw with real unwinding (including across callbacks), arrays, objects, strings,
  `console.log` with node-style array/object printing, and a builtin slice (`Math`, `JSON.stringify`,
  `Object.keys`, `parseInt`, `Error` family, array and string methods incl. callback methods
  `forEach/map/filter/reduce/some/every/find/sort`).
- **Totally PARENA, all arenas.** No malloc, no GC, no free anywhere: every token, constant,
  value-stack slot, string, array, object, closure and the console output buffer is bump-allocated
  from the single `Arena` the host passes in. JS values are `(tag, payload)` pairs in two parallel
  scalar `Vec`s, so the stack never allocates per push. A run is one region; the host drops the
  arena when the script ends. The only C in the engine path is one-liners behind `#target`
  (`snprintf`/`strtod` number formatting, `floor`/`pow`/`sin`..., a couple of buffer fills) —
  the same escape hatch every stdlib file uses — plus the 50-line host (`examples/v16_wasm/v16_host.c`)
  that owns the input buffer and the arena.
- **Runs in WebAssembly.** `make v16-wasm` -> `build/v16.wasm`, ~130 KB, **zero imports** (no WASI
  shim needed), five exports (`v16_input`, `v16_input_capacity`, `v16_run`, `v16_output`,
  `v16_arena_used`). Path: PARENA -> C (the normal emitter) -> any wasm32 C compiler
  (`python3 -m ziglang cc` by default). Verified under wasmtime: every corpus program, in a fresh
  instance and replayed in one shared instance.
- **Hard limits, not hangs:** an instruction budget, an **arena byte budget** (a RangeError
  *before* the pool runs dry), a 1024-frame call stack, 64 nested try blocks — a hostile script
  gets an ordinary `Uncaught ...` line.

**The NORTHSTAR "GC question" (Phase 4), as answered by the spike:** region-per-run, no collector.
A script cannot free memory mid-run; a long-lived embedding needs either short scripts or a
per-call/per-request region discipline. Escape analysis for closures is trivial here because
captured variables live in arena cells that outlive their frame by construction. What this does
*not* give you is bounded memory for a long-running loop that churns strings — it gets stopped by
the budget. Real compaction/reclamation (a per-call frame arena with copy-out, or a real tracing GC
confined to the engine's heap) is the honest next step if a consumer needs long-lived scripts.

**Not built (named, not hidden):** `this`/prototypes/classes/user constructors, `finally`,
generators, async/Promise, regex, template literals, destructuring/spread, getters/setters,
`JSON.parse`, `Date`/`Math.random` (no host clock/entropy is wired — the module has zero imports),
UTF-16/Unicode semantics, per-iteration `let` binding in `for` loops, function hoisting,
labelled statements, `with`, tail calls, a JIT. It is **not** spec-conformant (not run against
test262), numbers print via `%.17g`-roundtrip rather than the ECMAScript algorithm (so a few edge
cases such as very large/small exponents may differ), and the dispatcher is a plain comparison
chain — fib(27) takes ~0.35 s natively at `-O2` on this box (informal, one run, not a benchmark
harness; do not quote it).

**What it cost the compiler (PARENA itself gained):** (1) a statement-level lowering for `cond`/
`if` whose clause bodies are `do`/`let`/`loop`/`when`/`set!`/`unit` — the long-standing "a `cond`
clause body can't be a `let`/`do`" VS0 restriction is gone (plain-expression `cond`s keep their exact
ternary lowering); (2) `vec/truncate!`; (3) a loop that ends in `unit` no longer emits
`return __loop_result;` from a void function; (4) a **freestanding runtime mode**
(`PARENA_FREESTANDING`, automatic under `__wasm__`): OS glue is compiled out, `Vec` slots are 8
bytes everywhere (the old layout silently assumed 64-bit pointers), arena exhaustion traps instead
of calling `exit`.

**Next, in order of value:** (1) `this`/prototype objects + user `new` (needed for most real
libraries); (2) per-call frame regions with copy-out, to make long scripts safe; (3) `Date`/
`Math.random` as an explicit, optional host import; (4) a real number-to-string (shortest
round-trip, ECMAScript formatting) in PARENA instead of `snprintf`; (5) template literals,
destructuring, spread; (6) DOM is still out of scope — see below.

## What this is

A JavaScript engine — lexer, parser, bytecode/AST interpreter (a real JIT is explicitly out of
scope for any v0/v1), and enough of the JS standard library to run real, unmodified JS programs
— implemented natively in PARENA itself, compiled through VS0/BURROW like any other PARENA
program. Founder-named "V16" as a joke on V8 ("double the V8"), first said 2026-08-20; the name
is real, the scope was said explicitly in the same breath: **"table stakes"** — not a nice-to-
have feature, a baseline capability the founder considers required, most directly connected to
GoblinFoxDragon/PAPERCRAFT's own real mod-surface ambitions (Roblox's own real scripting layer
is Lua, but "whatever the browser is doing to make [a] file viewable" and general JS-ecosystem
compatibility were both named directly by the founder as reference points) and to DUNG's own
real "V16 renderer" framing (2026-08-30: "we may need to start building the v16 renderer").

## Why this is its own NORTHSTAR, not a STDLIB.md section

Every other real PARENA stdlib addition in this monorepo (`bstree`, `explosion_mod`, the k8s/
helm primitives, the regex family) is a bounded, single-sitting-to-few-sessions piece of work.
This is not that. A real, honest size comparison: this is the same order of magnitude as
building V8 or a Node.js-class runtime from scratch — one of, if not the, largest single asks
anywhere in this monorepo's own history. The same judgment call already made for `container/lxc`
+ cgroups v2 (scoped in `docs/NORTHSTAR_MOLTBOOK_HARDENING.md`-style planning before any code)
and for Moltbook/OpenClaw-hardening-class asks applies here, more so: **plan the real shape and
real phases first, in writing, before committing to any implementation pass.**

## Real prerequisites, checked against PARENA's own current, actual capability

- **Lexer/parser**: PARENA's own S-expression frontend is irrelevant here — a JS lexer/parser is
  a from-scratch component, parsing real JS syntax (not S-expressions) into an AST represented
  as PARENA data structures. This alone is comparable in scope to VS0's own lexer+parser
  (`src/lexer.c`+`src/parser.c`), which took real, multi-week effort to reach today's maturity.
- **Dynamic typing / tagged values**: JS values (`number`/`string`/`boolean`/`object`/`array`/
  `function`/`undefined`/`null`) need a real tagged-union representation. PARENA's own `Result`/
  `Option` precedent (`{tag; void *}`, the exact shape BURROW's Go target now also emits — see
  `BURROW/CHANGELOG.md`'s 2026-09-03 match/Result entry) is the closest existing primitive, but a
  full JS value representation needs many more tags (at minimum: number, string, bool, null,
  undefined, object-ref, array-ref, function-ref) and, critically, **real generics or a
  hand-rolled equivalent** to avoid one giant untyped blob everywhere — and VS0 has **no generic
  type parameters today** (confirmed directly and repeatedly this session: `vec.prn`'s `(Vec T)`
  and `map.prn`'s `(Map K V)` both fail to compile — "unsupported return type symbol"). This is
  the single largest real blocker: a JS engine without real generics means either (a) one
  universal boxed-value type used everywhere (workable, but a real performance and ergonomics
  cost VS0's own downstream users would feel directly), or (b) VS0 needs real generics first,
  which is itself unscoped, separate, foundational compiler work.
- **Garbage collection**: JS is a GC'd language; PARENA is explicitly region-based with no GC.
  Running real, arbitrary JS (closures capturing outer scope, circular object references,
  first-class functions escaping their creating scope) needs either a real GC bolted onto the
  V16 runtime specifically (a large, separate subsystem, orthogonal to PARENA's own region
  analyzer) or a real, provable argument that region-based allocation can host a JS heap safely
  — neither has been attempted or even sketched.
- **Object model**: JS's prototype-chain object model (dynamic property addition/deletion,
  `Object.prototype`, `[[Get]]`/`[[Set]]` semantics) has no existing PARENA analog — `defstruct`
  is a fixed-shape, compile-time-known record, the opposite of JS's own dynamic-shape objects. A
  real hash-map-backed object representation is needed; PARENA's own `map.prn` is a real,
  existing building block, but is itself blocked on the same missing-generics issue above for a
  fully general `(Map String JSValue)`, not the concrete `(Map String I32)`-class of type it
  supports today (see this session's own `bstree.prn`/`json.prn` "commit to a concrete type"
  workaround for the same real limitation).
- **Standard library surface**: even a genuinely minimal, "table stakes" JS runtime needs
  `Array`/`Object`/`String`/`Math`/`JSON`/`Promise`(at least a microtask-queue stub) — each one
  its own real, multi-file undertaking; `JSON` alone is comparable in scope to PARENA's own
  existing `json.prn` (already a real, non-trivial stdlib file).
- **Compilation target choice**: unclear yet whether V16 itself should be a VS0 (C target) or
  BURROW (Go target) program, or needs its own new emission target entirely — this has not been
  evaluated at all and is real, separate follow-up scoping.

## Explicitly out of scope for any v0/v1

- A real JIT (baseline or optimizing). Interpretation only, even if slow, is the only realistic
  starting bar.
- Full ECMAScript spec compliance (proxies, generators, async/await, most of `Intl`, WeakMap/
  WeakRef, the full numeric edge-case behavior of IEEE-754 `NaN`/`-0` semantics matching browsers
  exactly). A real "runs a meaningfully useful, deliberately narrow subset of real-world JS"
  bar, not "passes test262," is the only honest v1 target.
- DOM/browser APIs of any kind — this is a language runtime, not a browser engine. Founder's own
  "whatever the browser is doing to make this file viewable" framing is a reference point for
  ambition, not a literal DOM-compatibility requirement.
- Node.js-style module resolution / npm ecosystem compatibility.

## Real, phased plan

1. **DECIDED (2026-09-03), real, checked live, not assumed**: (a) — commit V16 to concrete,
   non-generic types rather than a real polymorphic container. Confirmed directly: VS0 still
   rejects a bare `(Vec T)`/`(Map K V)` signature, but `(Vec Tok)` with a CONCRETE struct element
   type compiles fine — so a JS token/value representation built from concrete structs (starting
   with `JsToken` below) is real and buildable today, at the real, accepted cost of "one concrete
   type per real need" rather than a genuinely generic value representation. Scoping real generic
   type parameters into VS0 itself remains real, separate, much larger, unstarted work — not
   pursued here.
2. **JS lexer (Phase 2a) — SHIPPED (2026-09-03)**, kanban priority-queue card 34134124, "parena
   v16 iteratejs engine": `stdlib/v16/lexer.prn`, a real, working, tested (`make test-v16-lexer`)
   tokenizer — number/string/identifier/punctuation tokens plus a real `TokEof` sentinel, real
   honest v0 boundaries named in that file's own header comment (integer-only numbers, no
   escape-sequence decoding, no keyword table, a small fixed punctuation set, no comments/regex/
   template strings). See `STDLIB.md`'s own "v16/lexer" section for the full writeup, including a
   real, live-found multi-file-build invocation trap (a `.prn` file calling into another stdlib
   module silently gets a wrong `void *` type guess if that module's own source isn't passed to
   the SAME `parena build` invocation — not an emitter bug, a real, easy-to-hit mistake, now named
   so it isn't rediscovered).
2b. **JS parser → AST — SHIPPED (2026-09-04)**, founder real-time "continue that work... iterate":
   `stdlib/v16/parser.prn`, a real, working, tested (`make test-v16-parser`) EXPRESSION parser
   (real precedence climbing — comparison over additive over multiplicative over primary — real
   left-associativity, parenthesized sub-expressions, real multi-argument function calls via a
   chained `AstArg` list). Real, honest v0 boundary named in that file's own header comment:
   expressions only, no statements/assignment/`if`/blocks yet (the lexer has no keyword table, so
   a real statement grammar needs that groundwork first — separate, later work). AST nodes use
   bstree.prn's own established "flat `Vec`, integer-index children, no self-referential
   pointers" convention, not a new representation. Three real, found-live VS0 emitter quirks
   worked around this pass (documented in the file itself and `STDLIB.md`'s own "v16/parser"
   section): a `do` block can never contain a `let` as any of its elements (not just non-final
   position); a `cond` clause's own body (unlike an `if` branch, a `let` body, or a function body)
   also can't be a `let` OR a `do` — both need extracting into a real, separate function whose own
   body IS the let/do; and binding a Unit-returning call's result via `let` emits invalid C
   (`void x = f();`) regardless of where that let sits, fixed by never naming a Unit result at
   all. 8 real end-to-end assertions in `tests/test_v16_parser.c`, all pass; `make test`: 345/345
   core compiler tests, zero regressions.
3. **Interpreter — SHIPPED as a spike (2026-10-15)**, but as a single-pass bytecode compiler + stack
   VM (`stdlib/v16/engine.prn`), not the tree-walker this plan originally named: the existing AST
   parser (Phase 2b) was expression-only and the VM was both simpler to write under VS0's quirks
   and faster. `lexer.prn`/`parser.prn` are untouched and still tested.
4. **GC or GC-substitute strategy — answered for the spike:** one arena per run, a byte budget, no
   collector (see "The 2026-10-15 spike"). Proven on closures-with-shared-state, callbacks that
   throw across native frames, and doubling-string/array-growth stress cases; NOT proven for
   long-lived embeddings.
5. **Minimal stdlib slice — SHIPPED (spike):** console.log, Math, JSON.stringify, Object.keys,
   Array/String methods, Error family. Not spec completeness.
6. **Re-evaluate against a real target consumer** (most likely PAPERCRAFT/GoblinFoxDragon mod
   scripting, or DUNG's own "v16 renderer" framing) before investing further — this whole
   engine's real value depends on which consumer actually needs it and what subset of JS that
   consumer's own real scripts use, which has not been determined.

## Related

- `PARENA/NORTHSTAR.md` — the language VS0/BURROW compile; this document assumes that context.
- `BURROW/NORTHSTAR.md` — the Go emission target; a real candidate compilation path for V16 once
  Phase 1's generics question is resolved.
- `EMILY/BACKLOG.md` S189-63 — the real kanban-tracked cruise-queue card this document answers
  ("V16 JS engine, native PARENA implementation, not yet started, should be NORTHSTAR-level").
- `PARENA/STDLIB.md` "bstree" and "idunapro/cli-mod" sections — the real, live precedent for the
  concrete-type-over-generics workaround Phase 1 would fall back to if generics aren't scoped
  first.
