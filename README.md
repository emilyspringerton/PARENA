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

**Status (2026-08-20): all 5 VS0 DoD domains are complete** — lexer/parser, region analyzer, C
emitter, memory verification (ASan/UBSan/real Valgrind), and the CLI runner (`parena parse` /
`parena analyze` / `parena build file.prn -o out.c`) all real, all CI-verified. This is a closed
milestone for VS0 *itself* — it does not mean the mod-surface stdlib is finished; that's real,
separate, ongoing work tracked below and in `STDLIB.md`/`EMILY/BACKLOG.md`. The emitter has grown
well past the original spec-grounded minimum since VS0 closed: `defenum`/`defstruct` (real
user-defined tagged unions and structs), generic `Arena @ Region` params/returns, `Bool`/`F64`,
`(Vec T)` (a real, working, arena-allocated dynamic array — `vec/new`/`vec/push!`/`vec/get`/
`vec/len`), `&Type`/`&mut Type` reference parameters/fields/returns plus `deref`, `do`, and `set!`
— each landed by actually compiling real `.prn` files (not synthetic snippets) and their emitted C
with `gcc -Wall -Wextra -pedantic -Werror`, not just trusting `parena build`'s own exit code.

The `stdlib/` directory holds real `.prn` source for the packages below, in the dependency order
given here. Most files parse and region-analyze cleanly; how far each one gets through the C
emitter varies by package — some compile completely end to end (`gfd.prn`, `csv.prn`'s pure-PARENA
portions), others are blocked on real, specific, itemized gaps (see `STDLIB.md`'s own gap-analysis
sections) rather than a blanket "not implemented yet." "Built out in Parena" means *real Parena
source exists*, checked per-file against the current compiler — not a claim that the whole
standard library links and runs.

