# PARENA

A new systems language: S-expression syntax, compile-time region-based memory safety (no GC, no
manual free), linear ownership for native resources, multiple compilation targets (C first, then
JVM/TypeScript/WebAssembly). Ships its own editor/plugin API as a first-class part of the design.
Full design: `NORTHSTAR.md`. Full standard-library design: `STDLIB.md`.

**Status:** VS0 (the `parena-c` compiler) has a working lexer/parser (domain 1 of 5) — `parena
parse file.prn` is real and works. The region analyzer, C emitter, and memory verification
(domains 2-4) don't exist yet; `parena build` says so rather than pretending. See
`EMILY/BACKLOG.md` S189-13 for the live status.

The `stdlib/` directory holds real, `parena parse`-verified `.prn` source for the packages below,
in the dependency order given here — it is not compilable/runnable yet, since that needs domains
2-3. "Built out in Parena" currently means *real, parseable Parena source exists*, not *the
standard library is executable*. See `STDLIB.md`'s own "Dependency order" section for the full
build-order rationale and the "Priority order" section for what's getting attention first.

## Standard library — full planned API surface

Every package below is designed in `STDLIB.md` with real function signatures (region-typed,
matching NORTHSTAR's own `(var : Type @ Region)` form). This table is the index; `STDLIB.md` is
the source of truth for signatures, grounding, and honestly-stated limitations.

### Built-in (no `import` needed)

| Package | Purpose |
|---|---|
| `core` | `Option`/`Result`, `Arena`/`with-arena` — already core language forms |
| `sdl2` | Windowing/input/audio-device/clipboard, FFI-bound to real SDL2, same function names |

### Foundation

| Package | Purpose |
|---|---|
| `vec` | Growable array — `new`/`push!`/`get`/`len` |
| `map` | Open-addressing hash table — `new`/`get`/`set!`/`contains` |
| `string` | `parse-i32`/`length`/`concat`/`split` |
| `log` | `info`/`warn`/`error` |
| `buffer` | `set-data`/`get-data` on an arena-backed buffer |
| `io` | `open`/`close`/`read-string`/`write-string`/`read-line`/`read-floats` |
| `thread` | OS-thread FFI — `spawn`/`join`/`channel`/`send`/`recv`/`mutex`/`lock` |

### Numeric (numpy/scipy/pandas-equivalent, gpt2-alpine-c-port-grounded)

| Package | Purpose |
|---|---|
| `array` | Region-typed N-dimensional `NDArray` — the numpy equivalent |
| `linalg` | `matmul`/`transpose`/`dot`/`inverse`/`solve` (FFI to real BLAS/LAPACK, not native) |
| `stats` | `mean`/`std`/`sum`/`min`/`max` |
| `dataframe` | Heterogeneous labeled tabular data — `read-csv`/`column`/`select`/`filter`/`group-by` |
| `nn` | `layernorm`/`gelu`/`softmax` — the 3 primitives `gpt2-alpine-c` actually calls |
| `tokenizer` | Real BPE — `load`/`encode`/`decode`, matches `gpt2-alpine-c`'s own 3-function shape |
| `sort` | Generic `sort-by`/`top-k` |

### Pattern matching & text tools

| Package | Purpose |
|---|---|
| `regex/syntax` | Shared pattern-AST parser every engine below compiles |
| `regex/nfa` | Thompson/Pike's-VM, guaranteed-linear-time, no backrefs/lookaround (RE2/Go-style) |
| `regex/pcre` | Full backtracking: named captures, backrefs, lookaround, possessive quantifiers |
| `regex/posix` | BRE/ERE, leftmost-longest matching |
| `regex/glob` | Shell-style `*`/`?`/`[...]`/`{a,b}` |
| `expr` | Tiny arithmetic/string/comparison evaluator (awk's own coercion rules) |
| `grep` | `lines-matching`, engine-selectable (`Nfa`/`Pcre`/`Posix`) |
| `sed` | `substitute` (`s/pattern/replacement/`) over a stream |
| `awk` | Field/record pattern-action programs (`AwkProgram`/`AwkRule`) |

### Networking & media

| Package | Purpose |
|---|---|
| `net/tcp` | `listen`/`accept`/`connect`/`read`/`write` |
| `net/udp` | `bind`/`send-to`/`recv-from` |
| `net/http` | `get`/`post`/`serve` |
| `media/audio` | `open-device`/`load`/`play`/`mix` — the real MIXFORGE crossfade primitive |
| `media/codec` | `decode`/`encode`, FFI-bound to a real codec library (libavcodec-class) |
| `media/stream` | `connect-destination`/`publish` — native multi-destination fan-out (dual-streaming, avoids third-party relay overhead/credential exposure) |

### Concurrency ergonomics (Erlang/OTP-inspired, not a BEAM port)

| Package | Purpose |
|---|---|
| `otp/gen-server` | Callback-module servers (`init`/`handle-call`/`handle-cast`) on real `thread`s |
| `otp/supervisor` | Restart policies (`OneForOne`/`OneForAll`/`RestForOne`) over child gen-servers |
| `otp/ets` | Thread-safe in-memory keyed table |

### Editor/plugin API

| Package | Purpose |
|---|---|
| `editor/plugin` | Lifecycle, config, command-palette registration |
| `editor/buffer` | Read/insert/delete/select text ranges |
| `editor/events` | Subscribe to `OnSave`/`OnChange`/`OnKeybind` |
| `editor/ui` | Gutter markers, diagnostics, status bar, popups |

The editor *shell* itself (Electron/Tauri/GTK+GtkSourceView/SDL2+ImGui/ncurses+Tree-sitter) is
still an open, undecided question — flagged, not resolved, per `NORTHSTAR.md`.

### Mod-surface binding

| Package | Purpose |
|---|---|
| `gfd` | World-object/solidity/skate-surface/faction/METALVERSE-panel bindings for GoblinFoxDragon's `apps2/battlegrounds_gui`, matching the real, already-shipped EduScript builtin table |

## Related

- `NORTHSTAR.md` — language design, VS0 Definition of Done, self-hosting plan
- `STDLIB.md` — every package above, with real signatures, grounding, and stated limitations
- `stdlib/` — the real `.prn` source tree, in build order
- `EMILY/BACKLOG.md` — S189-13 (VS0 compiler status) and S189-14/15 (stdlib build history)
