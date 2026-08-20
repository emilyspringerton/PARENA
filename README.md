# PARENA

**"PARENA is a language to make your software more programmable via fluid and composible plugin
APIS."** — founder's own mission statement. The adoption mechanism: bolt a plugin/FFI boundary
onto existing software first, let PARENA code live there, and PARENA eats more of the host from
the outside in, iteratively — not a rewrite. See `NORTHSTAR.md`'s "What this is" for the full
statement.

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

**Real source exists today** (`stdlib/`, all `parena parse`-verified) for: `vec`, `map`, `string`,
`log`, `buffer`, `io`, `thread`, `sdl2`, `net/tcp`, `net/udp`, `net/http`, `array`, `linalg`
(matmul/transpose/dot; inverse/solve deferred), `stats`, `dataframe` (column/select; read-csv/
filter/group-by deferred), `nn`, `tokenizer` (load; encode/decode deferred), `sort`, `regex/syntax`,
`regex/nfa` (signatures only), `regex/pcre` (a real, working backtracking matcher), `regex/posix`,
`regex/glob`, `expr`, `grep`, `sed`, `awk`, `gfd`, `editor/plugin`, `editor/buffer`, `editor/events`,
`editor/ui`, `ringo`, `world`, `mapbuilder/tools`, `pty`, `shell`, `ssh`, `crypto/hash`, `crypto/aes`,
`crypto/ed25519`, `gfd/browser`, `firefly`, `firefly/gomega`, `scarab`. Every other package in the tables below is designed in `STDLIB.md`
but has no `.prn` file yet (`otp/*`, `media/*`, `sql/*`, `mapbuilder/layout`, `mapbuilder/template`).

## Standard library — full planned API surface

Every package below is designed in `STDLIB.md` with real function signatures (region-typed,
matching NORTHSTAR's own `(var : Type @ Region)` form). This table is the index; `STDLIB.md` is
the source of truth for signatures, grounding, and honestly-stated limitations.

### Built-in (no `import` needed)

| Package | Purpose |
|---|---|
| `core` | `Option`/`Result`, `Arena`/`with-arena` — already core language forms |
| `sdl2` | Windowing/input/audio-device/clipboard, FFI-bound to real SDL2, same function names. Long-term goal is a *native* PARENA reimplementation (not FFI) — explicitly deferred, not started |

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
| `otp/scheduler` | Cooperative work-stealing task pool on real OS threads — real BEAM-style preemption is a much bigger, explicitly deferred undertaking |

### SQL — building blocks, real implementation deferred

| Package | Purpose |
|---|---|
| `sql/ast` | Parsed `Select`/`Insert`/`Update`/`Delete` representation |
| `sql/planner` | `SqlStmt` → `QueryPlan` — real query planning genuinely deferred |
| `sql/driver` | Backend-agnostic `Connection`/`execute`, real wire-protocol work per backend |

### Plotting

| Package | Purpose |
|---|---|
| `ringo` | The matplotlib equivalent — `figure`/`plot`/`scatter`/`bar`/`hist`/`show`/`save`, plots `array`'s `NDArray` onto an `sdl2` window. Named by the founder: "apparently parena is a beetle" -> "RINGO" |

### Testing

| Package | Purpose |
|---|---|
| `firefly` | The base testing library — Go `testing.T`-shaped (`errorf`/`fatalf`/`skip`/`run-tests`). Beetle-named: fireflies are real beetles (Lampyridae) |
| `firefly/gomega` | The matcher-chain library — `expect(x).to(equal(y))`, real Gomega shape |
| `scarab` | BDD + test runner, the real Ginkgo shape — `describe`/`context`/`it`/`before-each`/`after-each`/`run-suite`. Beetle-named for the real scarab cycle/renewal association |

`stdlib/tests/` holds real `.prn` test files written *using* `firefly`, dogfooding it against the
stdlib itself — `vec_test.prn`, `map_test.prn`, `world_test.prn` so far.

### World/map building

| Package | Purpose |
|---|---|
| `world` | Terrain/block placement data model, grounded in SHANKPIT's `TerrainHeightfield` and GoblinFoxDragon's `WorldBlock`/heightmap |
| `mapbuilder/tools` | Click/drag/select/undo affordances, direct generalization of PITVIPER's own real mouse-drag-selection code |
| `mapbuilder/layout` | Constraint-based auto-layout (Android ConstraintLayout / iOS Auto Layout model) — for PC + mobile interface design |
| `mapbuilder/template` | Named prefab/scene instantiation, grounded in GoblinFoxDragon's real per-scene procedural generators |

### Shell/remote/crypto — the concrete dogfooding path into PITVIPER

| Package | Purpose |
|---|---|
| `pty` | Spawn a subprocess on a pseudo-console, generalized from PITVIPER's own ConPTY/openpty code |
| `shell` | The actual shell-resolution policy — direct port of PITVIPER's `isWslStub`/`findGitBash` fix |
| `ssh` | `connect`/`exec`/`open-pty`, FFI-bound to libssh2 |
| `crypto/hash` `crypto/aes` `crypto/ed25519` | FFI-bound to OpenSSL/libsodium — no from-scratch cryptography |

### Editor/plugin API — shell resolved: a PARENA-authored, SDL2-based vim-like editor, hosted by PITVIPER

| Package | Purpose |
|---|---|
| `editor/plugin` | Lifecycle, config, command-palette registration |
| `editor/buffer` | Read/insert/delete/select text ranges |
| `editor/events` | Subscribe to `OnSave`/`OnChange`/`OnKeybind`/`OnDragDrop`/`OnPaste` |
| `editor/ui` | Gutter markers, diagnostics, status bar, popups — renders via `sdl2` |

NORTHSTAR's own "editor shell: Electron/Tauri/GTK/SDL2+ImGui/ncurses+Tree-sitter, undecided"
question is resolved: a modal, Vim-like editor built on `sdl2`, following the same
plugin-boundary-first adoption mechanic named above — `PITVIPER` (Go+SDL2, already real, already
ships ConPTY + mouse-drag-selection + clipboard) hosts it short-term and grows a real plugin API;
the PARENA rewrite happens incrementally once VS0 can compile something this size. Concrete next
step, not yet started: PITVIPER's own Go-side plugin API.

### Mod-surface binding

| Package | Purpose |
|---|---|
| `gfd` | World-object/solidity/skate-surface/faction/METALVERSE-panel bindings for GoblinFoxDragon's `apps2/battlegrounds_gui`, matching the real, already-shipped EduScript builtin table |
| `gfd/browser` | A real, modern web browser panel — FFI-bound to a real embeddable engine (CEF-class), not a native HTML/CSS/JS implementation. Real, unresolved security note: which URLs it can load is GFD-side policy, not answered here |

## Related

- `NORTHSTAR.md` — language design, VS0 Definition of Done, self-hosting plan
- `STDLIB.md` — every package above, with real signatures, grounding, and stated limitations
- `stdlib/` — the real `.prn` source tree, in build order
- `EMILY/BACKLOG.md` — S189-13 (VS0 compiler status) and S189-14/15 (stdlib build history)
