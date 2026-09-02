# NORTHSTAR — PARENA

**Status:** all five VS0 DoD domains built and CI-verified. Lexer/parser (domain 1), region
analyzer (domain 2), and C emitter (domain 3) — `parena build examples/valid_only.prn -o out.c`
produces real C99 that compiles with zero warnings under `gcc -Wall -Wextra -pedantic -std=c99`
(the DoD's own literal bar) and, verified beyond the DoD's own requirement, actually runs
correctly when linked against the real runtime. Memory verification (domain 4) — the same real
emitted program runs clean under ASan+UBSan (and Valgrind, where installed), and a deliberately-
broken fixture is confirmed to actually get caught, proving the check has teeth. CLI runner
(domain 5) — the DoD's own literal bar (`./parena build input.prn -o output.c`, exit 0 on
success, exit 1 on a real region error) verified automatically (`tests/integration/
run_domain5_check.sh`, wired into CI): a real valid file exits 0 and writes real, non-empty
output; a real region-safety violation (the DoD's own literal example, an escaping `:region/
scratch` pointer) exits 1 and leaves no stale output file behind.

**Explicitly still out of scope for VS0** (per the DoD table's own "Explicitly out of scope"
line, unchanged by the above): the stdlib, the editor/plugin API, JVM/TypeScript/WebAssembly
targets, macros (`defmacro`), and the mod-surface integration question. All five domains being
done means VS0 itself — the parser + region analyzer + C emitter + CLI — is complete; it does
not mean the mod-surface stdlib (`stdlib/editor/*`, `stdlib/gfd.prn`, `stdlib/mapbuilder/*`, the
`#target`/`defenum`/`defstruct` emitter extensions layered on top of VS0 this same session) is
finished — that work continues on its own, separate, real timeline, tracked in `STDLIB.md` and
`EMILY/BACKLOG.md`, not conflated with VS0's own closed scope here.
**Date:** 2026-08-20.

## What this is

Founder's own mission statement for the project, stated directly mid-session (2026-08-20), worth
carrying verbatim rather than paraphrased: **"PARENA is a language to make your software more
programmable via fluid and composible plugin APIS."** Everything below — the editor/plugin
surface, the GFD mod-surface binding, PITVIPER hosting a PARENA-authored vim-like editor, the
"build the plugin API, then the feature, then expand the API only when a new feature needs it"
development pattern the founder separately named as "a new emerging pattern" already recurring
across GFD's mod API and PITVIPER's own plugin surface — is that mission applied to a specific
surface, not a separate goal.

The concrete adoption mechanism, stated the same session: **"we bolt on the plugin interface on
to software and then PARENA slowly eats the codebase from the outside in."** A real, named
software-migration shape (close kin to the "strangler fig" pattern — replace a legacy system
piece by piece from its edges rather than a rewrite) applied specifically to how PARENA is meant
to enter an existing codebase: expose a plugin/FFI boundary first (PITVIPER's Go core gaining a
plugin API; GFD's `apps2/battlegrounds_gui` gaining the `gfd` binding surface), let PARENA code
live on that boundary, and only later — incrementally, as real need justifies it, not on a fixed
schedule — does more of the host's own internals get rewritten in PARENA. Not started as an actual
rewrite anywhere yet; the boundary-first half of this (PITVIPER's plugin API) is the concrete near-
term work.

A new systems language, built from scratch, with a real editor/plugin surface as a first-class
part of the design (not bolted on later). Founder, real-time, across a fast, fragmented thread:
"LANGUAGE SPEC FOUND" → "PARENA UPSTREAM" → "ITS AN EDITOR AND A LANGUAGE" → "its like EDU
scripts scary older sister" → "parena is the native API does that make sense" → "PARENA needs a
standard library like the golang one" → "just build it right in yolo like php" → "but using
PARENA to do it" → "then northstar that bitch and build it pure before we have to think about how
it plays with EDU script." Confirmed directly (AskUserQuestion): PARENA is new, built from
scratch — not adopting an existing external project.

**Real source material, not invented here**: `Building Your Own Integrated Development
Environment.docx`, uploaded to this repo by the founder — a Gemini conversation transcript that
starts as generic "how do I build an IDE" research (Electron vs. Tauri, GTK vs. SDL2+ImGui vs.
ncurses, Rust's `ropey`/`egui`/`tree-sitter`/`tokio` stack) and converges into a real, specific
language design partway through. Everything under "Language design" and "VS0" below is pulled
directly from that document, not guessed at. The generic IDE-architecture research at the start
of the doc is real prior art for the editor half of "editor and a language," referenced where
relevant, not repeated wholesale here.

**"Build it pure before we have to think about how it plays with EduScript"**: `GoblinFoxDragon`'s
`docs2/MOD_SURFACE_NORTHSTAR.md` scoped EduScript (a real, already-live, already-running bytecode
VM — see that doc) as the incumbent candidate for GFD's mod-surface scripting language, while
flagging that the founder had "invented a language" not yet located. PARENA is that language.
Per the founder's own sequencing, this doc and PARENA's own build stay deliberately silent on
mod-surface integration — that's real, separate, later work, not decided here.

## Why this exists — "EduScript's scary older sister"

EduScript (per `MOD_SURFACE_NORTHSTAR.md` §1) is a real, working, narrow stack machine: `int
stack[256]`, `int vars[64]`, 17 opcodes, no arrays, no user-defined functions, no recursion —
good enough for one puzzle-object-manipulation trial, explicitly not enough to carry a real
mod-surface on its own. PARENA is not an extension of EduScript's opcode set — it's a different
kind of thing entirely: an ahead-of-time-compiled, statically-typed, S-expression language with
**compile-time memory-safety verification** (region typing, not garbage collection or manual
free) and **multiple real compilation targets** (C, JVM, TypeScript, WebAssembly). Where EduScript
interprets bytecode at runtime, PARENA compiles to native code with zero-overhead cleanup —
"native API" in the founder's own words.

## Language design (from the source document, verbatim structure)

### Syntax

| Element | Syntax | Notes |
|---|---|---|
| S-expressions | `(fn-name arg1 arg2)` | Execution & function application |
| Vectors/tuples | `[elem1 elem2 elem3]` | Fixed-size or stack/arena-allocated contiguous arrays |
| Maps/structs | `{:field1 val1 :field2 val2}` | Anonymous or typed key-value pairs |
| Keywords | `:keyword`, `:region/scratch` | Immutable symbolic identifiers |
| Type & region signature | `(var : Type @ Region)` | Binds a type and an explicit lifetime rank |
| Ownership transfer | `!var` or `(move var)` | Forces a linear move, invalidating `var` in scope |
| Borrowing | `&var` or `&mut var` | Borrow handle without transferring ownership or region |

### Declarations & bindings

`def` (global constant/static), `defn` (typed function, explicit parameter region contracts),
`defstruct` (aggregate memory layout), `defenum` (tagged unions — `Option`, `Result`), `defmacro`
(compile-time AST transforms), `let` (local bind), `set!` (mutate existing mutable binding/field).

### Control flow & pattern matching

`if`/`when`/`cond` (branching), `match` (type-safe destructuring on structs/enums/`Option`/
`Result`), `loop`/`recur` (zero-overhead tail-recursive iteration), `while`/`for` (imperative
traversal).

### Modules & packaging

`module` (namespaces the current file), `import` (pull symbols from another Parena package or a
target-native header/class), `export` (mark visible outside the module boundary).

### Memory model — region typing (the actual core idea)

Every value carries an explicit region rank (`:region/scratch` = 0, `:region/buffer` = 2, etc. —
higher rank = longer-lived). The compiler enforces two invariants at compile time, not runtime:

- **Assignment invariant**: `Region(Source) ⪰ Region(Destination)` — assigning a short-lived
  scratch pointer into a longer-lived buffer-scoped slot is a compile error.
- **Return invariant**: `Region(Value) ⪰ Region(Caller)` — returning an address allocated inside
  an internal `with-arena` block (which is about to be torn down) is a compile error.
- **Move/ownership invariant**: consuming a linear (`!`-prefixed) variable invalidates all prior
  bindings to it — referencing a moved handle after it's been passed to a consuming function is a
  compile error, the mechanism that rules out double-close bugs on native sockets/files/
  subprocesses.

This is the actual "memory safety without a GC or manual free" story — closer in spirit to
region-typed research languages (Cyclone, Vale's generational references) than to Rust's borrow
checker or a tracing GC, and the real reason a from-scratch language is being built here rather
than reusing something existing: EduScript has no type system at all, and adding compile-time
region-safety to an existing stack-machine bytecode VM isn't a good fit for either.

### Core idioms (real code from the source document)

**Scratch-to-buffer promotion** — the canonical pattern for expensive temporary work that
shouldn't pollute persistent memory:

```clojure
(defn process-and-persist [(file-buf : Buffer @ :region/buffer)
                           (path : String @ :region/scratch)]
  ;; 1. Do heavy work in a temporary 8KB scratch scope
  (let [result-ptr ...]
    ...))
```

**Zero-allocation `Result`/`Option` matching** — errors as tagged unions bound to the active
task/scratch scope, no heap exceptions:

```clojure
(defn safe-parse-int [(input : String @ :region/scratch)
                      (scratch : Arena @ :region/scratch)]
  (match (string/parse-i32 input)
    ((Ok val) (log/info "Parsed integer:" val))
    ...))
```

**Linear resource transfer** — `!`-prefixed handles prevent double-close bugs on native
resources:

```clojure
(defn write-log-and-close [(!file : FileHandle @ :region/task)]
  (io/write-string !file "Operation completed\n")
  (io/close !file))
  ;; Calling (io/write-string !file ...) after this point is a compile error.
```

**Cross-target native FFI** — `#target` conditional forms emit target-native syntax during
codegen:

```clojure
(defn target-yield []
  #target
  {:c   (inline-c "sched_yield();")
   :jvm (inline-java "Thread.yield();")
   :ts  (inline-ts "await new Promise(r => setTimeout(r, 0));")})
```

### Multi-target compilation

| Target | Arena under the hood | Scope boundary construct | Memory behavior |
|---|---|---|---|
| Native C | Bump pointer on `malloc()` | `__attribute__((cleanup))` | Direct pointer rewinds |
| JRE (Java 22+) | `java.lang.foreign.Arena` | `try (Arena a = ...)` | Off-heap Panama segments |
| TypeScript | `ArrayBuffer` + `DataView` | `using arena = ...` | Zero V8 garbage collection |
| WebAssembly | Linear Wasm memory page | Stack frame pointer | Fixed byte offset reset |

VS0 (below) targets **C only**. JVM/TypeScript/WebAssembly backends are real, specified,
explicitly **not** in VS0's scope — later milestones, not started.

### Editor/plugin API — the "editor" half

A namespaced plugin surface, written *in* Parena itself, for extending an editor built on top of
it:

| Module | Namespace | Purpose |
|---|---|---|
| Plugin Core | `(parena/plugin ...)` | Lifecycle, configuration, command palette registration |
| Buffer Access | `(parena/buffer ...)` | Read/insert/delete/select text ranges in active buffers |
| Events & Hooks | `(parena/events ...)` | Subscribe to editor actions (on-save, on-change, keybindings) |
| UI & Overlay | `(parena/ui ...)` | Gutter decorations, inline diagnostics, status bar, popups |

**Not started, not in VS0.** The editor shell itself (which of Electron/Tauri/GTK+GtkSourceView/
SDL2+ImGui/ncurses+Tree-sitter from the source document's own earlier research) is an open,
undecided question — flagged, not resolved here. VS0 is compiler-only.

## Standard library — real gap, not designed yet

Founder: "PARENA needs a standard library like the golang one." Go's stdlib is the explicit
reference point (batteries-included, consistently designed, `io`/`os`/`net`/`strings`-style
package layout) — genuinely not designed yet. The source document's own code samples reference
`string/parse-i32`, `io/write-string`, `io/close`, `log/info`, `buffer/set-data` as if a stdlib
already exists, but none of those packages are specified beyond their call-site usage in the
examples above. Real open work, sequenced after VS0: design the actual package boundaries and
function signatures, not invented in this pass.

## VS0 — the real, concrete, already-scoped starting point

Everything in this section is copied from the source document's own "VS0 and DOD" answer, not
invented for this NORTHSTAR — the founder's own spec research already did this scoping.

**Goal**: validate the core hypothesis — parsing S-expressions, enforcing compile-time region
escape checking, and transpiling to zero-overhead C with automatic cleanup.

**Test program** (`test.prn`) — must compile a program demonstrating both a valid promotion
pattern and a rejected invalid region escape:

```clojure
;; Valid: Promotes scratch result to buffer arena
(defn load-config [(buf-arena : Arena @ :region/buffer)]
  (with-arena [scratch :region/scratch 1024]
    (let [temp-str (alloc scratch String "config.json")
          real-buf (alloc buf-arena String "parsed_data")]
      real-buf)))

;; INVALID: Should fail compilation during region analysis
(defn break-safety [(buf-arena : Arena @ :region/buffer)]
  (with-arena [scratch :region/scratch 1024]
    (let [bad-str (alloc scratch String "escaped_memory")]
      ;; Error: Assigning scratch pointer to buffer-scoped slot
      (buffer/set-data buf-arena bad-str))))
```

**Compiler architecture** (`parena-c`):

- **Parser**: S-expression reader in C, using a single bump arena for compiler AST nodes.
- **Analyzer**: single-pass tree-walker assigning region ranks (`:region/buffer` = 2,
  `:region/scratch` = 0) and checking `Region(Source) ⪰ Region(Destination)`.
- **Emitter**: C code generator emitting standard C99 with `__attribute__((cleanup))` macros for
  `with-arena` forms.

**Definition of Done**:

| Domain | Completion criteria | Verification method |
|---|---|---|
| Lexer & Parser | Reads valid `.prn` files into an AST without heap allocations outside the compiler's bump arena | Unit tests on balanced and imbalanced S-expressions |
| Region Analyzer | Valid script compiles cleanly to C; invalid script aborts with `Compile Error: Escaping region pointer from :region/scratch to :region/buffer at line X` | Automated CLI test suite, 1 positive + 1 negative case |
| C Code Generator | Emits clean C using scoped-arena macros for `with-arena` constructs | `gcc -Wall -Wextra -pedantic -std=c99` compiles emitted output with 0 warnings |
| Memory Verification | Compiled C output has zero runtime leaks or use-after-free | Runs clean under Valgrind (0 bytes leaked) and AddressSanitizer (`-fsanitize=address`) |
| CLI Runner | Single executable driving the pipeline: `./parena build input.prn -o output.c` | Exit code 0 on success, 1 on region error |

**Explicitly out of scope for VS0**: the stdlib, the editor/plugin API, JVM/TypeScript/WebAssembly
targets, macros (`defmacro`), and the mod-surface integration question. VS0 is the parser +
region analyzer + C emitter + CLI, nothing else.

## Self-hosting — real progress started (2026-08-27)

Founder, real-time: "ok but after we have a compiler we also need to write parena in parena" →
"not c" → "silly." A real, well-understood language-engineering milestone (the same one Go, Rust,
and most serious systems languages eventually hit): once the language and its C-implemented
compiler (`parena-c`, VS0 above) are complete enough, rewrite the compiler *in Parena itself* —
explicitly not staying C underneath, per the founder's own emphasis. This section's own gating
condition ("Architecturally this can't start until VS0's remaining domains exist") is now
satisfied — VS0 is complete, all 5 DoD domains done and CI-green (see root `CLAUDE.md`'s current
status line) — so the C-implemented half of the gate is real, not aspirational.

**Real first step taken, same day, founder real-time: "self hosted compiler" → "continue"**:
`selfhost/lexer.prn` — a real, faithful PARENA-language port of `src/lexer.c` (VS0's own C
tokenizer), compiled BY the existing C-based `parena-c` (self-hosting only becomes real once a
PARENA-in-PARENA pipeline can compile ITSELF — this is the first domain toward that, not a claim
of having reached it). Real design departures from `src/lexer.c`'s own imperative, pointer-
mutating shape, forced by VS0's own real, confirmed current limits (no struct field mutation, no
tuple returns): every "advance" operation returns a NEW `Lexer` value; `lexer-next` returns a
`LexStep` struct bundling the produced `Token` and the advanced `Lexer` together, a named-struct
stand-in for a tuple return. 60 real assertions in `tests/test_selfhost_lexer.c` (hand-traced
against `src/lexer.c`'s own real, documented behavior — parens/brackets, bang/amp-prefixed
symbols, keyword-vs-standalone-colon disambiguation, negative/decimal numbers, every real escape
sequence a string literal supports, unterminated-string error reporting, `;;` comment skipping,
and a real fragment lifted from `stdlib/string.prn` itself), all passing, zero regressions across
the full existing suite + bazel + real mingw cross-compile.

**Real compiler bugs found and fixed along the way** (not guessed at — confirmed live via actual
gcc errors self-hosting this file surfaced, the exact kind of real-world exercise a reference
lexer port is good for): `emit_if`/`emit_binop` in `src/emit.c` built their own final C ternary/
expression text via a fixed 512/1024-byte `snprintf` buffer, silently TRUNCATING mid-identifier
once a deeply-nested `cond`/`if`/comparison chain's combined text exceeded it (confirmed live via
a real `'LexSte' undeclared; did you mean 'LexStep'?` gcc error) — the exact same class of bug
`emit_cond` had already been written to avoid (see its own header comment), just not yet applied
to `emit_if`/`emit_binop`, or to 3 more `sb_appendf(out, "return %s;", ...)` call sites building a
function's own final return statement. Fixed everywhere found via the same real, growable `StrBuf`
+ direct `sb_append` pattern `emit_cond` already established — not a speculative blanket rewrite
of every remaining fixed-buffer site in `emit.c` (~25 more exist, unaudited, a real, separate,
deferred systemic-review item, not attempted here). Also found: a match-bound `Ok` payload's own
real emitted C variable stays generically `void *` (already documented as a real, established
convention in `stdlib/regex/syntax.prn`'s own header comment — `(get-field (deref x) :field)`,
never bare `(get-field x :field)`), applied here for the first time outside that one file.

**Real second step, same day (founder: "continue"/"clnt")**: `selfhost/parser.prn` — a real,
faithful PARENA-language port of `src/parser.c` (VS0's own recursive-descent reader over the
lexer's own token stream, producing the generic S-expression AST `src/ast.h` defines). Real design
departures forced by the same class of VS0 limits: no `setjmp`/`longjmp` (every parse function
returns a real `(Result ParseStep SelfhostParseError)`, propagating an `Err` upward through
ordinary match-based short-circuiting — arguably simpler than the C reference, not just a forced
substitution); `Node.children` is `(Vec Node) @ Region` (a Vec of the struct itself, BY VALUE),
the same real, already-proven shape `stdlib/regex/syntax.prn`'s own `PatternNode` uses for its own
recursive `Concat`/`Alt` variants; no in-place `node_push_child` mutation — every compound form's
children accumulate into a local Vec via ordinary `vec/push!` (captured by an outer `let`, not
threaded through `loop`/`recur`) before the parent `Node` is constructed once, fully formed, the
same "build the Vec first, construct the struct once" shape `stdlib/string.prn`'s own `split`
already uses.

Found and fixed a real, deeper compiler gap along the way (confirmed live via a real "dereferencing
'void *' pointer" gcc error attempting `(deref e)` on a match-bound `Err` payload): the emitter's
own real type-hint mechanism for a single-field `Ok`/`Some` payload bound from a KNOWN function's
`(Result X E)` return type (added 2026-08-21 for `unwrap`) was, by its own header comment's honest
admission, "only valid for Ok/Some specifically ... this emitter has no equivalent lookup for
[the Err/None side] at all." Added the real, symmetric counterpart (`resolve_result_error_type`,
`scrut_error_type`, a new `error_type` field on `DefnReturnType`) so a match-bound `Err` payload's
own real type is now resolvable the identical way, letting `deref` correctly cast it instead of
silently staying generic `void *`. `None` still has no equivalent (`(Option X)` carries no error
type to look up).

51 real assertions in `tests/test_selfhost_parser.c`, hand-traced against `src/parser.c`'s own
real, documented behavior — including the DoD's own exact required error wording for 3 of 4 real
failure cases (`tests/test_lexer_parser.c`'s own C-reference suite already pins these same 3
strings verbatim); the 4th (an unterminated string literal) is this file's own honest, documented,
minor departure — `SelfhostParseError`'s own header comment explains why it doesn't repeat the C
reference's own "line number appears twice" artifact. Full local suite + bazel build/test + real
mingw cross-compile all clean, zero regressions.

**Real third step, same day (founder: "continue"/"contginue")**: `selfhost/region.prn` — a real,
faithful PARENA-language port of `src/region.c` (VS0's own DoD-domain-2 checker: a single-pass
symbol-table walk enforcing the assignment invariant, Region(Source) >= Region(Destination), over
the parser's own real Node AST). Real design departures forced by VS0's own confirmed current
limits: no linked `Scope`-with-a-`parent`-pointer chain — VS0 has no way to store a reference type
as a struct field at all, so the whole scope is ONE flat `(Vec Binding) @ Region`, most-recently-
pushed bindings scanned first (a real, functionally-equivalent substitute for the C reference's own
per-scope-then-walk-to-parent lookup, not a silent behavior change); "entering a child scope"
(`with-arena`/`let`/a `defn`'s own params) copies every existing binding into a fresh Vec before
adding the new one(s), rather than mutating a shared Scope in place.

Found and fixed 3 more real bugs along the way (all confirmed live, none guessed at): (1) a `match`
nested as a VALUE inside an `if`/`cond` branch that isn't itself in tail position doesn't compile
("unknown identifier" at the match's own bound name) — a real, deep architectural gap (match's own
value production is fundamentally statement-shaped, incompatible with being embedded in a single C
ternary the way `emit_if`/`emit_cond` compose their own branches; a general fix would need
something like GCC's own statement-expression extension, genuinely incompatible with this whole
repo's `-std=c99 -pedantic` discipline) — worked around throughout this file by extracting every
such `match` into its own small function instead, whose own body directly IS the match (a real,
already-proven-safe tail position). (2) An Ok-bound raw `(Vec T)` Result payload doesn't get its
own element type resolved (stays generic `void *`) — worked around by wrapping the Vec in a real
struct field instead (a `ScopeResult` wrapper), the same "named-struct tuple-return stand-in"
pattern `LexStep`/`ParseStep` already establish. (3) A genuine LOGIC bug in this file's own first
draft, not the compiler: `walk-let-bindings` seeded its own accumulator scope from an empty Vec
instead of a real copy of the outer scope, silently losing every outer binding (a `defn`'s own
params, an enclosing `with-arena`'s own bound name) the moment a `let` was entered — found via a
real minimal debug harness after the DoD's own required negative test case came back a false
negative, fixed with a new `scope-copy` helper.

9 real assertions in `tests/test_selfhost_region.c`, EVERY ONE lifted directly from
`tests/test_region.c`'s own real test suite (the DoD's own required 1 positive + 1 negative case,
plus its own 5 real edge cases) — identical real inputs against a completely independent
implementation, including an exact match on the DoD's own required error message string. Full local
suite + bazel build/test + real mingw cross-compile all clean, zero regressions.

**Real fourth step, same day (founder: "continue", repeated) — a real, complete, end-to-end vertical
slice**: `selfhost/emit.prn` — a real, faithful PARENA-language port of `src/emit.c`'s own C
emitter, but deliberately scoped to VS0's own ORIGINAL, narrow domain-3 DoD acceptance bar (`src/
emit.h`'s own real, honestly-documented v0: "only understands the exact shape test.prn's own valid
function uses... `char *` is the only inferred type"), not the current, much-expanded (5000+ line)
C emitter grown incrementally to serve the whole real stdlib over many sessions — the same "one
real, faithful, honestly-scoped domain at a time" discipline lexer/parser/region already
established. Concretely: `with-arena`, `let`+`alloc` (String only), Arena-typed params, a bare
symbol as a function's own tail — exactly `examples/valid_only.prn`'s own real `load-config` shape,
the actual real DoD acceptance file.

**This completes a real, working, end-to-end vertical slice of a PARENA-in-PARENA pipeline**:
`selfhost/lexer.prn` -> `selfhost/parser.prn` -> `selfhost/region.prn` -> `selfhost/emit.prn`,
verified by actually running `examples/valid_only.prn`'s own real source through all four,
compiling the REAL resulting C with a real `gcc -std=c99 -Wall -Wextra -pedantic -Werror`, linking
it against the SAME real `tests/integration/driver_valid_only.c` domain 4's own check already uses
against the C reference, and actually RUNNING it — the driver's own real
`assert(strcmp(result, "parsed_data") == 0)` passes against the selfhost emitter's own real output,
not just the C reference's. This is the first real point where "self-hosted" stops being aspirational
for at least one real, narrow program shape — still compiled BY the existing C-based `parena-c`
(self-hosting only becomes real once a PARENA-in-PARENA pipeline can compile ITSELF, the real,
much bigger next milestone), but the pipeline's own real OUTPUT is now proven correct end to end.

Real design departures, same classes lexer/parser/region already document: no arena-typed
"pointer-parameter vs. local-Arena-value" tracking via a linked Scope-with-parent chain (the same
flat-Vec substitute region.prn's own header comment justifies, reused for a different real
question: does an arena-typed name need `&` before it in the emitted C). Every `match` used as a
non-tail VALUE is extracted into its own small function (the same real, confirmed VS0 emitter gap
region.prn's own bug list already documents — reused here without needing to rediscover it).
`mangle` is deliberately narrow (hyphen-only; `/` and trailing `!`/`?` are real, flagged, unneeded
gaps for this file's own real target). Return type is hard-coded `char *`, matching the real,
documented v0 scope exactly, not a real type-inference pass.

14 real assertions in `tests/test_selfhost_emit.c` — structural checks on the generated C text
(the real, correctly-mangled signature, the real arena-kind distinction between `buf_arena` bare
and `&scratch`, the real cleanup attribute) PLUS the real compile-and-run proof described above.
Full local suite + bazel build/test + real mingw cross-compile all clean, zero regressions.

Not scoped further than this narrow v0 emitter here — arithmetic, `match`, `loop`/`recur`,
closures, Vec/Map generics, and every real stdlib-driven special case the current C emitter now
handles are real, separate, unstarted follow-up work, the same honest boundary every domain in this
whole effort has drawn.

**Real fifth step, same day (founder: "continue on welf hosted parena compiler")**:
`selfhost/main.prn` — a real, faithful PARENA-language port of `src/main.c`'s own `cmd_build`
(VS0's CLI-runner domain: parse → region-analyze (abort on error) → emit → write output file), the
same real pipeline order `cmd_build` itself uses. The first selfhost file to do real disk I/O
(`io/file-open`/`io/read-string`/`io/write-string`/`io/file-close`) rather than work purely on
in-memory strings the way `selfhost/emit.prn`'s own test harness did — `build-file` is a real,
callable, end-to-end "compile this file to that file" function.

Real, honest scope note: NOT yet a real argv-parsing standalone executable the way
`./parena build in.prn -o out.c` is — confirmed live (grepped `src/emit.c` for any `int main`
emission: none) that parena-c has no `(defn main ...)` → C `int main` emission convention for ANY
PARENA program yet, selfhost included; every emitted program stays a library of functions linked
into a hand-written C driver. Real argv plumbing and a main-emission convention are a genuinely
separate, unstarted emitter feature, not attempted here. `build-file` is the real pipeline-
sequencing logic `cmd_build` wraps around argv parsing, exercised by a real C test driver
(`tests/test_selfhost_main.c`) calling it with real path strings — the same honest shape
`test_selfhost_emit.c`'s own driver already established for the emit-only step.

Two more real bugs found and fixed along the way (both confirmed live via real gcc errors, not
guessed at): (1) `node-kind-code`/`is-symbol?`/`is-symbol-headed-list?`/`is-call-named?` were
defined identically, verbatim, in both `selfhost/region.prn` and `selfhost/emit.prn` — the same
real, already-documented "private top-level names aren't module-scoped" gap `join-all` hit earlier
(region.prn/emit.prn), just never actually exercised for THESE 4 names before now, since no prior
build ever compiled region.prn and emit.prn together in one translation unit (`test-selfhost-region`
only ever built lexer+parser+region; `test-selfhost-emit` only ever built lexer+parser+emit —
`build-file` is the first thing that needs both). Fixed the same way: emit.prn's own 4 copies
renamed with an `emit-` prefix (`emit-node-kind-code`, `emit-is-symbol?`,
`emit-is-symbol-headed-list?`, `emit-is-call-named?`), region.prn's left untouched. (2) A match-
bound `Ok` payload of a real `defstruct` type (here, `io/file-open`'s own `FileHandle`, and
`selfhost/parser/parse-program`'s own `Node`), passed DIRECTLY as a function's own by-value
argument (not through `get-field`), stays generic `void *` and fails to compile — the same real,
already-documented "match-bound Ok payload needs `deref`" convention `stdlib/regex/syntax.prn`'s
own header comment established for the `get-field (deref x) :field` case, confirmed here to apply
identically to a bare `(deref x)` passed as a whole-value argument, not just inside `get-field`
(and already precedented elsewhere in the codebase — `stdlib/dataframe.prn`'s own `(vec/push!
&out-cols (deref col))` — just not yet needed by any prior selfhost file).

7 real assertions in `tests/test_selfhost_main.c`: a real end-to-end run of `build-file` against
the real `examples/valid_only.prn` DoD acceptance file (open → read → parse → region-analyze → emit
→ write, all inside `build-file` itself, not the test harness), a check that the real output file
exists and holds real, non-empty, correctly-mangled C, a real `gcc -std=c99 -Wall -Wextra -pedantic
-Werror` compile of that disk-written file linked against the same real
`tests/integration/driver_valid_only.c` domain 4 already uses, an actual run of the result, and a
real negative case (a nonexistent input path returns a clean `Err`, not a crash). Full local suite
(336 tests) + `make test-selfhost-lexer/parser/region/emit` (zero regressions) + real mingw
cross-compile of the new test file all clean. Wired into real CI as a 5th, Linux-only step
(`test-selfhost-main`, same `Makefile` pattern as domains 1-4) — Linux-only for the same real,
already-documented reason domain 4 stayed Linux-only: `build-file`'s own real compile+run step
shells out to `gcc` via `system()`, not wired to the Windows/macOS jobs' own selfhost loop (still
just `lexer parser region`).

Not scoped further than this here either — the real next step toward "self-hosting" actually
becoming true (not just a proven vertical slice) is compiling the selfhost pipeline's OWN five
`.prn` files (lexer/parser/region/emit/main) back through itself, which needs the much-expanded
language coverage (`match`, `loop`/`recur`, arithmetic, closures, Vec/Map generics, real argv/main
emission) `selfhost/emit.prn`'s own "not scoped further" paragraph above already flags as real,
separate, unstarted follow-up work — real, honest, unstarted, not attempted here.

**Real, empirical first attempt at that same day (founder: "continue working on self hosted parena
compiler")**: with `build-file` now real and callable, actually tried it — `build-file
"selfhost/lexer.prn" "/tmp/out.c"`, the pipeline's own first real attempt to compile a real piece of
ITSELF. Confirmed live, not guessed: it segfaulted. Root cause, found via `gdb`'s own real
backtrace: `emit-let-bindings` called `emit-alloc-call` unconditionally on every let-binding's own
expr-node, with zero check that the node was actually an `alloc` call at all (the header comment
already honestly flagged this as a real, narrow-v0 assumption — just never enforced). `lexer.prn`'s
own real first let-binding, `(let [lx0 (selfhost/lexer/new-lexer src)] ...)`, is a genuine, in-scope
PARENA shape (a plain function call) this narrow emitter has never claimed to support — `vec/get`'s
own real, honest out-of-bounds convention returns `NULL` (see `runtime/parena_runtime.h`), but
`emit-alloc-call` then did a bare `get-field` on that `NULL`, which has no such check and crashes at
the C level. "Unsupported" crashing the compiler process itself is real harm regardless of how
narrow v0's own honest scope already is — every other domain in this whole effort holds to "fails
honestly … not guessed C" (this doc's own emit.prn write-up above), this was the one real gap in
that discipline. Fixed with a real `alloc-call-shaped?` guard (`emit-is-call-named?` + a real
`vec/len` check before ever touching the node's own children) in `selfhost/emit.prn` — a non-
alloc-shaped binding now emits a real, clean C `#error` line instead, verified to make a subsequent
`gcc` compile fail with a real, comprehensible diagnostic (exit 1, "unsupported let-binding shape")
rather than crash anything. New regression test in `tests/test_selfhost_emit.c` (a hand-written
non-alloc-shaped let-binding, not the full self-compile fixture) proves the fix without needing
`lexer.prn`'s own much larger real gap surface as a test dependency. 3 new assertions there (13
total for domain 4); full local suite (336 tests) + all 5 selfhost domains + real mingw cross-
compile all clean, zero regressions.

Real, honest picture after this fix: `selfhost/lexer.prn` still doesn't actually compile through
the selfhost pipeline — the segfault is gone, but the resulting C now fails a real `gcc` compile
with dozens of genuine, expected errors (undeclared struct-typed identifiers in tail position,
`return` statements with no value in non-`Unit` functions, and more), all real symptoms of the same
already-documented, much bigger gap (no `match`/`loop`/arithmetic/closures/generics support). This
was never expected to make a real self-compile succeed — the real, scoped value here was turning a
crash into an honest, diagnosable failure, the same bar every other domain in this effort already
holds itself to. The actual language-coverage expansion stays real, separate, unstarted follow-up
work.

**Real first step of that language-coverage expansion, same day (founder: "continue working on
self hosted parena compiler")**: `emit-params` used to hard-code EVERY param's own C type as
`Arena *`, regardless of its real declared type — this file's own prior header comment already
honestly flagged it ("every param is assumed Arena-typed... src/emit.c's own real, current version
handles I32/String/Vec/... params too; a real, separate, deferred gap"). Concretely wrong for a real,
live function: `selfhost/lexer.prn`'s own `new-lexer` takes `(src : String @ Region)`, a String
param, which the old code silently declared as `Arena *src` instead of the correct `char *src`.
Fixed with two new real, narrow helpers: `param-type-name` (reads a param's own type-name symbol —
confirmed live via a real AST dump of `(src : String @ Region)` that it always parses as an NList
`[NSymbol name, NColon, NSymbol Type, NAt, ...]`, so index 2 is always the type-name symbol) and
`param-c-type` (a real, narrow type-name → C-type mapping: `String` → `char *`, `I32` → `int`,
anything else — a real defstruct type, `&Type`, `Bool`, `F64`, ... — falls back to the file's own
prior universal `Arena *` behavior, an honest, narrow v0 boundary, not a claim of covering every
real param shape). Only an Arena-typed param is still added to the real `ArenaBinding` scope
(`resolve-arena-ref`'s own real `None` case already falls back to a bare mangled name for anything
not found in scope, which is exactly the correct behavior for a String/I32 param referenced
elsewhere). Found and fixed the same real, already-documented "inline `get-field` on a bare
`vec/get` call doesn't compile" gap along the way (`region.prn`'s own `is-symbol-headed-list?`
header comment already covers this class) — restructured `param-type-name` with an explicit `let`
binding.

7 new real assertions in `tests/test_selfhost_emit.c` (20 total for domain 4): a real String-typed
param now gets a real `char *`, a real I32-typed param gets a real `int`, and a real Arena-typed
param still gets `Arena *` and is still correctly scoped (an `alloc` call referencing it still
resolves bare, no stray `&` — the zero-regression check for the exact codepath this change touches).
Full local suite (336 tests) + all 5 selfhost domains + real mingw cross-compile clean, zero
regressions.

Real, honest picture, re-checked via the same self-compile diagnostic used to find the original
crash: `selfhost/lexer.prn` still doesn't compile through the pipeline, and the real error count
against it is UNCHANGED by this fix (still dominated by the same 35 `#error` "unsupported let-
binding shape" instances plus a real, separate, not-yet-diagnosed class of "undeclared identifier"
errors — `Region`/`I32`/`Lexer` bare tokens leaking into value position, likely a real bug in
`emit-form`'s own fallback-to-bare-symbol-tail branch misfiring on a non-symbol node). Expected and
honest: `new-lexer`'s own params were never what was blocking `lexer.prn`'s self-compile — this fix
closes a real, independently-confirmed type-correctness bug on its own merits, not a claim of moving
`lexer.prn` closer to compiling. The dominant real blocker (non-alloc-shaped let-bindings, i.e. real
function calls used as `let` values) is real, separate, unstarted follow-up work — the next, much
larger step in this same expansion.

**Real second step of that expansion, same day (founder: "continue working on self hosted parena
compiler")**: went after that dominant blocker directly — a `let`-binding whose value is a real,
plain function call (not `alloc`), the exact shape `selfhost/lexer.prn`'s own real
`(let [lx0 (selfhost/lexer/new-lexer src)] ...)` needs, and the one responsible for 35 of the ~57
real self-compile errors found above. `emit-let-bindings`'s own 2-way `alloc-call-shaped?` dispatch
became a real 3-way one (`emit-let-value`/`let-value-error-prefix`): a real `alloc` call (unchanged),
a real **plain call** (new), or — still — a clean `#error` for anything else.

A plain call is scoped deliberately narrow: `plain-call-shaped?` requires a symbol-headed list that
isn't `alloc`, isn't a `vec/`-qualified call (`is-vec-call?`'s own real, deliberate, PERMANENT
exclusion — the identical real collision risk `src/emit.c`'s own `mangle_call_name` already guards
against: `vec` is a real, hardcoded runtime pseudo-module, not a registered `.prn` module this
narrow emitter has any `find_defn_return_type`-style registry to disambiguate from a real,
same-named user function), and every real argument is itself a bare symbol (no nested calls, no
literals — `every-call-arg-symbol?`'s own real, narrower-than-the-C-reference scope). Each argument
resolves through the same real `resolve-arena-ref` every other arena-typed reference in this file
already goes through — correctly falls back to a bare mangled name for a non-arena-scoped symbol,
exactly right for a `String`/`I32` argument, no new logic needed there.

A real `/`-qualified call name (`selfhost/lexer/new-lexer`) needed a new `mangle-call-name`: a real,
narrow port of `src/emit.c`'s own `mangle_call_name` for the one shape this emitter supports — strip
the name down to its own real final segment (via `string/split` on `/`, confirmed live that the full
C compiler's own generated top-level defn names are never module-prefixed: grepping the real
generated C for `new_lexer` finds a bare `char * new_lexer(...)`, not
`selfhost_lexer_new_lexer`), then run the existing hyphen-only `mangle` on that segment. Real,
honest, narrower than the C reference: no `find_defn_return_type`-backed disambiguation (this narrow
emitter has no such registry) — safe only because `plain-call-shaped?` already excludes the one real
collision risk (`vec/`) the C reference itself guards against.

13 new real assertions in `tests/test_selfhost_emit.c` (26 total for domain 4): the original crash
regression fixture was updated to a shape `plain-call-shaped?` still deliberately excludes (a
nested-call argument) since its own original fixture is now real, supported emission, not an error
— plus new coverage proving a bare plain call, a `/`-qualified plain call, and a `vec/`-qualified
call's own real, permanent `#error` exclusion. Full local suite (336 tests) + all 5 selfhost domains
+ real mingw cross-compile clean, zero regressions.

Real, measured progress via the same self-compile diagnostic: `selfhost/lexer.prn`'s own real error
count dropped from 57 to 46 (the `#error` count specifically: 35 → 24), a genuine, empirically-
confirmed step, not just a claim. Still doesn't compile — the remaining `#error`s are let-bindings
with non-symbol arguments or other unsupported shapes, plus the same real, separate,
not-yet-diagnosed `Region`/`I32`/`Lexer` undeclared-identifier class flagged above. Real, honest
next steps in this same expansion, not attempted here: literal/numeric arguments in a plain call,
`match`/`cond` as a real body form (not just a tail-position bare symbol), and diagnosing the
undeclared-identifier class.

**Correction, 2026-08-28 (founder real-time: "continue working on parena self hosted" / "removing
c ffi when possible")**: the "still doesn't compile" diagnostic above was real but incomplete --
it ran `parena build selfhost/lexer.prn` ALONE, never with its own real dependency,
`stdlib/string.prn`, in the same invocation. Every one of that run's real `Region`/`I32`/`Lexer`-
class undeclared identifiers, and every one of its 24 `#error`s, was an artifact of that missing
dependency, not a real, separate emitter gap. Confirmed directly: `./parena build stdlib/string.prn
selfhost/lexer.prn -o out.c` compiles with **zero errors and zero gcc warnings** under
`-std=c99 -Wall -Wextra -pedantic`, and always has -- `Makefile`'s own `test-selfhost-lexer` target
already invoked it exactly this way and already passed all 60 of its real assertions; this was
simply never reflected back into this doc's own prose.

Once that was understood, the REAL next milestone this doc itself already named -- "compiling the
selfhost pipeline's OWN five files together... duplicate struct definitions... never compiled
together before" -- was checked directly too: `./parena build stdlib/string.prn stdlib/array.prn
stdlib/io.prn selfhost/lexer.prn selfhost/parser.prn selfhost/region.prn selfhost/emit.prn
selfhost/main.prn -o out.c` (8 real files, all 5 selfhost domains plus their 3 real stdlib
dependencies -- `stdlib/array.prn` needed for `io/read-floats`'s own `NDArray` return type, a
real, separate, minor gap `Makefile`'s own `test-selfhost-main` comment already documents) compiles
to 2545 lines of C, **zero warnings**, Token/Node each defined exactly once despite living in both
`region.prn` and `emit.prn` (the emitter already de-dupes identical struct definitions across
files correctly -- the "duplicate struct" concern above never actually materializes as a problem).
`make test-selfhost-main` (the same pipeline, run through `build-file` against
`examples/valid_only.prn`, the real DoD acceptance file) already passes ALL PASS end to end --
this was ALREADY true before today, an existing, working, already-committed Makefile target this
doc's own prose just never caught up to reflecting.

**New work today: `build-files`, the real multi-file entry point** (`selfhost/main.prn`) --
`build-file` above is deliberately single-file only; this is the real, faithful port of
`src/main.c`'s own `cmd_build` MULTI-FILE loop (parse each path into its own Node, merge every
file's top-level children into one combined `NList`, region-analyze/emit/write exactly once),
matching `parena build a.prn b.prn -o out.c`'s own "multiple files combined into one compilation
unit" contract. `parse-file`/`merge-children!`/`build-files-step`/`build-files-parse-and-continue`
are the real, small supporting pieces -- the match-in-tail-position workaround
`handle-symbol-headed-call`'s own header comment already documents (region.prn) applies here too,
so the per-index loop body is split across two small functions rather than nesting a `match`
inside a `cond` branch. Verified via direct C-level instrumentation that the merge itself is
correct (two real files' children genuinely combined into one 2-element Vec) independent of
anything downstream, then verified end to end with a real, dedicated fixture (`examples/
selfhost_multifile_a.prn`/`_b.prn`, deliberately NOT the reference compiler's own `examples/
multifile_a.prn`/`_b.prn` -- see below for why) and a new test (`tests/
test_selfhost_main_multifile.c`, `make test-selfhost-main-multifile`): both files built together
via `build-files` produce real, gcc-clean C with a genuinely resolved cross-file function call;
the callee file built alone still honestly fails to compile (undeclared function) -- the same real
positive/negative pair `tests/integration/run_multifile_check.sh` already proves for the reference
compiler, reproduced here through the selfhost pipeline. ALL PASS, zero regressions across the
full local suite (336 tests) + every other selfhost domain's own test binary.

**Three real, separate `selfhost/emit.prn` gaps found while building that fixture, none fixed
here** (the reference C emitter, `src/emit.c`, handles all three correctly -- these are gaps in
the PARENA-language PORT specifically):
- `emit-program`/`region-analyze` only ever walk top-level `(defn ...)` forms -- a top-level
  `defstruct` is silently skipped entirely (confirmed: a `defstruct T` + a `defn` using `T` across
  two files compiles "successfully" but the struct never appears in the output, and the function
  using it emits garbage).
- `emit-form`'s own top-level dispatch (`selfhost/emit.prn`) only recognized `with-arena` and
  `let` as real defn-body shapes; everything else fell through to `emit-tail-symbol`, which ONLY
  correctly handles a bare symbol tail (`return <mangled-text>;`) -- a bare `alloc` call, a bare
  arithmetic expression, or a bare `match`/`cond` as a function's ENTIRE body all silently emitted
  wrong/empty C through this path (confirmed via direct instrumentation: `(+ n 10)` as an entire
  body emitted `return ;`, not `return (n + 10);`). **Fixed, 2026-08-28** (same day, continuing
  "removing c ffi when possible"): `emit-form` now also dispatches a bare `alloc` call or a real
  `plain-call-shaped?` function call (both already-existing let-binding-value emitters,
  `emit-alloc-call`/`emit-plain-call`, just never reached from tail position before) through a new
  shared `emit-tail-expr` wrapper. A bare arithmetic expression (`+`/`-`/etc, whose args include a
  non-symbol literal) and a bare `match`/`cond` as the entire body are still real, separate,
  unfixed gaps -- `plain-call-shaped?`'s own "every argument a bare symbol" requirement (see the
  next bullet) still excludes them, and `match`/`cond` still have no dispatch anywhere in
  `emit-form` at all. 2 new tests (`tests/test_selfhost_emit.c`, 15 total for domain 4), verified
  both via direct emitted-C-text assertions and via a real, live `build_files` round-trip; zero
  regressions across the full local suite (336 tests) + all 6 selfhost test binaries (the 5
  pre-existing ones plus `test-selfhost-main-multifile`) + `bazel build //...`.
- `emit-let-bindings`' own real, narrow let-value dispatch (its own header comment already admits
  "alloc call / plain function call / #error") further requires a plain call's every argument to
  be a bare symbol -- no nested calls, no literals. `(add-ten (add-ten n))` and `(+ n 10)` both
  fail this for that reason.

These three are why the multi-file fixture above is a dedicated, new one (a plain
`with-arena`+`let`+`alloc`-shaped callee, `examples/valid_only.prn`'s own already-proven real
shape, called via a plain-call-shaped, bare-symbol-argument let-binding) rather than the reference
compiler's own `examples/multifile_a.prn`/`_b.prn` (which uses a top-level `defstruct`, hitting
the first gap immediately). Real, honest, not-yet-scoped next steps for whoever continues this:
closing any one of these three would meaningfully widen what real `.prn` source the selfhost
emitter can correctly self-compile, independent of and complementary to `build-files` itself.

**Second bullet's "no literals" half closed too, same day (2026-08-28)**: `every-call-arg-symbol?`
(renamed `every-call-arg-symbol-or-number?`) now accepts a real number-literal argument, not just a
bare symbol -- `emit-call-arg` already handled the text correctly either way, only the shape guard
was too narrow. New `binary-op-symbol?`/`emit-binary-op`/`binary-op-call-shaped?` recognize
`+`/`-`/`*`/`/`/`=`/`>=`/`<=`/`>`/`<` as real C infix operators (not `mangled_name(args)` call
syntax -- `plain-call-shaped?` now explicitly excludes them) -- `(+ n 10)` is exactly the real,
motivating case. Wired into both `emit-let-value` and `emit-form`'s own tail-position dispatch.

Closing this surfaced a real, separate, THIRD gap, found and fixed the same day: an arithmetic
expression is a real, unboxed C `int`, but this emitter's own uniform convention declares
EVERY function `char *` and EVERY let-binding `char *` -- assigning or returning a raw `int`
into either is a real, confirmed `-Wint-conversion` -- a hard `-Werror` failure. New
`emit-i32-boxed` reinterprets the value via a real `(char *)(intptr_t)` cast (not a heap
allocation -- the C reference's own `int_box`-style per-file box-helper generation, `src/
emit.c`'s `g_box_helpers`, is a genuinely separate, larger undertaking) -- any real caller
unboxes with `(int)(intptr_t)result`. Verified as a genuine round-trip, not just "compiles
clean": a new driver (`tests/integration/driver_arith.c`) actually calls the compiled
`add-ten(5)` and asserts the real returned value is 15.

A FOURTH gap was found and fixed while chasing why `add-ten` (`] : I32`) still emitted THREE
return statements instead of one, even after the above: `emit-body-forms` always started its own
body walk at a hard-coded index 3, so a defn with an EXPLICIT `: ReturnType` annotation -- the
exact same `] : I32` shape `kind-code`/`is-close-token?`/etc (this very codebase's own real
source) all already use, or the even more common `] : String @ Region` -- had its own
colon/type(/at/region) children wrongly walked as 2-4 extra, bogus body statements, each
emitting its own spurious `return ...;` before the real one. New `body-start-index` detects and
skips the annotation (the type itself is still never used for anything, same "consumed by
omission" treatment an optional `@ region` already gets elsewhere in this file) -- fixes BOTH
the `] : I32` and `] : String @ Region` shapes, verified separately.

4 new tests total (`tests/test_selfhost_emit.c`, 19 for domain 4: parse+emit-text assertions for
the binary-op and return-annotation fixes, plus a real compile+run+assert-on-the-actual-value
check for the boxing round-trip). Zero regressions: full local suite (336 tests) + all 6 selfhost
test binaries + `bazel build //...` all clean.

Real, honest, still-open scope after all four: `plain-call-shaped?`/`every-call-arg-symbol-or-
number?` still reject a NESTED call as an argument (`(f (g x))`) -- only a bare symbol or number
literal is accepted. `binary-op-call-shaped?` is exactly 2-argument, no unary `-`, no `(+ a b c)`
chains. And `defstruct` is still not walked at the top level at all -- no `typedef struct`
emission, no `get-field`/struct-literal-construction support for code the selfhost emitter is
asked to compile (as opposed to code ABOUT structs living inside the selfhost `.prn` files
themselves, which the REFERENCE compiler already handles fine, unaffected by any of this).

**Real, TAIL-POSITION-ONLY `cond` support added the same day (2026-08-28)**, closing what was
the single largest remaining gap: `match`/`cond` had no dispatch anywhere in `emit-form` at all.
`handle-symbol-headed-call`'s own header comment (`region.prn`) already named the real,
architectural reason a NON-tail `match`/`cond` (a let-binding's own value, a plain sub-expression)
is genuinely hard -- their real value production is fundamentally STATEMENT-shaped (real if/else
blocks), which can't embed inside a single C expression/ternary without something like GCC's own
statement-expression extension, incompatible with this repo's `-std=c99 -pedantic` discipline. But
in TAIL position -- exactly where `emit-form` itself dispatches from -- a real if/else-if/else
chain is genuinely tractable: every clause's own result is already a complete STATEMENT (a
`return`, via `emit-form` itself, recursively) once wrapped, no expression-position value needed.
New `cond-call-shaped?`/`emit-cond`/`emit-cond-clauses`/`emit-cond-test`, wired into `emit-form`
only (never `emit-let-value` -- a `cond` as a let-binding value is exactly the hard, non-tail
case above, not attempted). Real, honest, narrow v0 scope: `match` itself (defenum tag dispatch,
needs a real tag registry this emitter doesn't have) is NOT attempted -- `cond` only. A test is
either a real `binary-op-call-shaped?` comparison, a bare symbol (a Bool-typed variable,
referenced truthily), or the literal `true`; `or`/`and`/`not` compound tests and a plain-call-
shaped? predicate call are real, separate, not attempted (falls back honestly, not a wrong
guess). The LAST clause MUST be `(true ...)` -- this codebase's own real, universal `cond`
convention already, and the only way this narrow v0 can guarantee the emitted chain is
exhaustive; a `cond` missing it is deliberately NOT treated as `cond-call-shaped?` at all, falling
back to the pre-existing honest fallback rather than risking a non-exhaustive if/else chain.

A FIFTH gap was found and fixed while verifying `cond`: a bare NUMBER LITERAL as a tail-position
result (any `cond` clause's own `10`, or any defn whose whole body is just a literal) fell
through to `emit-tail-symbol`, which emits the literal's raw text verbatim -- the identical
unboxed-`int`-into-`char*` problem `emit-i32-boxed` already exists to fix for binary-op results,
just reached via a different, previously-unexercised path. Boxed the same way, one new `emit-form`
clause.

4 more new tests (`tests/test_selfhost_emit.c`, 23 total for domain 4): structural assertions on
the generated if/else-if/else text, a real compile+run+assert-on-all-three-branches check (`tests/
integration/driver_cond.c`, since a `cond` that merely compiles isn't proof the real branches are
correct), and a real negative case (no trailing `(true ...)` -- confirmed NOT treated as
`cond-call-shaped?`, no `if` emitted at all). Zero regressions: full local suite (336 tests) + all
6 selfhost test binaries + `bazel build //...` all clean.

Real, honest, still-open scope after five: `match` itself (defenum tag dispatch); `cond` as a
non-tail value (the genuinely hard case above); nested calls as call arguments generally (shared
with the third gap above); `defstruct` still not walked at the top level.

**`or`/`and`/`not` compound `cond` tests added the same day (2026-08-28)**, closing one of the
two real exclusions the `cond` work above explicitly named. A real, RECURSIVE boolean-expression
sub-language (`bool-expr-supported?`/`emit-bool-expr`, replacing the narrower `cond-test-
supported?`/`emit-cond-test`) now also recognizes `or`/`and` (binary -- this codebase's own real
convention, confirmed via `is-close-token?`'s own real body, nests PAIRS rather than a variadic
N-ary call) and `not` (unary), each recursively composing further real comparisons or bool-expr
sub-expressions -- `(or (= n 1) (= n 3))` emits real `((n == 1) || (n == 3))`. Deliberately does
NOT generalize to arbitrary nested calls as arguments (the genuinely harder, still-open gap
above) -- a boolean/test-context expression sidesteps that entirely, since it never crosses this
file's own char*-boxing boundary at all, a real C `if`/`||`/`&&`/`!` just wants a raw int, same as
before. 2 new tests (`tests/test_selfhost_emit.c`, 25 total for domain 4): structural assertions
on the generated `||`/`&&`/`!` text, plus a real compile+run+assert-across-7-real-branches check
(`tests/integration/driver_bool_expr.c` -- both sides of the `or`, both sides of the `and`, the
`not`, and the final `true` fallback). Zero regressions: full local suite (336 tests) + all 6
selfhost test binaries + `bazel build //...` all clean.

Real, honest, still-open scope after six: `match` itself (defenum tag dispatch); `cond` as a
non-tail value (the genuinely hard architectural case); nested calls as call arguments generally;
`defstruct` still not walked at the top level.

**Plain-call-shaped predicate calls as a `cond` test added the same day (2026-08-28)**, closing
the second (and last) of the two real exclusions the `cond` work explicitly named. Turned out to
need NO new boxing/unboxing machinery at all: a Bool-returning function's own body already goes
through this file's real, existing convention on the CALLEE side (a comparison body boxes via
`emit-i32-boxed` in `emit-form`'s own `binary-op-call-shaped?` branch, so `0`/`1` becomes a real
`NULL`/non-`NULL` `char *`), and a real C `if (some_char_star_expr)` already treats a non-`NULL`
pointer as truthy -- exactly right, zero extra casting needed at the CALL site.
`bool-expr-supported?`/`emit-bool-expr` just gained one more `cond` clause each
(`plain-call-shaped?` -> `emit-plain-call`, reusing both unchanged), and the existing
`or-and-shaped?`/`not-shaped?` recursion means a predicate call also composes for free inside
`(and (is-foo? x) (is-bar? y))`-shaped tests, no extra work. 2 new tests (`tests/
test_selfhost_emit.c`, 27 total for domain 4): a structural assertion on the generated `if
(is_zero(n))` text, plus a real compile+run+assert check (`tests/integration/
driver_predicate_cond.c`) against a genuine two-function program (a real predicate defn plus a
real `cond` dispatching to it), both a zero and a non-zero input. Zero regressions: full local
suite (336 tests) + all 6 selfhost test binaries + `bazel build //...` all clean.

Real, honest, still-open scope after seven: `match` itself (defenum tag dispatch); `cond` as a
non-tail value (the genuinely hard architectural case); nested calls as call arguments generally
outside the boolean/test context above; `defstruct` still not walked at the top level at all.

**Top-level `defstruct` support added the same day (2026-08-28)**, closing the OTHER large
standalone gap this arc had been carrying alongside `cond`/`match`. Real, honest, narrow v0
scope: only SCALAR field types (I32, Bool, F64, String, Arena) -- a field typed as ANOTHER
registered struct, a `Vec`, or an enum is a real, separate, larger undertaking (needs the C
reference's own much larger real field-type/`Vec`-elem-hint tracking, `process_defstruct`'s own
real scope in `src/emit.c`), not attempted; a `defstruct` with any such field is simply not
treated as `defstruct-shaped?` at all (the pre-existing honest "skipped" fallback every
unsupported top-level form already gets, never a wrong guess). No struct-literal construction
(`{:field val}`) either -- this pass only lets an ALREADY-CONSTRUCTED struct value (a real param)
be READ via `get-field`, not built.

New `struct-prepass` (walks every top-level form once, in real source order, emitting each real
`defstruct-shaped?` form's own `typedef struct {...} Name;` and registering its name into a real
`known-structs` registry) runs before `emit-program`'s own existing defn pass, matching
`src/emit.c`'s own real "every defstruct before any defn, in source order" DoD behavior. `param-
c-type` (threaded a new `known-structs` parameter, along with `emit-params`/`emit-defn`) now
recognizes a registered struct name and passes it BY VALUE (`Point p`, not `Point *p`) --
confirmed directly against the reference compiler's own real, generated shape for the identical
source (`int get_x(Point);` / `int get_x(Point p) {...}`) before implementing, not guessed at.

New `get-field-shaped?`/`emit-get-field` (`(get-field p :x)` -> real `(p).x`, target itself a
bare symbol -- `get-field` on a NESTED expression is a real, separate, harder case, not
attempted) is wired into TWO real positions: `emit-call-arg` (widened alongside `every-call-arg-
symbol-or-number?`, so `get-field` now composes inside a comparison or plain-call argument too,
e.g. `(= (get-field p :x) 0)`) and `emit-form`'s own tail-position dispatch (a real struct
accessor function's own natural shape, boxed via `emit-i32-boxed` the same way binary-op results
already are). Call-arg position never needs boxing at all regardless of the field's real type
(a param already carries its own correctly-inferred C type) -- ONLY the tail-position case
assumes an I32/Bool-shaped field, a real, narrower, honestly-flagged sub-gap (a String/F64/Arena
field read in tail/let-value position would be WRONGLY boxed there, not attempted).

A real, separate, pre-existing gap was found and fixed while verifying: `mangle()` only ever
converted a hyphen, so a real `?`-suffixed predicate name (`is-zero-x?`, this codebase's own
extremely common Scheme/Lisp/Ruby-style Bool-naming convention) emitted as the literal, INVALID C
identifier `is_zero_x?` -- confirmed directly against the reference's own real `mangle()`
(`src/emit.c`), which also converts `/` and `!`, and STRIPS a leading `!` entirely (the mutation-
marker param sigil, `(!f : FileHandle @ :region/task)`). Widened to match exactly.

5 new tests (`tests/test_selfhost_emit.c`, 33 total for domain 4): structural assertions on the
generated typedef/param/get-field/mangled-name text, plus a real compile+run+assert check
(`tests/integration/driver_defstruct.c`) against real, constructed `Point` values calling both a
real accessor and a real `?`-suffixed predicate that uses `get-field` inside a comparison. Zero
regressions: full local suite (336 tests) + all 6 selfhost test binaries + `bazel build //...`
all clean.

**A real, separate gap found but NOT fixed, flagged honestly**: `and`/`or`/`not` at the TOP
LEVEL / tail position (a defn whose entire body is `(and ...)`, not wrapped in a `cond`'s own
test position) still isn't handled by `emit-form` at all -- `bool-expr-supported?`/`emit-bool-
expr` (S202-49) only ever get reached from `cond`'s own test dispatch. A real, separate,
tractable follow-up (mirroring `emit-i32-boxed`'s own real "box a raw C expression for a
char*-declared slot" pattern, just for a boolean expression instead of a binary-op one), not
attempted here.

Real, honest, still-open scope after eight: `match` itself (defenum tag dispatch); `cond` as a
non-tail value; nested calls as call arguments generally outside the boolean/test context;
`defstruct` fields typed as another struct/`Vec`/enum; struct-literal construction.

**Top-level `and`/`or`/`not` (tail position AND let-value) added the same day (2026-08-28)**,
closing the gap the defstruct work above flagged as found-but-not-fixed. `bool-expr-supported?`/
`emit-bool-expr` (S202-49's own real, recursive boolean sub-language) were previously only ever
reached from `cond`'s own test dispatch -- a defn whose ENTIRE body is `(and ...)` (not wrapped
in a `cond`), the natural, expected shape for a real predicate function like `is-origin?`, fell
through to the old silent empty `return ;`. Two new `emit-form` clauses (`or-and-shaped?`/
`not-shaped?`, boxed via `emit-i32-boxed` -- an `or`/`and`/`not` result is always a real, raw C
`int`, never a `String`, same reasoning `binary-op-call-shaped?`'s own comparison results already
use) close the tail-position half; a new shared `let-value-is-bool-expr?` (reused by both
`emit-let-value` and `let-value-error-prefix`, so they can't drift out of sync) closes the
symmetric let-binding-value half.

2 new tests (`tests/test_selfhost_emit.c`, 35 total for domain 4): structural assertions on the
generated boxed-boolean text for BOTH positions, plus a real compile+run+assert check (`tests/
integration/driver_bool_body.c`) combining both in one program -- `is-origin?`'s own real
top-level `and` body (itself composing `get-field` inside a comparison, proving the whole chain
of this session's own work fits together) and `is-boring`'s own `or` used as a LET-BINDING value
specifically (not tail position), verified across every real branch. Zero regressions: full local
suite (336 tests) + all 6 selfhost test binaries + `bazel build //...` all clean.

Real, honest, still-open scope after nine: `match` itself (defenum tag dispatch); `cond` as a
non-tail value (the genuinely hard architectural case); nested calls as call arguments generally
outside the boolean/test context; `defstruct` fields typed as another struct/`Vec`/enum;
struct-literal construction.

**Real, narrow, TAIL-POSITION-ONLY Result/Option `match` support added the same day (2026-08-28)**,
directly closing the first half of the `match` gap named above -- but deliberately NOT the general
form. Building a real user-defenum tag registry (tracking every defenum's own constructor -> tag-
number mapping across a whole build, arbitrary payload arity) is real, separate, harder work this
increment doesn't attempt. Instead it hardcodes the ONE real tag mapping essentially every real
`match` in this whole stdlib already relies on without ever declaring it itself -- Result's
`Ok`/`Err` and Option's `Some`/`None`, VS0's own two BUILTIN two-variant enums (confirmed live:
`runtime/parena_runtime.h`'s own `result_ok`/`result_err`/`option_some`/`option_none`, tag 1 =
Ok/Some, tag 0 = Err/None) -- covering the overwhelmingly dominant real match usage across this
whole stdlib (`json.prn`, `io.prn`, `shell.prn`, `textmate_loader.prn`, ... every one of them
Result/Option, never a user defenum, this whole session). Same real "no shared `result_var`, each
clause directly `return`s" simplification `cond`'s own v0 already uses -- but `match` adds one real
new wrinkle `cond` never had: the scrutinee's own value needs evaluating exactly once into a real C
local (`.tag` tested, `.value` read for the bound payload), so the whole `match` always wraps
itself in a fresh `{ ... }` C block, making a single fixed local name (`__match_scrutinee`) safe
even when matches nest, no gensym/counter infrastructure needed. Scrutinee: a bare symbol (an
already-bound Result/Option local) or a real `plain-call-shaped?` call. Exactly 2 clauses, patterns
either bare `None` (no payload, matching this stdlib's own real convention) or a 2-child list like
`(Ok x)`/`(Err e)`/`(Some s)` (payload bound to a real, block-scoped `void *` local), covering both
tags {0, 1} (never the same tag twice). Also added: `Result`/`Option` as real, recognized param
types in `param-c-type` (passed by value, same convention a registered `defstruct` param already
uses) -- needed to make a match's own scrutinee reachable via a real function signature at all.
Using the bound payload beyond a bare-symbol tail (e.g. `(get-field (deref x) :foo)`) needs real
`deref`-form emission this file doesn't have yet -- falls through to this file's own existing,
honest final `emit-form` fallback the same way any other unsupported clause-body shape already
does, not silently miscompiled.

9 new tests (`tests/test_selfhost_emit.c`, 44 total for domain 4): structural assertions on the
generated `Result __match_scrutinee = r;`/`Option __match_scrutinee = o;` locals (proving the
correct builtin C type is picked per scrutinee, not always `Result`), the real `if
(__match_scrutinee.tag == 1)` dispatch, and both payload bindings, plus a real compile+run+assert
check (`tests/integration/driver_match.c`) constructing real `Result`/`Option` values in C
(constructing one from PARENA itself -- `(Ok x)` as an expression, not a pattern -- is a real,
separate, not-yet-started gap) and calling the real compiled `describe-result`/`describe-option`
across every real tag. Zero regressions: full local suite (336 tests) + all selfhost domains
clean. `bazel build //...` itself could not be re-checked this round (the same real, pre-existing
`/home/treeiii/.cache` permission collision from a separate, concurrent session on this same
machine already documented elsewhere in this repo's own history -- not a regression from this
change) -- the Makefile-based suite above is the real, working verification path used instead.

Real, honest, still-open `match` scope after this: a general user-defenum tag registry (any
`match` over something other than Result/Option); `match` as a non-tail value; a clause body that
needs the payload beyond a bare-symbol tail; constructing an `Ok`/`Err`/`Some`/`None` value from
PARENA itself. `cond` as a non-tail value; nested calls as call arguments generally outside the
boolean/test context; `defstruct` fields typed as another struct/`Vec`/enum; struct-literal
construction remain open too, unchanged by this increment.

**Real, narrow Result/Option CONSTRUCTION support added the same day (2026-08-28)**, directly
closing the "constructing an `Ok`/`Err`/`Some`/`None` value from PARENA itself" gap named above --
the direct complement to match support, completing a real round trip (a function can now both
PRODUCE and CONSUME a Result/Option entirely within this emitter's own real domain, not just
consume one already hand-constructed in C the way `driver_match.c`'s own header comment explicitly
flagged as still needed). Mirrors `src/emit.c`'s own real `Ok`/`Err`/`Some` handling
(`result_ok(inner)`/`result_err(inner)`/`option_some(inner)`) and bare `None` -> `option_none()` --
but narrower: the reference emitter boxes a non-pointer payload into a real, per-type heap cell via
a generated `_box` helper (`ensure_box_helper`) whenever the payload's own C type isn't already
pointer-shaped; this file has no such helper-generation infrastructure yet, so the payload here
must ALREADY be pointer-shaped -- a bare symbol, a `get-field-shaped?` struct-field read, or a
`plain-call-shaped?` function call, the same 3 shapes this file's own uniform `char *` convention
already makes pointer-shaped with no boxing needed. An I32 payload is deliberately NOT supported:
reusing `emit-i32-boxed`'s own `(char *)(intptr_t)n` reinterpretation trick here would hand the
runtime's own real `result_ok`/`option_some` a bogus, non-dereferenceable "pointer" that a later
real `deref` would crash on -- a real, separate, harder gap (needs a genuine per-type heap box),
not attempted here.

Found and fixed a real, deeper architectural gap while building this (confirmed live via a genuine
gcc "incompatible types when returning type 'Result'" error): this file's own `emit-defn` hard-coded
EVERY function's own C return type as `char *`, unconditionally -- correct for the narrow v0 this
file has covered so far, but a real constructor function's own body (`(Ok s)`) produces an actual
`Result`/`Option` C STRUCT value, which cannot be `return`ed from a function declared `char *`. New
`defn-c-return-type`/`defn-declared-return-type-name` read a defn's own explicit `: ReturnType`
annotation and recognize `Result`/`Option` by name as the real, concrete C return type (the same by-
value recognition `param-c-type` already established for these two names, added for match's own
scrutinee param) -- its own return-position counterpart. Every other declared return type (including
none at all) keeps this file's own pre-existing, uniform `char *` default completely unchanged --
still real, honest, narrow v0, not a general type-inference pass.

11 new tests (`tests/test_selfhost_emit.c`): structural assertions on the real, concrete
`Result`/`Option` return types (including a zero-parameter `make-none`), the real
`result_ok`/`result_err`/`option_some`/`option_none` runtime calls, PLUS a direct regression guard
proving `I32`-declared functions (`round-trip-result`/`round-trip-option`) still get the pre-
existing `char *` default unchanged -- plus a real compile+run+assert check
(`tests/integration/driver_result_ctor.c`) that constructs EVERY real Result/Option value via real
selfhost-emitted `make-ok`/`make-err`/`make-some`/`make-none`, feeds each straight into real
selfhost-emitted `round-trip-result`/`round-trip-option` (itself built on the match support above),
and asserts both the tag AND the real payload survive the full round trip intact -- the first real
proof this whole self-hosting effort has that a value can be produced and consumed by generated code
on both ends, never hand-constructed in the driver. Zero regressions: full local suite (336 tests) +
all 6 selfhost domain test binaries + `test-json`/`test-yaml` + `bazel build`/`bazel test //...` all
clean.

Real, honest, still-open scope after this: a general user-defenum tag registry; `match`/`cond` as a
non-tail value; a match clause body that needs the payload beyond a bare-symbol tail; an I32 (or any
other non-pointer-shaped) Ok/Err/Some payload (needs a real per-type heap box, not attempted); nested
calls as call arguments generally outside the boolean/test context; `defstruct` fields typed as
another struct/`Vec`/enum; struct-literal construction (`{:field val}`) for a user-defined
`defstruct` (still real, separate, not attempted -- Result/Option's own construction above is a
narrower, hardcoded special case, not general struct-literal support).

**Real nested-call-as-a-call-argument support added the same day (2026-08-28)**, closing the
"nested calls as call arguments" gap this file's own header comment history had named but not fixed
since the very first binary-op/plain-call work — `(f (g x))`. `every-call-arg-symbol-or-number?`
(the one, shared shape-guard `binary-op-call-shaped?`/`plain-call-shaped?` both already delegate
their own argument checking to) now also accepts a nested argument that's itself
`plain-call-shaped?` or `binary-op-call-shaped?`, emitted through the SAME real
`emit-plain-call`/`emit-binary-op` this file already trusts for a whole function body — no new
expression-emitting machinery needed, only wiring the existing ones into a new position. This makes
`every-call-arg-symbol-or-number?`/`plain-call-shaped?`/`binary-op-call-shaped?` real, mutual
recursion (a nested call's own arguments get the identical real check, all the way down) — safe and
terminating since a real, parsed `Node` tree is always finite. `emit-call-arg` gained the matching
dispatch (get-field, then plain-call, then binary-op, then the bare-symbol/number fallback).
Real, honest, unchanged limitation, same as every other call-argument position this file already
has: no cross-function type-checking — a nested call's own real C type is whatever its callee
happens to be declared as (this file's own uniform `char *` default, or `Result`/`Option`/a
registered struct for the narrower cases `defn-c-return-type` now recognizes), the caller doesn't
verify it matches what the enclosing position expects, same as a bare-symbol argument already
doesn't either.

Found and fixed a real, direct consequence of this while testing: verifying the feature with two
`I32`-typed functions (an inner one, boxed via `emit-i32-boxed` the same way every I32-returning
function already is, called as one operand of an outer real `+`) surfaced that the resulting C is
genuine POINTER arithmetic (`char* + int`, since the inner call's own real declared C return type is
`char *`, not `int`) — confirmed live, by direct compile+run, that this still produces the
numerically correct result on every real input including a negative operand, because `char` has size
1: advancing a `char *` by N bytes is bit-for-bit identical to plain integer addition once round-
tripped back through `(int)(intptr_t)result`, the same real technique this whole file's
`emit-i32-boxed` convention already relies on everywhere else — not a new risk this feature
introduces, just a new position where the pre-existing convention gets exercised.

4 new tests (`tests/test_selfhost_emit.c`): the pre-existing crash-regression fixture (this file's
own very first plain-call-support test) had its own negative case moved from "a nested call as an
argument" — no longer unsupported — to "a nested `alloc` call as an argument" (still genuinely
unsupported: `alloc` needs its own real, distinguished Arena-destination argument, not composable as
an ordinary call argument the way a plain-call/binary-op result already safely is), preserving the
original crash-safety guard honestly. New positive coverage (structural + a real
compile+run+assert check, `tests/integration/driver_nested_call.c`) proves a real 2-function program
— the second calling the first with a nested call as one operand of a real binary-op — emits no
`#error`, nests the call INLINE in the generated C (not hoisted into a separate statement), compiles
clean, and computes the correct value on every real input tested. Zero regressions: full local suite
(336 tests) + all 6 selfhost domain test binaries + `bazel build`/`bazel test //...` all clean.

Real, honest, still-open scope after this: a general user-defenum tag registry; `match`/`cond` as a
non-tail value; a match clause body that needs the payload beyond a bare-symbol tail; an I32 (or any
other non-pointer-shaped) Ok/Err/Some payload; a nested `alloc` call as a call argument; `defstruct`
fields typed as another struct/`Vec`/enum; general struct-literal construction for a user-defined
`defstruct`.

**Real nested-`alloc`-call-as-a-call-argument support added the same day (2026-08-28)**, closing the
one real exclusion the earlier same-day nested-call-argument work explicitly named as still open
(`(f (alloc dest String "lit"))`). Turned out not to be a real obstacle: `alloc-call-shaped?`/
`emit-alloc-call` already take the exact same `scope`/`dest` every other expression emitter in this
file does, and the target Arena is always named EXPLICITLY in `alloc`'s own syntax -- nothing about
being called from a call-argument position instead of a let-value/tail position changes what Arena
it targets. `emit-alloc-call`'s own real return (`arena_strdup(...)`) is always pointer-shaped
(`char *`), so — like get-field/plain-call, unlike binary-op — there's no boxing ambiguity at this
position either. `every-call-arg-symbol-or-number?` gained a 4th accepted shape;
`emit-call-arg` gained the matching `alloc-call-shaped?` -> `emit-alloc-call` dispatch clause.

The crash-regression fixture that had already moved once this same day (from "nested call" to
"nested alloc call" as its own negative case) moved a SECOND time, to a genuinely different still-
unsupported shape: a raw STRING LITERAL as a call argument (`(f "lit")`) -- no shape guard in this
file accepts kind `NString` at all yet, a real, separate, not-yet-attempted gap. New positive
coverage (structural + a real compile+run+assert check, `tests/integration/driver_nested_alloc.c`)
proves a real 2-function program — the second passing a nested `alloc` call as its one argument to
the first — emits no `#error`, nests the call INLINE in the generated C, compiles clean, and
genuinely allocates the correct real string into the correct Arena at runtime. (Also confirmed, in
passing, a real, separate, pre-existing limitation this test's own fixture had to work around rather
than fix: `emit-program` emits no forward prototypes at all, so a callee must textually precede its
own caller for generated multi-function C to compile -- unrelated to this feature, not attempted
here.) Zero regressions: full local suite (336 tests) + all 6 selfhost domain test binaries + `bazel
build`/`bazel test //...` all clean.

Real, honest, still-open scope after this: a general user-defenum tag registry; `match`/`cond` as a
non-tail value; a match clause body that needs the payload beyond a bare-symbol tail; an I32 (or any
other non-pointer-shaped) Ok/Err/Some payload; a raw string literal as a call argument; no forward
prototypes emitted (a callee must textually precede its caller); `defstruct` fields typed as another
struct/`Vec`/enum; general struct-literal construction for a user-defined `defstruct`.

**Real string-literal-as-a-call-argument support added the same day (2026-08-28)**, closing the "no
literals" half of a gap this file's own header comments have carried since the very first binary-op/
plain-call work (number literals were closed same-day back then; strings stayed open until now):
`(f "lit")`. New `emit-string-literal` re-wraps the literal's own real, already-DECODED `:text` in C
double-quotes — the SAME real, honest, no-re-escaping convention `emit-alloc-call`'s own literal
argument already relies on (this file's own lexer decodes `\n`/`\t`/`\"`/`\\` into their real single-
byte form at LEX time, so a literal with a real embedded quote/backslash/newline byte isn't round-
tripped correctly here either — not a new limitation this widening introduces).

**Found and fixed a real, live, silently-wrong-C bug while verifying**: `(f (and b b))` — a real
boolean expression used as a call argument — was silently emitting `f(and(b, b))`, a genuine call to
a NEVER-DEFINED C function named `and`, instead of the correct real boolean logic or an honest
`#error`. Root cause: `plain-call-shaped?` never excluded `or`/`and`/`not` from its own name checks
the way `alloc` and every `binary-op-symbol?` name already are — harmless as long as plain-call-
shaped? was only ever reachable from the WHOLE-body/let-value dispatch (where `or-and-shaped?`/
`not-shaped?` are always checked FIRST), but the earlier same-day nested-call-as-a-call-argument
widening made plain-call-shaped? itself decide whether a NESTED argument is supported too — a
position `or-and-shaped?`/`not-shaped?` were never wired into at all, so this exact collision could
only fire from THERE, and only started being reachable once that widening landed. Fixed with a new
`bool-op-symbol?` exclusion, the same shape `alloc`'s own exclusion already established — `(and b b)`
as a call argument now correctly falls through to a clean `#error` (bool-expr composition still isn't
wired into `emit-call-arg` at all, a real, separate, not-yet-attempted gap) instead of silently wrong
C.

6 new tests (`tests/test_selfhost_emit.c`): the crash-regression fixture moved a THIRD time (string
literal -> a real `or`/`and` boolean expression, the fix above's own negative-test proof), plus new
positive coverage (structural + a real compile+run+assert check, `tests/integration/
driver_string_literal_arg.c`) proving a real 2-function program — the second passing a raw string
literal as an argument to the first — emits no `#error`, re-quotes the literal correctly, compiles
clean, and genuinely returns the correct string at runtime. Zero regressions: full local suite (336
tests) + all 6 selfhost domain test binaries + `bazel build`/`bazel test //...` all clean.

Real, honest, still-open scope after this: a general user-defenum tag registry; `match`/`cond` as a
non-tail value; a match clause body that needs the payload beyond a bare-symbol tail; an I32 (or any
other non-pointer-shaped) Ok/Err/Some payload; `or`/`and`/`not` as a call argument; no forward
prototypes emitted; `defstruct` fields typed as another struct/`Vec`/enum; general struct-literal
construction for a user-defined `defstruct`.

**Real `or`/`and`/`not`-as-a-call-argument support added the same day (2026-08-28)**, closing the
last real exclusion from that same day's still-open list — `(f (and a b))`. `emit-bool-expr` already
emits a plain, raw C boolean/int expression needing no boxing at all, the same real property
binary-op-call-shaped? arguments already rely on (the outer callee's own declared param type
governs). While building this, found and fixed THREE more real, live, silently-wrong-C bugs of the
exact same class the previous round's `bool-op-symbol?` fix already caught one instance of —
confirmed live via the identical direct-probe technique, not guessed:

- `(f (and b b))` itself was STILL only half-fixed: the earlier fix excluded `or`/`and`/`not` from
  `plain-call-shaped?`, but that was the ONLY thing standing between this exact shape and an honest
  `#error` — closing the gap the RIGHT way (real boolean composition) is this round's own real
  feature, not a second bug.
- `(f (Some s))` was silently emitting `f(Some(s))`, a call to a NEVER-DEFINED C function named
  `Some`, since `Ok`/`Err`/`Some` were ALSO never excluded from `plain-call-shaped?`'s own name
  checks. Fixed with a new `result-option-ctor-symbol?` exclusion, plus real, correct support: a
  `(Ok x)`/`(Err x)`/`(Some x)` construction argument now emits the real `result_ok`/`result_err`/
  `option_some` runtime call via `emit-result-option-ctor`.
- A bare `(f None)` was silently emitting `f(None)`, referencing a NEVER-DECLARED identifier
  `None`, since a bare `None` symbol already satisfied the generic bare-symbol-argument fallback
  with no special-casing at all (this one wasn't even a `plain-call-shaped?` exclusion gap — it's a
  different code path entirely). Fixed with a new `emit-call-arg` dispatch clause, checked BEFORE
  the generic fallback, emitting the real `option_none()`.
- `(f (cond (b 1) (true 2)))` was silently emitting THREE bogus calls at once —
  `f(cond(b(1), true(2)))` — since `cond` was ALSO never excluded, and a `cond` clause's own
  `(test result)` 2-element list shape happens to ALSO satisfy `plain-call-shaped?`'s own recursive
  argument check. Fixed with a new `other-special-form-symbol?` exclusion covering `cond`, `match`,
  `with-arena`, and `let` all at once — real defense in depth rather than relying on the
  (accidentally, today) structurally-safe shapes of the latter two holding forever as this file
  keeps changing. `cond`-as-an-argument itself stays honestly unsupported (real, by-design tail-
  position-only scope, documented elsewhere in this file) — this fix only stops it from being
  silently WRONG, not adds real support for it.

10 new tests (`tests/test_selfhost_emit.c`): the crash-regression fixture moved a FOURTH time (bool-
expr — no longer unsupported — to a real `cond`-as-argument, the negative proof for the 4th bug
above), plus 3 new real positive-coverage blocks (structural + real compile+run+assert checks,
`tests/integration/driver_bool_arg.c` and `driver_ctor_as_arg.c`) proving `or`/`and` composition and
both `Some`/`None` construction genuinely compute/round-trip correctly as call arguments, not just
compile clean. Zero regressions: full local suite (336 tests) + all 6 selfhost domain test binaries +
`bazel build`/`bazel test //...` all clean.

Real, honest, still-open scope after this: a general user-defenum tag registry; `match`/`cond` as a
non-tail value; a match clause body that needs the payload beyond a bare-symbol tail; an I32 (or any
other non-pointer-shaped) Ok/Err/Some payload; `cond`/`match` as a call argument (by design, real,
separate, harder gap — not attempted); no forward prototypes emitted; `defstruct` fields typed as
another struct/`Vec`/enum; general struct-literal construction for a user-defined `defstruct`.

**Real forward-prototype support added the same day (2026-08-28)**, closing the "no forward
prototypes emitted" gap named repeatedly this same day — several of this same day's own earlier test
fixtures had to reorder their own functions (callee textually before caller) specifically to work
around it, since this file used to emit NO prototypes at all. New `emit-defn-prototype` reuses the
EXACT SAME real signature-building pieces `emit-defn`'s own body already does
(`mangle`/`emit-params`/`defn-c-return-type`) — a prototype is just that same signature text followed
by `;` instead of a real `{ body }` block, no new type-resolution logic needed. `emit-program` now
runs a real prototype pre-pass (`emit-prototypes`) over every top-level defn BEFORE any function
bodies, matching the C reference emitter's own real, established convention
(`build_defn_prototype`) of emitting one unconditionally for EVERY defn — not just the ones a real
dependency analysis would say actually need one, the same "simpler and safer than tracking real
call-graph order" tradeoff this whole codebase already embraces elsewhere.

5 new tests (`tests/test_selfhost_emit.c`): structural checks confirming both a caller's own real
prototype AND its callee's own prototype are both emitted, PLUS a real compile+run+assert check
(`tests/integration/driver_forward_ref.c`) proving a real 2-function program written in
CALLER-BEFORE-CALLEE order (`make-buf`, which calls `wrap-buf`, defined FIRST — the EXACT ordering
that used to trigger a real gcc "implicit declaration of function" error) now compiles clean and
computes the correct value. Zero regressions: full local suite (336 tests) + all 6 selfhost domain
test binaries + `bazel build`/`bazel test //...` all clean (including the multi-file build-files
domain's own real negative case — `b.prn` alone still genuinely fails without `a.prn`, since
`emit-prototypes` only emits prototypes for defns that actually exist in the given program, confirming
this change doesn't accidentally paper over real cross-file resolution gaps).

Real, honest, still-open scope after this: a general user-defenum tag registry; `match`/`cond` as a
non-tail value; a match clause body that needs the payload beyond a bare-symbol tail; an I32 (or any
other non-pointer-shaped) Ok/Err/Some payload; `cond`/`match` as a call argument (by design, not
attempted); `defstruct` fields typed as another struct/`Vec`/enum; general struct-literal
construction for a user-defined `defstruct`.

**Real struct-typed struct field support added the same day (2026-08-28)**, closing one of the two
real gaps the defstruct section's own header comment named the moment defstruct support first
landed ("a field typed as ANOTHER registered struct ... a real, separate, larger undertaking ... not
attempted here"). `struct-field-c-type`/`struct-field-supported?`/`struct-field-shaped?`/
`all-struct-fields-shaped?`/`defstruct-shaped?`/`emit-struct-field`/`emit-struct-fields`/
`emit-defstruct` all threaded `known-structs` through (the same registry `param-c-type` already
established recognizing a registered struct name, reused here) — a struct-typed field is now
recognized and emitted with the real struct's own name as its C type, by value, matching this whole
file's own real, confirmed struct-param convention exactly. A struct-typed field's own struct must
be REGISTERED already (declared EARLIER in the file) — not an arbitrary scope choice deferred for
later, but the SAME real requirement plain C itself imposes on by-value struct nesting: a struct
containing another BY VALUE needs that other struct's own complete size already known (an incomplete
type can't be embedded by value), so two structs can never mutually reference each other by value in
real C either, forward-declared or not.

**Found and fixed a real, small formatting bug while verifying** (confirmed live, not guessed): the
new struct-lookup branch, copied from `param-c-type`'s own convention (`"Point "`, WITH a trailing
space, since `emit-params` never adds its own separator), produced a genuine double space
(`"Point  start;"`) once composed with `emit-struct-field`'s own separate, pre-existing explicit `" "`
push — harmless to gcc but a real, confirmed divergence from every other field's own real output.
Fixed by returning the bare struct name (no trailing space) here instead, matching
`struct-field-c-type`'s own sibling branches' convention exactly (they were never the ones with the
trailing space to begin with).

7 new tests (`tests/test_selfhost_emit.c`): structural checks confirming the correctly-typed,
correctly-spaced field declaration and the real, unboxed struct-as-call-argument composition, plus a
real compile+run+assert check (`tests/integration/driver_struct_field_type.c`) proving a real,
genuinely nested `Point`-inside-`Line` struct read produces the correct value at runtime through a
real by-value struct composition (deliberately exercised via the CALL-ARGUMENT position, not tail
position — a struct-typed field used AS a whole function's own tail-position body would hit a real,
separate, already-documented, pre-existing gap: `get-field`'s own tail-position dispatch always boxes
via `emit-i32-boxed`, which only works for a scalar I32/Bool field, not a struct VALUE — not attempted
or exercised here, not a regression from this change). Zero regressions: full local suite (336 tests)
+ all 6 selfhost domain test binaries + `bazel build`/`bazel test //...` all clean.

Real, honest, still-open scope after this: a general user-defenum tag registry; `match`/`cond` as a
non-tail value; a match clause body that needs the payload beyond a bare-symbol tail; an I32 (or any
other non-pointer-shaped) Ok/Err/Some payload; `cond`/`match` as a call argument (by design, not
attempted); a struct-typed field used as a tail-position return (needs real per-shape unboxing
beyond emit-i32-boxed's own scalar-only trick); `Vec`/enum-typed `defstruct` fields; general
struct-literal construction for a user-defined `defstruct`.

**Real nested/chained `get-field` support added the same day (2026-08-28)**, closing the "get-field
on a NESTED expression" gap that function's own header comment named the moment get-field support
first landed — real friction found firsthand while verifying struct-typed struct field support the
same day (a genuinely natural shape once a struct can itself CONTAIN another struct, e.g.
`line.start.x`). `(get-field (get-field l :start) :x)` now real, tractable, mutual recursion between
`get-field-shaped?`/`emit-get-field` (safe and terminating since a real, parsed `Node` tree is always
finite) — `get-field-shaped?`'s own target check widened to accept EITHER a bare symbol OR itself a
nested `get-field-shaped?` node; `emit-get-field`'s own target resolution widened to recurse into
`emit-get-field` when the target itself is get-field-shaped, composing `((l).start).x`. Real, honest,
narrower-than-general scope stays: a chain of get-field calls all the way down to a bare-symbol root
only — `(get-field (some-call x) :f)` (a non-get-field NESTED call as the target) is still a real,
separate, harder case, not attempted here.

3 new tests (`tests/test_selfhost_emit.c`): structural checks confirming no `#error` and the correct,
doubly-nested `((l).start).x` C expression (boxed via `emit-i32-boxed`, since the innermost field is
I32-typed — the SAME pre-existing, documented boxing scope, unchanged by this widening), plus a real
compile+run+assert check (`tests/integration/driver_nested_get_field.c`) proving the real chained
field read genuinely computes the correct value on multiple real inputs, not just that gcc accepts
the nested expression text. Zero regressions: full local suite (336 tests) + all 6 selfhost domain
test binaries + `bazel build`/`bazel test //...` all clean.

Real, honest, still-open scope after this: a general user-defenum tag registry; `match`/`cond` as a
non-tail value; a match clause body that needs the payload beyond a bare-symbol tail; an I32 (or any
other non-pointer-shaped) Ok/Err/Some payload; `cond`/`match` as a call argument (by design, not
attempted); a struct-typed field used as a tail-position return; a non-get-field nested call as a
get-field target; `Vec`/enum-typed `defstruct` fields; general struct-literal construction for a
user-defined `defstruct`.

**Real struct-return-type support PLUS a non-get-field nested call as a get-field target, added
together the same day (2026-08-28)**, closing two of the still-open gaps named above at once — they
turned out to be naturally paired: a function returning a registered struct BY VALUE is the real,
motivating reason a get-field target ever needs to accept a plain function call in the first place.
`defn-c-return-type` widened (now taking `known-structs`/`dest`, both already available at its own 2
real call sites, `emit-defn`/`emit-defn-prototype`) to recognize a declared return type naming an
ALREADY-REGISTERED struct, the same by-value convention `param-c-type`/`struct-field-c-type` already
established for these two positions — needed for the same real reason `Result`/`Option` needed it: a
function whose whole body is a bare struct-typed param passed straight through
(`(defn identity-point [(p : Point)] : Point p)`) produces a real `Point` C struct value via
`emit-tail-symbol`'s own unboxed `return p;`, correct ONLY when the function's own declared C return
type is ALSO `Point`, not the pre-existing `char *` default. `get-field-shaped?`/`emit-get-field`
widened again (this function's own THIRD real widening the same day) to accept a `plain-call-shaped?`
target too, alongside the bare-symbol/nested-get-field targets from earlier the same day — emits via
the SAME real `emit-plain-call` this file already trusts everywhere else, composing
`(identity_point(p)).x`. Makes `get-field-shaped?` real, terminating mutual recursion with
`plain-call-shaped?`/`every-call-arg-symbol-or-number?` (which itself already calls
`get-field-shaped?` on its own call arguments) — safe since every recursive call here strictly
descends into a smaller child subtree of a real, finite parsed `Node` tree.

7 new tests (`tests/test_selfhost_emit.c`): structural checks confirming the real, concrete `Point`
return type, the correct unboxed `return p;`, and the real `(identity_point(p)).x` composition, plus
a real compile+run+assert check (`tests/integration/driver_struct_return_type.c`) proving a real
struct value genuinely round-trips through a real by-value function return and a real get-field read
on multiple real inputs including a negative value, not just that gcc accepts the generated text.
Zero regressions: full local suite (336 tests) + all 6 selfhost domain test binaries + `bazel
build`/`bazel test //...` all clean.

Real, honest, still-open scope after this: a general user-defenum tag registry; `match`/`cond` as a
non-tail value; a match clause body that needs the payload beyond a bare-symbol tail; an I32 (or any
other non-pointer-shaped) Ok/Err/Some payload; `cond`/`match` as a call argument (by design, not
attempted); a struct-typed field used as a tail-position return; `Vec`/enum-typed `defstruct` fields;
general struct-literal construction for a user-defined `defstruct`.

**Real struct-typed-field-as-a-tail-position-return support added the same day (2026-08-28)**,
closing the ONE gap struct-return-type support's own landing (right above) explicitly left open:
`emit-form`'s own `get-field-shaped?` dispatch ALWAYS boxes its result via `emit-i32-boxed`, correct
ONLY for a scalar (I32/Bool) field — boxing a real STRUCT VALUE the same way isn't even valid C (a
struct isn't a scalar/pointer type, can't be cast through `intptr_t`). Real, honest, narrow fix:
rather than threading struct-type-awareness through `emit-form`'s own general recursion (used from
many contexts — `let`/`with-arena` bodies, `match`/`cond` clauses — a genuinely bigger, riskier
undertaking), new `struct-returning-get-field-body?` checks ONLY the one case where the needed
information is already naturally at hand: a defn whose OWN declared return type is an already-
registered struct (`defn-c-return-type`'s own real output already answers this) AND whose ENTIRE
body is exactly one `get-field-shaped?` form. In that one case, `emit-defn` bypasses the general
`emit-body-forms`/`emit-form` path entirely, emitting a real, unboxed `return (target).field;`
directly via `emit-get-field` — correct because the field's own real value, by construction, has the
exact same real C type the function's own return type already promises. Every other case (a struct-
typed field nested inside a `let`/`with-arena`/`cond`/`match` body, a SCALAR-field get-field tail, or
a non-get-field body) keeps this file's own pre-existing behavior completely unchanged.

7 new tests (`tests/test_selfhost_emit.c`): structural checks confirming the real, unboxed
`return (l).start;` (and confirming the OLD, wrong boxed form is genuinely absent, not just that the
right one happens to also be present), plus a real compile+run+assert check (`tests/integration/
driver_struct_field_tail.c`) proving the real struct value returned this way carries BOTH real fields
correctly on multiple real inputs including negative values, not just that gcc accepts the direct
struct return — PLUS a direct regression guard proving the pre-existing SCALAR-field case
(`point-x`) still emits the exact same boxed `(char *)(intptr_t)(p).x` as before, confirming
`struct-returning-get-field-body?`'s own new special case never fires for a non-struct-returning
function. Zero regressions: full local suite (336 tests) + all 6 selfhost domain test binaries +
`bazel build`/`bazel test //...` all clean.

Real, honest, still-open scope after this: a general user-defenum tag registry; `match`/`cond` as a
non-tail value; a match clause body that needs the payload beyond a bare-symbol tail; an I32 (or any
other non-pointer-shaped) Ok/Err/Some payload; `cond`/`match` as a call argument (by design, not
attempted); a struct-typed field nested inside a `let`/`with-arena`/`cond`/`match` body (this
round's own fix only covers a defn's ENTIRE body being one bare get-field); `Vec`/enum-typed
`defstruct` fields; general struct-literal construction for a user-defined `defstruct`.

**Real get-field-as-a-let-value support added the same day (2026-08-28)**, closing a real, honest,
until-now-unsupported gap confirmed live via a direct probe: `(let [n (get-field p :x)] n)` fell
straight to the honest `#error` fallback, `get-field-shaped?` never having been wired into
`emit-let-value` at ALL (not even for a plain SCALAR field, unrelated to any struct-typing question).
Real, honest, narrow v0: SCALAR-only, boxed via `emit-i32-boxed` — the SAME assumption `emit-form`'s
own tail-position dispatch already made (and still correctly makes for a scalar field, even after
this same day's earlier struct-return-type work narrowly carved out just the whole-defn-body case). A
STRUCT-typed field as a let-value would be WRONGLY boxed here the exact same way a struct-typed field
used to be wrongly boxed in tail position before that fix — but unlike a defn's own body, a
let-binding carries no declared return-type annotation to special-case against, so there's no
equivalent narrow fix available here without real field-type lookup infrastructure this file doesn't
have yet; not attempted.

6 new tests (`tests/test_selfhost_emit.c`): structural checks confirming no `#error` and the correct,
boxed `(char *)(intptr_t)(p).x` let-binding, plus a real compile+run+assert check (`tests/
integration/driver_let_get_field.c`) proving the real scalar struct-field read, passed through a real
let-binding, genuinely computes the correct value on multiple real inputs including a negative one.
Zero regressions: full local suite (336 tests) + all 6 selfhost domain test binaries + `bazel
build`/`bazel test //...` all clean.

Real, honest, still-open scope after this: a general user-defenum tag registry; `match`/`cond` as a
non-tail value; a match clause body that needs the payload beyond a bare-symbol tail; an I32 (or any
other non-pointer-shaped) Ok/Err/Some payload; `cond`/`match` as a call argument (by design, not
attempted); a STRUCT-typed field used as a let-value (needs real field-type lookup infrastructure);
a struct-typed field nested inside a `with-arena`/`cond`/`match` body; `Vec`/enum-typed `defstruct`
fields; general struct-literal construction for a user-defined `defstruct`.

**Real `deref` support added the same day (2026-08-28)**, closing the "a clause body that needs the
payload beyond a bare-symbol tail" half of the match gap this file's own match-support section
explicitly named as still open — `(deref x)` reads back a real, boxed I32 value out of a match-bound
`void *` payload local, emitting `*((int *)(x))`, boxed via `emit-i32-boxed` when crossing this file's
own uniform char* boundary. Real, honest, narrow v0: the target must be a bare symbol (the ONLY real
shape a match-bound payload name ever is); constructing a REAL boxed I32 payload (`(Some 42)`, boxed
via a real per-type heap-box helper) is the natural complement but a real, separate, harder gap this
increment does NOT attempt (needs new box-helper-generation infrastructure this file doesn't have
yet) — verified here against a value boxed directly in real C by the test driver instead, the same
real technique `driver_match.c`'s own original (pre-real-construction) version already used.

**Found and fixed a real, live, silently-wrong-C bug while verifying** (confirmed via a direct probe,
not guessed, the same technique that has now caught this exact class of bug SIX times this same day):
`deref` was ALSO never excluded from `plain-call-shaped?`'s own name checks — `(defn f [(x : I32)]
(deref x))` was silently emitting `deref(x)`, a call to a NEVER-DEFINED C function, before
`deref-shaped?` existed to intercept it in `emit-form`'s own dispatch (checked BEFORE
`plain-call-shaped?`). Fixed by adding `deref` to `other-special-form-symbol?`'s own exclusion list
too, the same real defense-in-depth already covering `cond`/`match`/`with-arena`/`let`/`get-field` —
a call-argument/let-value position (where deref support is NOT attempted this round) would otherwise
still be able to hit this exact bug.

6 new tests (`tests/test_selfhost_emit.c`): structural checks confirming no `#error` and the real,
correctly boxed `*((int *)(x))` dereference inside a real match clause, plus a real
compile+run+assert check (`tests/integration/driver_deref.c`) proving the real dereference genuinely
reads back the correct value from a real, boxed I32 payload (constructed directly in C) on multiple
real inputs including a negative one, not just that gcc accepts the generated text. Zero regressions:
full local suite (336 tests) + all 6 selfhost domain test binaries + `bazel build`/`bazel test //...`
all clean.

Real, honest, still-open scope after this: a general user-defenum tag registry; `match`/`cond` as a
non-tail value; a real, non-pointer-shaped (I32) Ok/Err/Some payload CONSTRUCTED by this emitter
itself (needs a real per-type heap-box helper); `cond`/`match` as a call argument (by design, not
attempted); a STRUCT-typed field used as a let-value; a struct-typed field nested inside a
`with-arena`/`cond`/`match` body; `Vec`/enum-typed `defstruct` fields; general struct-literal
construction for a user-defined `defstruct`.

**Real non-pointer-shaped (I32) Ok/Err/Some payload CONSTRUCTION support added the same day
(2026-08-28)**, closing the gap named directly above — the natural complement to the same day's
earlier real `deref` support, together completing the FULL real round trip: a value can now be both
PRODUCED and CONSUMED, scalar payload included, entirely by real selfhost-emitted C on both ends.

New `int-box-helper-decl` emits a genuine, minimal, narrow port of the ONE real case src/emit.c's own
much larger, general `ensure_box_helper`/`g_box_helpers` mechanism exists for — a single, hardcoded
`int_box(Arena *dest, int v) -> int *` helper, matching the reference's own real naming convention
exactly (genuinely different from `parena_runtime.h`'s own, unrelated `vec_box_i32`, which boxes into
a `Vec`'s own stored arena for a different real purpose). Real, deliberate simplification over the
reference's own on-demand, per-distinct-type, deduplicated generation: since this narrow v0 only ever
needs ONE type, `int_box` is emitted UNCONDITIONALLY, every program, matching this file's own already-
established "simple over optimized" tradeoff (e.g. always-emit-every-prototype) — no generation-
tracking/deduplication infrastructure needed. `static inline` means an unused `int_box` in a program
that never constructs an I32 payload produces no real "unused function" warning under `-Wall -Wextra`
(confirmed directly).

New `result-option-payload-is-i32?` recognizes a bare NUMBER literal or a real
`binary-op-call-shaped?` expression as I32-shaped (the same 2 shapes `emit-i32-boxed`'s own existing
callers already recognize as "definitely produces a raw int", reused here for boxing into a REAL heap
cell instead — the two conventions are genuinely different and must never be confused). New
`arena-scope-any-ref` finds the destination Arena to box into: the most-recently-pushed arena binding
currently in scope (the same "most recent wins" convention `arena-scope-lookup`'s own reverse
iteration already establishes) — a real, honest, self-documenting invalid-identifier fallback for the
genuinely degenerate no-arena-in-scope case (never actually hit in this whole codebase's own real
test snippets), matching the same real, already-accepted "we don't verify everything, but we never
emit text that silently compiles into WRONG behavior" class of limitation `plain-call-shaped?`'s own
unverified callee names already establish.

An I32-typed bare-symbol payload (e.g. `(Some n)` where `n` is a plain I32 local) is deliberately NOT
included — this narrow emitter has no real per-symbol type tracking, so a bare-symbol payload is
already claimed by the pointer-shaped branch (correct for the dominant real case, a String/struct
value); a real, separate, not-yet-attempted gap.

7 new tests (`tests/test_selfhost_emit.c`): structural checks confirming no `#error`, the real,
unconditionally-present `int_box` helper, the real `Option` return type, and the real, correctly
composed `option_some(int_box(dest, 42))`, plus a real compile+run+assert check (`tests/integration/
driver_i32_roundtrip.c`) proving a real I32 value genuinely round-trips through a real heap box, a
real Option construction, and a real match+deref consumption — construction AND consumption BOTH
entirely selfhost-emitted, never hand-constructed or hand-dereferenced in the driver, not just that
gcc accepts the generated text. Zero regressions: full local suite (336 tests) + all 6 selfhost
domain test binaries + `bazel build`/`bazel test //...` all clean.

Real, honest, still-open scope after this: a general user-defenum tag registry; `match`/`cond` as a
non-tail value; an I32-typed BARE-SYMBOL Ok/Err/Some payload; `cond`/`match` as a call argument (by
design, not attempted); a STRUCT-typed field used as a let-value; a struct-typed field nested inside
a `with-arena`/`cond`/`match` body; `Vec`/enum-typed `defstruct` fields; general struct-literal
construction for a user-defined `defstruct`.

**Real struct-LITERAL construction support added the same day (2026-08-28)**, closing "general
struct-literal construction for a user-defined defstruct" — the other of the two real gaps the
defstruct section's own header comment named since defstruct support first landed (struct-typed
struct FIELDS, the first, closed earlier the same day). Mirrors `src/emit.c`'s own real map-literal
handling (structural field-NAME-SET matching against every registered defstruct, no real type-
context threading) but scoped MUCH narrower: rather than supporting a struct literal in ANY
expression position (which would need `known-struct-nodes` threaded through this whole file's own
general `emit-form`/`emit-call-arg`/`emit-let-value` recursive machinery — a genuinely much bigger
undertaking, the same class of "invasive general threading" already declined for struct-typed tail-
position/let-value support), this checks ONLY the one case where the needed registry is already
naturally at hand without threading it anywhere new: a defn whose OWN declared return type is an
already-registered struct AND whose ENTIRE body is exactly one map-literal matching that struct's own
real field set exactly — `emit-defn` special-cases this the SAME way it already special-cases
`struct-returning-get-field-body?`.

Real, deliberate emission choice: a C99 compound literal (`(Point){x, y}`, positional, matching the
struct's own DECLARED field order) rather than the reference compiler's own real
`Point_new(x, y)` positional-constructor-FUNCTION convention — a real, valid, standard C99
construct (not a GNU extension, compiles clean under `-pedantic`), avoiding the need to ALSO
generate a whole new constructor function per struct.

`struct-prepass` widened to build a SECOND, parallel registry (`known-struct-nodes : Vec Node`,
alongside the pre-existing `known-structs : Vec String`) holding each registered struct's own RAW
defstruct AST node, threaded through `emit-program`/`emit-defn` (a small, 3-function-touching
change, NOT the whole-file threading declined above, since both are called directly from one place
each). **Found and fixed a real, live type-mismatch, confirmed via a direct probe (not guessed),
while building this**: `Option Node`/deref-based lookups repeatedly failed real gcc compiles — the
reference compiler's own real `deref` needs a concrete, DECLARED variable type to hook its own type
inference into, which a bare `Option Node` match clause's own value position doesn't reliably
provide (defaulted to a bogus generic `void *` in one spot). Redesigned around plain I32 INDEXES
(-1 for "not found") into the parallel registries instead — every real caller looks the actual node
up directly via `vec/get`, never needing `Option`-of-struct boxing/deref at all. A second real,
confirmed-live gap found the same way: a struct literal that DOESN'T match any registered struct
(wrong return type, or a field-set mismatch) used to fall through `emit-form`'s own final catch-all
fallback into a genuine `return ;` on a non-void function — still an honest COMPILE FAILURE (never
silently WRONG C, `-Werror=return-type` catches it), but with a generic gcc message rather than this
file's own established, real `#error`-PREPROCESSOR-directive convention every other unsupported
shape already gets (`let-value-error-prefix`'s own precedent) — fixed with a new, dedicated
`map-literal-shaped?` clause in `emit-form` emitting a real, named `#error` instead.

7 new tests (`tests/test_selfhost_emit.c`): structural checks confirming no `#error` for the real
case and the correct, REORDERED `(Point){x, y}` compound literal (the source map literal's own keys
deliberately written `{:y y :x x}`, y first — proving real field-order resolution, not a lucky
positional match), plus a real compile+run+assert check (`tests/integration/
driver_struct_literal.c`) proving the real construction produces the correct struct value on
multiple real inputs including negative ones, PLUS a direct negative test proving a field-mismatched
literal now gets the real, honest, NAMED `#error` instead of the old generic empty-return fallback.
Zero regressions: full local suite (336 tests) + all 6 selfhost domain test binaries + `bazel
build`/`bazel test //...` all clean.

Real, honest, still-open scope after this: a general user-defenum tag registry; `match`/`cond` as a
non-tail value; an I32-typed BARE-SYMBOL Ok/Err/Some payload; `cond`/`match` as a call argument (by
design, not attempted); a STRUCT-typed field used as a let-value; a struct-typed field nested inside
a `with-arena`/`cond`/`match` body; `Vec`/enum-typed `defstruct` fields; a struct literal in ANY
position other than a defn's own entire body (a let-value, a call argument, nested inside
`with-arena`/`cond`/`match`); a struct-literal field itself holding a freshly-constructed
Result/Option value.

**Real I32-typed BARE-SYMBOL Ok/Err/Some payload support added the same day (2026-08-28)**, closing
the gap `result-option-payload-is-i32?`'s own header comment named explicitly as not-yet-attempted:
`(defn wrap [(n : I32) (dest : Arena @ Region)] : Option (Some n))`. Real, confirmed-live problem
found via a direct probe: a bare symbol payload ALREADY satisfied `result-option-payload-supported?`'s
own existing "pointer-shaped" branch unconditionally (kind `NSymbol`, 3) — correct for the dominant
real case (a String/struct value already char*-shaped) but WRONG for an I32-typed one, silently
emitting `option_some(n)` with `n` a raw C `int` passed where the runtime's own real `void *value` is
expected — a genuine `-Werror=int-conversion` compile failure (an HONEST failure, not a silent
miscompile, but a real, fixable gap all the same).

Real, narrow fix, the SAME "special-case only where the needed information is already naturally at
hand" strategy struct-return-type/struct-literal support already established: rather than threading
real per-symbol type tracking through this whole file's own general `emit-form`/`emit-call-arg`/
`emit-let-value` recursive machinery, this checks ONLY the one case where a bare symbol's own real
type IS already knowable without any new tracking: it names one of the ENCLOSING DEFN's OWN real,
declared params, and that param's own declared type is I32. `emit-defn-body` special-cases this the
SAME way it already special-cases `struct-returning-get-field-body?`/struct-literal construction —
checked and dispatched BEFORE the general path (which would otherwise still hit the exact same real
bug this closes). New `emit-defn-i32-param-ctor` boxes the real, MANGLED param reference (via
`resolve-arena-ref`, correct for a bare symbol — deliberately NOT `emit-int-box-call`'s own bare
`:text` fallback, which is only ever correct for a number literal) into the SAME real `int_box` helper
every other I32 payload construction already uses.

6 new tests (`tests/test_selfhost_emit.c`): structural checks confirming no `#error` and the real,
correctly composed `option_some(int_box(dest, n))`/`result_err(int_box(dest, n))` (proving this
closes the gap for `Err`/`Result` too, not just `Some`/`Option`), plus a real compile+run+assert
check (`tests/integration/driver_i32_param_ctor.c`) proving the real I32 param genuinely round-trips
through a real heap box, a real bare-symbol Option construction, and a real match+deref consumption
on multiple real inputs including a negative one — PLUS a direct regression guard proving a
String-typed bare-symbol payload (the dominant real case the pre-existing pointer-shaped branch
already exists for) still emits the exact same unboxed `option_some(s)` as before, confirming this
new I32-specific special case never misfires for a non-I32 payload. Zero regressions: full local
suite (336 tests) + all 6 selfhost domain test binaries + `bazel build`/`bazel test //...` all clean.

Real, honest, still-open scope after this: a general user-defenum tag registry; `match`/`cond` as a
non-tail value; `cond`/`match` as a call argument (by design, not attempted); a STRUCT-typed field
used as a let-value; a struct-typed field nested inside a `with-arena`/`cond`/`match` body;
`Vec`/enum-typed `defstruct` fields; a struct literal in ANY position other than a defn's own entire
body; a struct-literal field itself holding a freshly-constructed Result/Option value; an I32-typed
bare-symbol Ok/Err/Some payload referencing a `let`-bound local rather than a defn's own PARAM
(this round's own fix only covers the param case, the one real, knowable-without-tracking shape).

**Real "struct-literal field holding a freshly-constructed Result/Option value" support added the
same day (2026-08-28)**, closing the gap named directly above — a genuinely FREE widening, not new
work: `struct-literal-field-value-supported?` widened to accept `result-option-ctor-shaped?`/
`none-shaped?`, and `emit-call-arg` (already used to emit every struct-literal field's own value)
already dispatched both correctly (Ok/Err/Some/None-as-a-call-argument support landed earlier the
same day) — the only real change needed anywhere was the shape GUARD.

**Found and fixed a real, live, PRE-EXISTING bug while verifying this** (confirmed via a direct
probe, not guessed): a defstruct field WITH an explicit `@ Region` suffix (`(label : String @
Region)`, the exact real shape String fields typically use throughout this codebase) has 5 real
children, not 3 — `struct-field-shaped?`'s own OLD exact-3-children check silently rejected it,
an honest "skipped, not registered" failure (never a wrong guess), but never exercised by any PRIOR
struct-field test in this file (every one used a scalar field with no region annotation). Widened
the check from exact-3 to at-least-3 — the type name is always at index 2 regardless of whether a
region annotation follows, the SAME real convention `param-type-name` already established for
params. Also widened `struct-field-c-type` to recognize `Result`/`Option` as real, by-value struct
FIELD types (the other real half needed: the constructed value has to have somewhere correctly-
typed to land).

7 new tests (`tests/test_selfhost_emit.c`): structural checks confirming both the region-annotated
field AND the Option field are now emitted correctly, plus a real compile+run+assert check
(`tests/integration/driver_struct_literal_option_field.c`) proving a real struct literal genuinely
constructs a struct whose String field AND whose freshly-constructed Option field both hold the
correct values, not just that gcc accepts the generated text. Zero regressions: full local suite
(336 tests) + all 6 selfhost domain test binaries + `bazel build`/`bazel test //...` all clean.

Real, honest, still-open scope after this: a general user-defenum tag registry; `match`/`cond` as a
non-tail value; `cond`/`match` as a call argument (by design, not attempted); a STRUCT-typed field
used as a let-value; a struct-typed field nested inside a `with-arena`/`cond`/`match` body;
`Vec`/enum-typed `defstruct` fields; a struct literal in ANY position other than a defn's own entire
body; an I32-typed bare-symbol Ok/Err/Some payload referencing a `let`-bound local.

**Build system, decided now for when this milestone starts**: founder, real-time: "also when we
write PARENA in PARENA we want it to all be BAZEL powered." Consistent with `parena-c` itself
already building on Bazel (`.bazelrc`/`MODULE.bazel`/per-directory `BUILD.bazel`, CI's own primary
path) — the self-hosted `parena` compiler, once it exists, keeps that same build system rather
than introducing a second one. Not a new decision so much as confirming the existing one carries
forward; no new work implied today.

**JIT compilation — a real, much further-out idea, not scoped**: founder, real-time: "we can do
some fancy stuff like v8 with our compiler i bet to make it groovy." A genuinely real compiler-
architecture direction (V8's own JIT — parse, run once in a fast baseline interpreter, profile,
recompile hot functions with an optimizing tier) — but one that presupposes VS0's own domains 2-5
(region analyzer, C emitter, full build pipeline) exist first, since a JIT is itself a compiler
backend sitting where the C emitter sits today, several stages beyond where VS0 currently is
(domain 1 of 5). Flagged as a real idea worth remembering, not designed or sequenced here.

**Real sixth step (2026-09-02, "continue working on parena self hosted compiler"): a real,
standalone, argv-parsing `parena-selfhost` binary** — closes the honest gap `selfhost/main.prn`'s
own header comment named directly: "NOT yet a real argv-parsing standalone executable ... argv
plumbing and a real main-emission convention are a genuinely separate, unstarted emitter
feature." Rather than teach `parena-c` a new `(defn main ...)` → C `int main` emission convention
(a real, separate, bigger undertaking with its own design questions), `selfhost/cli_main.c` is
the same real pattern every selfhost test driver already established: a small, hand-written C
`main` doing the real OS-interop (argv → a real `Vec String`, an exit code back out), calling
straight into the PARENA-compiled `build-file`/`build-files` — no new compiler feature, no new
language-level concept, just a real, permanent, user-runnable binary instead of a test-only one.
Mirrors `src/main.c`'s own real `build` subcommand shape exactly (`parena-selfhost build <files...>
-o <out.c>`). New `make parena-selfhost` / `make test-selfhost-cli` Makefile targets — the latter
a real, `fork`+`exec` end-to-end test of the actual compiled binary (not build-file/build-files
called in-process), covering single-file build, multi-file build, and a real nonzero exit code +
stderr message on a real failure.

**Real, live segfault found and fixed the moment this got used for real**: the first thing tried
with the new binary was genuine self-compilation — `parena-selfhost build` pointed at the
self-hosted compiler's own real source files. `selfhost/lexer.prn`/`parser.prn` and the 3 real
stdlib dependencies compiled fine; `selfhost/region.prn`/`emit.prn`/`main.prn` each independently
SEGFAULTED. `gdb` (rebuilt with `-g -O0`) pointed straight at `match-pattern-payload-name`'s own
`(get-field payload :text)`: `match-pattern-has-payload?` only checked a pattern's KIND
(call-shaped), not whether it genuinely has a payload child — a real, zero-payload variant
pattern written parenthesized (`((None) body...)`, real and common throughout
`selfhost/region.prn`'s own match clauses) is call-shaped too, with only ONE child, so
`match-pattern-payload-name`'s own unconditional `children[1]` read was a real out-of-bounds
`Vec` access. Fixed generically (checking child count ≥ 2, not special-cased to the name
`None`) — every previously-crashing file, and the full real 8-file self-compilation attempt
(`stdlib/string.prn`+`array.prn`+`io.prn` + all 5 selfhost domains), now completes cleanly, exit
0, real C output. New regression test (`tests/test_selfhost_emit.c` + a new
`tests/integration/driver_none_paren_pattern.c`): the exact previously-crashing shape, both a
structural "no `#error`, and critically, no crash reaching this line" check and a real
compile+run assertion.

**Real, honest, newly-discovered next frontier**: the self-compiled OUTPUT (`self_compiled.c`,
1544 real lines) does NOT yet compile clean under `gcc` — `selfhost/emit.prn`'s own real emitter
has no support yet for `#target`/`inline-c` FFI bodies (`stdlib/string.prn`'s own real
`length`/`char-at`/etc. all use this — confirmed the REAL `parena-c` compiler handles these
correctly today, so this is a genuine, separate self-hosted-emitter scope gap, not a regression
anywhere in production). True bootstrapping (the self-hosted compiler successfully compiling
ITSELF into a working binary) needs real `#target` emission support in `selfhost/emit.prn` — a
real, substantial, separate feature, not attempted this pass. Local `bazel build //...`
unverifiable this pass (a pre-existing, local-only permission wall: stale `bazel-*` convenience
symlinks in this checkout point into a different user account's own cache — a real environment
quirk, not something CI's own fresh runners would ever hit); the full Makefile suite (342 tests)
and all `test-selfhost-*` targets are clean.

## Status

VS0 lexer/parser done (Apple #14732, commit `3bace34`): 32 unit tests, CI green, real S-expression
reading with no heap allocation outside the compiler's own bump arena.

VS0 region analyzer done (commit `b6d1e43`): a real single-pass symbol-table walk enforcing the
assignment invariant (`Region(Source) ⪰ Region(Destination)`) — `parena analyze` on the real
`examples/test.prn` produces NORTHSTAR's own DoD-table error message verbatim: `Compile Error:
Escaping region pointer from :region/scratch to :region/buffer at line 16`. 8 unit tests (the
DoD's own required positive+negative case, plus real edge cases: same-rank assignment not a false
positive, promoting a longer-lived value into a shorter-lived slot not a false positive, an
unconstrained non-`alloc` `let` binding not falsely flagged, a nested `with-arena` escape still
caught), ASan/UBSan clean, CI green (run confirmed via the GitHub Actions API, not assumed). Real,
honest scope stated in `src/region.h`'s own header: only the assignment invariant — not the Return
invariant, not the Move/ownership invariant, not full bidirectional type inference. `check_call_
escape`'s own real limitation: it recognizes "first argument is the destination" (matching every
`set-data`/`write-string`-shaped STDLIB.md signature), not a general call-graph analysis.

VS0 C emitter done (commit `9bdf91e`): `src/emit.c`, a single-pass emitter producing real C99 for
`with-arena`/`let`/`alloc` — `with-arena` compiles to a real C block scoping a
`__attribute__((cleanup(arena_free_all)))`-attributed `Arena` local, matching NORTHSTAR's own
"reclaimed when its region ends" wording literally. `parena build examples/valid_only.prn -o
out.c` produces C that compiles with zero warnings under the DoD's own exact flags
(`gcc -Wall -Wextra -pedantic -std=c99`, checked in CI with `-Werror` besides), and — verified
beyond what the DoD itself requires — a real driver program linking the emitted code against
`runtime/parena_runtime.c` actually runs it correctly (asserts the real returned value, clean
under ASan/UBSan: the scratch arena's cleanup fired at its own block's exit, the promoted
buffer-arena value survived it). 13 unit tests (real success cases — the cleanup attribute, the
return statement, the runtime `#include` all actually present in the output — and real failure
cases — an unsupported construct or an unbound identifier fails honestly rather than emitting
guessed-at C), CI green (run confirmed via the GitHub Actions API). Real, honest scope stated in
`emit.h`'s own header: only understands the exact shape `test.prn`'s own valid function uses (no
numeric/boolean literals, no arithmetic, no nested calls beyond `alloc`, `char *` is the only
inferred type) — a real, scoped emitter for what VS0's own acceptance case needs, not yet a
general-purpose one for arbitrary future `.prn` programs.

Memory verification and the full `parena build` pipeline's remaining polish (VS0's last 2 DoD
domains) were real, scoped follow-up work at the time this paragraph was written — since done: all
5 DoD domains are complete and CI-green (see root `CLAUDE.md`'s current status line for the
up-to-date summary; this paragraph is kept as the real, dated historical record of how each domain
landed, not rewritten to erase that). Self-hosting (above) has genuinely started as of 2026-08-27,
now that its own gating condition is met.

## Related

- `GoblinFoxDragon/docs2/MOD_SURFACE_NORTHSTAR.md` — the mod-surface scripting-language decision
  PARENA is deliberately not resolving yet, per the founder's own "build it pure" sequencing.
- `Building Your Own Integrated Development Environment.docx` (this repo) — the full source
  material this doc summarizes; read directly for anything not covered above (the generic
  IDE-architecture research at the top of the document, real prior art for whichever editor shell
  gets picked later).
