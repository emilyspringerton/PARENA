# NORTHSTAR — PARENA

**Status:** VS0 lexer/parser built and CI-verified (DoD domain 1 of 5). Region analyzer, C
emitter, and full build pipeline (domains 2-5) not yet built.
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

## Self-hosting — a real future milestone, not started

Founder, real-time: "ok but after we have a compiler we also need to write parena in parena" →
"not c" → "silly." A real, well-understood language-engineering milestone (the same one Go, Rust,
and most serious systems languages eventually hit): once the language and its C-implemented
compiler (`parena-c`, VS0 above) are complete enough, rewrite the compiler *in Parena itself* —
explicitly not staying C underneath, per the founder's own emphasis. Architecturally this can't
start until VS0's remaining domains (region analyzer, C emitter, full build pipeline) exist AND
enough of the stdlib (still undesigned — see "Standard library" above) is real enough to write a
parser/analyzer/emitter in Parena. Not scoped further than that here — a real VS1/VS2-class
milestone, sequenced after VS0 is actually done, not attempted now.

**JIT compilation — a real, much further-out idea, not scoped**: founder, real-time: "we can do
some fancy stuff like v8 with our compiler i bet to make it groovy." A genuinely real compiler-
architecture direction (V8's own JIT — parse, run once in a fast baseline interpreter, profile,
recompile hot functions with an optimizing tier) — but one that presupposes VS0's own domains 2-5
(region analyzer, C emitter, full build pipeline) exist first, since a JIT is itself a compiler
backend sitting where the C emitter sits today, several stages beyond where VS0 currently is
(domain 1 of 5). Flagged as a real idea worth remembering, not designed or sequenced here.

## Status

VS0 lexer/parser done (Apple #14732, commit `3bace34`): 32 unit tests, CI green, real S-expression
reading with no heap allocation outside the compiler's own bump arena. Region analyzer, C emitter,
and the full `parena build` pipeline (VS0's remaining 4 DoD domains) are real, scoped, unstarted
follow-up work — the DoD table above is the actual acceptance bar, not a vague target.
Self-hosting (above) is sequenced after VS0 completes.

## Related

- `GoblinFoxDragon/docs2/MOD_SURFACE_NORTHSTAR.md` — the mod-surface scripting-language decision
  PARENA is deliberately not resolving yet, per the founder's own "build it pure" sequencing.
- `Building Your Own Integrated Development Environment.docx` (this repo) — the full source
  material this doc summarizes; read directly for anything not covered above (the generic
  IDE-architecture research at the top of the document, real prior art for whichever editor shell
  gets picked later).
