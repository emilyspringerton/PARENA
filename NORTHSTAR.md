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