**Real source exists today** (`stdlib/`) for: `vec`, `map`, `string`, `log`, `buffer`, `io`,
`thread`, `sdl2`, `net/tcp`, `net/udp`, `net/http`, `array`, `linalg` (matmul/transpose/dot;
inverse/solve deferred), `stats`, `dataframe` (column/select; read-csv/filter/group-by deferred),
`nn`, `tokenizer` (load; encode/decode deferred), `sort`, `regex/syntax`, `regex/nfa` (signatures
only), `regex/pcre` (a real, working backtracking matcher), `regex/posix`, `regex/glob`, `expr`,
`grep`, `sed`, `awk`, `gfd` (compiles completely, zero warnings), `csv` (the LONGMA port —
`split`/`generate`, pure PARENA except the real file-I/O gap every stdlib package still shares),
`editor/plugin`, `editor/buffer`, `editor/events`, `editor/ui`, `ringo`, `world`,
`mapbuilder/tools`, `pty`, `shell`, `ssh`, `crypto/hash`, `crypto/aes`, `crypto/ed25519`,
`gfd/browser`, `pentest/{scan,pcap,webapp,wireless,crack,exploit}` (5 of 6 compile clean; `pcap`
blocked on a reference-typed parameter), `yoko` (a real, native Metasploit-shaped module/payload
type system — `#target` at the genuine low-level boundary), `firefly` (**compiles completely,
verified with real `gcc -Wall -Wextra -pedantic -Werror`**, not just `parena build`'s own exit
code — the first of the ladybug testing framework's own files to reach this bar), `firefly/ladybug`
(renamed from `firefly/gomega` — see the Testing section below; still blocked on the pre-existing
no-module-linking gap when compiled standalone), `firefly/gomega` (kept as a back-compat alias,
same blocker), `scarab` (still blocked on its own multi-field `defenum` payload gap). Every other
package in the tables below is designed in `STDLIB.md` but has no `.prn`
file yet (`otp/*`, `media/*`, `sql/*`, `mapbuilder/layout`, `mapbuilder/template`, `container/*` —
LXC/cgroups/Docker, designed for the Moltbook/OpenClaw hardening + Emily OS threads, blocked on the
shared file-I/O gap before real `.prn` source is worth writing).

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
| `vec` | Growable array — `new`/`push!`/`get`/`len`. **Real, working, `parena build`-verified today** — a real arena-allocated runtime `Vec` struct, not just a design |
| `map` | Open-addressing hash table — `new`/`get`/`set!`/`contains` |
| `fp` | Ramda-equivalent FP toolkit — `map`/`filter`/`reduce`/`compose`/`pipe`/`prop`/`pluck`/predicate combinators. Real auto-currying explicitly deferred (needs a real closure representation VS0 doesn't have yet) |
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
| `firefly/ladybug` | The matcher-chain library — `expect(x).to(equal(y))`, real Gomega shape. Renamed from `firefly/gomega` (that broke the beetle-naming convention — Gomega is Go's own library name, not a beetle; ladybugs are real beetles, and a direct "finds bugs" pun) |
| `firefly/gomega` | Kept as a back-compat alias of `firefly/ladybug` — exported API unchanged, delegates straight through |
| `scarab` | BDD + test runner, the real Ginkgo shape — `describe`/`context`/`it`/`before-each`/`after-each`/`run-suite`. Beetle-named for the real scarab cycle/renewal association |

This whole framework is also published as its own standalone repo,
[`github.com/emilyspringerton/ladybug`](https://github.com/emilyspringerton/ladybug) — EINHORN_INDUSTRIAL's
official BDD framework, the same way Ginkgo/Gomega are their own repos rather than bundled into Go
itself. That repo's own CI verifies domains 1+2 (parse/region-analyze) on all four files today;
`STDLIB.md`'s own gap list names exactly what's still blocking a full `parena build` there.

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

### Native CLI tooling — `parena ci-status`, a real, narrow dogfooding milestone

The `parena` binary ships a real subcommand, `parena ci-status`, that polls a commit's own
GitHub Actions check-run status directly (`stdlib/ci/status.prn`, module `ci/status`, exported
`check`). Real, honest scope: it replaces the specific ad-hoc `python3`-plus-`curl` one-liners
this session's own workflow had been reaching for repeatedly to check build status — not a
general "phase out Python" initiative, just this one recurring, narrow job (founder, 2026-08-21:
"you are always checking that shit in python" → "build a small parena tool" → "instead of using
python" → "use native parena"). Real, distinct exit codes, matching a normal CLI tool's own
branchable convention: `0` = every check-run completed and green, `1` = at least one still
pending, `2` = completed with at least one real failure, `3` = no check-runs found or the
request itself failed. Same honest `#target` FFI convention every other host-interop file in
this stdlib already uses (`io.prn`/`net/tcp.prn`/`pty.prn`) — the stdlib has no JSON parser or
TLS client of its own yet, so the one `#target` function shells out to the already-installed
`curl` via `popen` and does crude, substring-based extraction of this one specific GitHub API
response shape. Not a general HTTP/JSON library.

The build that produces it is a real, deliberate two-stage bootstrap (`Makefile`): stage 1
(`.parena-bootstrap`, an ordinary build with `ci-status` compiled out) exists only to compile
`stdlib/ci/status.prn` itself down to C (`tools/ci_status_gen.c`) — using PARENA to build part of
its own tooling. Stage 2 recompiles the real, shipped `parena` binary with that generated C
linked back in alongside its host implementation (`tools/ci_status_host.c`). `make build`'s own
target is still just `parena`; callers never need to know it's two stages underneath. A real,
small-scale, working instance of PARENA dogfooding itself — not the full self-hosting compiler
NORTHSTAR's own longer-term plan describes, but a genuine, verified, checked-in first step in
that direction.

## Related

- `NORTHSTAR.md` — language design, VS0 Definition of Done, self-hosting plan
- `STDLIB.md` — every package above, with real signatures, grounding, and stated limitations
- `stdlib/` — the real `.prn` source tree, in build order
- `EMILY/BACKLOG.md` — S189-13 (VS0 compiler status) and S189-14/15 (stdlib build history)
