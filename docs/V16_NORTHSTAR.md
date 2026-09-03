# NORTHSTAR — V16 (a native-PARENA JavaScript engine)

**Status (2026-09-03): Phase 1 (the generics/representation decision) and Phase 2a (a real,
tested JS lexer, `stdlib/v16/lexer.prn`) are shipped — see "Real, phased plan" below. Everything
past that (parser/AST, interpreter, GC strategy, stdlib slice) remains real, unstarted, honestly
scoped work; this document still exists so the real overall size of this ask stays written down
honestly, not silently deferred.**

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
2b. **JS parser → AST** (real JS grammar, not S-expressions) — real, separate, unstarted work,
   building on the lexer's own real `(Vec JsToken)` output.
3. **Tree-walking interpreter** over the AST — no bytecode compilation yet, correctness over
   speed, matching VS0's own historical "get a real reference interpreter working before
   optimizing" discipline.
4. **GC or GC-substitute strategy**, chosen and proven on a real, minimal closures-and-objects
   stress case before the interpreter is trusted with arbitrary real-world JS input.
5. **Minimal stdlib slice**: `console.log`-equivalent output, `Array`/`Object`/`String`/`Math`
   basics only — enough to run genuinely useful small real-world scripts, not spec completeness.
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
