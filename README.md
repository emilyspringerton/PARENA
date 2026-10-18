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

**Real source exists today** (`stdlib/`) for: `vec`, `map`, `string`, `unicode` (strict UTF-8 codec, Unicode 15.1 properties/simple case mapping/width; `\u{...}` string escapes), `log`, `buffer`, `io`,
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
| `crypto/hash` `crypto/aes` `crypto/ed25519` | Design only — `crypto/ed25519.prn` exists but is a non-working stub (no libsodium linked) |
| `crypto/mldsa` | **Real, working, verified** (`make test-mldsa`) — post-quantum ML-DSA-44/FIPS 204 keygen/sign/verify, FFI-bound to a vendored, independently-verified copy of the official pq-crystals/dilithium reference (opt-in via `PARENA_WITH_MLDSA`). PARENA's first actually-working crypto binding. No live SSH-auth consumer yet — see `stdlib/crypto/mldsa.prn`'s own header comment |

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

## Self-hosting dogfooding: turbogrep/turbosed/turboawk

Three real CLI tools, each PARENA-compiled `stdlib/regex/pcre.prn` + `stdlib/{grep,sed,awk}.prn`
concatenated with a thin C host wrapper (`tools/turbo{grep,sed}_host.c`) — genuine dogfooding,
not synthetic test files:

- **turbogrep** — real, working, and actually **installed**: `~/.local/bin/grep` resolves to
  `turbogrep-router.sh`, an AST-based capability check that routes a pattern here if PARENA's own
  regex engine can handle it, falling back to real `grep` otherwise (currently ~8x slower than
  system grep, down from an initial ~430x — see `docs/TURBOGREP_VERIFICATION_REPORT.md`). Every
  invocation logs to `var/grep-invocations.ndjson` to guide its own next work.
- **turbosed** — real and working (`docs/TURBOSED_VERIFICATION_REPORT.md`, verified against real
  GNU sed output across 68 real `.prn` files), deliberately **not** routed into `~/.local/bin/sed`
  — sed mutates real pipelines, a higher bar than grep's read-only matching.
- **turboawk** — `stdlib/expr.prn`'s arithmetic/string/variable-binding evaluator and
  `stdlib/awk.prn`'s read-split-match-evaluate pipeline both compile and pass real end-to-end
  tests (`make test-awk`, 10/10). No CLI binary yet — `expr.prn`'s expression language has no
  `print`/output primitive, so a real, honest `turboawk` command needs that designed first; not
  faked by shipping a tool with nothing to print.

## REDGARDEN mods: PARENA driving another repo's live gameplay

`stdlib/redgarden/` compiles real PARENA mods straight into REDGARDEN's own C binaries (pure
C-to-C linking, no cgo layer) — "the mod is the trigger, host C does the real work": Bloodflower
(day/night lunar event), Tree passive (auto-attack + self-heal), build templates (shop auto-buy),
item curriculum (NORTHSTAR §26.3.2's stat-blending generation primitive), and Duck's Smoke Bomb
(a real AoE target-denial ability). Same pattern already proven in EmilyOS (`fsaclmod`,
GRANT_FS/REVOKE_FS) and PITVIPER (`scrollmod`, mod-surface v0) — real, live, shipped features in
other repos' own production code, not a demo.

## Adopting PARENA in your own repo: where does your `.prn` file live?

Real, direct answer to kanban priority-queue cards `PX-001`/`PX-BZ-001`: *"we need to add support
to commit the .prn file right in the same directory as the generated output... currently the
idiom is to add your mod to the stdlib... not sure how that would work with bazel and other build
tools."* Real, checked-not-assumed finding: **this already works today, no compiler change
needed** — `parena build <file>.prn -o <out>` (and `burrow build`, its Go-target counterpart)
accepts a `.prn` file at any path; nothing enforces that source live inside this repo's own
`stdlib/`. Contributing to `stdlib/` is a real, useful convention (shared code, reviewed once,
usable by every consumer) — it was never the only supported option.

**Two real, live examples already exist in this monorepo, both pre-dating any dedicated
tooling**: `DUNG/parena/entry.prn` (co-located with its own generated
`internal/burrowgen/entry_gen.go`, regenerated via a `//go:generate` directive, checked into a
plain `go_library` Bazel rule) and `ladybug/BUILD.bazel` (a hand-written genrule invoking
`@parena//src:parena` directly against a co-located `.prn` file).

**New, real, reusable Bazel macro** (`bazel/parena_compile.bzl`, `parena_compile_c`) closes the
one genuine friction point those two examples still had: every adopting repo previously had to
hand-write its own genrule. A consuming repo's own `BUILD.bazel` now needs only:

```python
load("@parena//bazel:parena_compile.bzl", "parena_compile_c")

parena_compile_c(
    name = "my_mod_gen",
    prn = "my_mod.prn",
    out = "my_mod_gen.c",
    deps = [],  # extra .prn files needed in the same build invocation, e.g. a stdlib module
)
```

Real, live-verified, not just written: built via a real `bazel build` in this repo's own
`bazel/testdata/` (both single-file and multi-file `deps` cases), producing correct, real C
output. Real, deliberate v0 scope: the C target only — an analogous macro for BURROW's own Go/TS/
Java targets would live in the BURROW repo (a different Bazel module), real, separate, honest,
not-yet-built follow-up. For non-Bazel build tools (Make, plain shell scripts), the exact same
co-location already works trivially — it's just a file path argument, no macro needed at all
(this repo's own `Makefile` already does this for every `test-*` target, e.g. `test-wire`/
`test-sip-message`).

## `parena new` — a real, "batteries included" scaffolding command

Real, direct answer to kanban priority-queue card `PXCL-9311`: *"make parena cli tool do the same
thing burrow new does but for C instead of for go"* — the direct C-target sibling of BURROW's own
already-shipped `burrow new`. `parena new <name>` generates a real, immediately-runnable starter:
`<name>.prn` (a minimal PARENA decision-logic module), `main.c` (a real C host including the
compiled output directly and calling into it), and a local copy of `runtime/parena_runtime.h`/
`.c` (so the new project doesn't need this repo's own source tree at a known relative path) —
then actually compiles AND RUNS the result via a real `gcc`/`cc` invocation before returning
success, so a broken scaffold is a real, honest failure in this command, never silently handed to
the user. Built headless (`main.c` defines `PARENA_NO_GRAPHICS` before including the runtime
header, this same repo's own real SDL2 opt-out), matching `burrow new`'s own "don't force a
heavier dependency on a trivial scaffold" judgment. Real, honest v0 assumption: run it from this
repo's own root (the runtime files are looked up relative to the current working directory, not
argv[0]) — a real, more portable fix is separate, later work. Live-verified via
`tests/integration/run_new_check.sh` (`make test-new`): a real scaffold builds, runs, prints the
correct output, and a second `parena new` on the same name correctly refuses to overwrite it.

## V16 — a JavaScript engine written in PARENA, running as WebAssembly (spike, 2026-10-15)

`stdlib/v16/engine.prn` is a small but real JavaScript execution engine (closures, arrow functions,
try/catch, for-of, switch, arrays/objects, `console.log`, `JSON.stringify`, `Math`, array/string
methods) written entirely in PARENA — no malloc, no GC: every token, bytecode word, value, string,
closure and the output buffer is bump-allocated from one `Arena`, and a per-run byte budget turns a
runaway script into a `RangeError`. It compiles PARENA → C → WebAssembly:

```bash
pip install ziglang wasmtime           # any wasm32 C compiler works; these two are the default
make test-v16-engine                   # corpus natively, -Wall -Wextra -pedantic -Werror
make v16-wasm                          # build/v16.wasm  (~130 KB, zero imports, 5 exports)
make test-v16-wasm                     # same corpus through wasmtime
```

**Honest status:** a spike, not a conformant engine — no `this`/prototypes/classes, `finally`,
generators, async, regex, template literals, destructuring, `JSON.parse`, `Date`/`Math.random`,
Unicode semantics or GC; not run against test262; the browser loader
(`examples/v16_wasm/index.html`) is untested in a real browser. Full account, limits and the
road ahead: `docs/V16_NORTHSTAR.md`; API and build details: `STDLIB.md` "v16/engine".

## BRAWLPIT in the browser — a whole game as WebAssembly, and the page's JS generated from PARENA (2026-10-16)

`stdlib/brawlpit/bp_*.prn` is BRAWLPIT, the platform fighter, ported to PARENA and compiled to one
wasm32 module: the ten characters and their specials, shields, smashes, blast zones, projectiles, the
CPU fighters, the menus, and a software renderer into a 960x540 RGBA framebuffer. **No malloc**: every
record is a fixed-address array in linear memory, the module imports nothing and its 8 MB memory cannot
grow (the build fails if an allocator symbol, an undefined symbol, a `memory.grow`, or any import appears).
The browser page needs JavaScript only for what a browser alone can do (instantiate the wasm, blit the
framebuffer, forward keys / gamepad / pointer, turn the equipped hat into pixels), and that host code, `bp_host.prn`, is
PARENA compiled to **TypeScript** by `emit_ts` and type-stripped to JS -- nothing hand-written.

```bash
scripts/build_brawlpit_web.sh out/          # out/brawlpit.wasm + out/bp-host.js (+ the .ts)
make test-brawlpit                          # build + every test below (needs node, gcc, the LLVM toolchain, ../BRAWLPIT)
```

* **parity** -- the physics is a line-for-line port of `BRAWLPIT/packages/common/physics.h` + `local_game.h` on the
  new **F32** wasm backend. `tests/brawlpit_parity_test.mjs` runs the real C engine and the wasm engine over 74
  scripted scenarios (all 10 characters, 3 stages, 1v1 / 3-way / 4-player / two humans / sandbox, chasers, special
  spam, restarts) and requires every player word and projectile equal after every one of 174,600 ticks, comparing
  floats as raw bits; a coverage check fails the run unless KOs, high-percent launches, shield breaks, parries,
  every character's special and both Duck projectiles were reached.
* **engine / host** -- 88 + 61 assertions: module hygiene (0 imports, no allocator export, fixed memory), menus and
  match flow by key events only, 2P / free-for-all / practice rules, hats and portraits drawn by the wasm,
  determinism, the CPU brain; and the generated `bp-host.js` driving the real wasm under a DOM stub.
  `WOTAN/scripts/brawlpit-browser-test.mjs` does the same in headless Chromium (26 checks).
* **hats** -- the player's equipped Hat Store hat is **worn by default**: when the page calls `bp_hat_set(0, 1)` the
  engine makes it player 1's hat (on the select screen, or mid-match if it lands late); only a hand-picked NO HAT /
  colour hat overrides it, and the CPU and player 2 never wear it. A pixel hat is a PNG (`image_url`); a curated
  catalogue hat has none and IDUNA stores its art as an HTML-entity string (`&#127914;`), which `bp_host.prn`
  decodes (numeric references, in PARENA -> TypeScript, no `innerHTML`), draws as a glyph, and fits onto the head.
  Honest limit: the glyph path was exercised here with a text symbol -- this build machine has no emoji font.
* **compiler work this needed** (the "if PARENA can't express it, that is the first task" rule): the LLVM backend
  gained `F32`, float memory ops, `f32/f64/i32` conversions (saturating, never trapping), `sqrt/fabs/floor`
  intrinsics, typed `let` binders and `cond/case/dotimes/inc!/...` sugar; the TypeScript backend gained statements,
  locals, `loop/recur`, module state (`def`, `start`), `Any` and a typed JS bridge (`js/get`, `js/call`, `js/new`,
  `callback` ...). Existing TS output (MISHRI, DEADWEIGHT rules) is byte-identical.

**Honest status:** verified in Node and headless Chromium only -- not on Firefox/Safari, not on a phone (there are no
touch controls), and the keyboard / gamepad paths were driven synthetically, no physical controller was attached.
No audio. Online play is not built (the page is local: vs CPU, two players on one keyboard / pads, free-for-all,
practice). The CPU brain levels EASY/NORMAL/HARD are new web code, not part of the parity-tested simulation
(CLASSIC is the C engine's own bot). Consumer: `WOTAN/BRAWLPIT/`. Details: `STDLIB.md` "brawlpit".

## BEDROCK RACERS in the browser — a rocket-car ball game as WebAssembly (2026-10-18)

`stdlib/bedrockracers/web/br_*.prn` is Bedrock Racers, a Rocket League-style Quidditch game: four teams of three rocket cars in one
stadium (a rounded box whose floor, walls and ceiling you can all drive on), one ball and four upside-down corner goals. Every car has
a **magnet**: hold it to carry the ball, let go to throw it, hold it at a team-mate to pass, bump a carrier to steal; be in on two of
your team's goals in a row and you are **ON FIRE** (a full tank, faster throws, more reach, a stadium lit in flame). It is one wasm32
module (~215 KB, **no malloc, zero imports, a fixed 8 MB**) with the physics, the rules, bots at three skill levels, a z-buffered
perspective software renderer into a 640x360 framebuffer, the HUD and the screens. The page host, `br_host.prn`, is PARENA compiled
to **TypeScript** and type-stripped to `br-host.js` -- no hand-written JavaScript.

```bash
scripts/build_bedrockracers_web.sh out/      # out/index.html + bedrockracers.wasm + br-host.js (+ the .ts): serve it statically
make test-bedrockracers                      # build + every test below + the soak line (the browser test needs Playwright configured)
```

* **drawing only reads the sim** -- `tests/br_engine_test.mjs` runs one match drawn every tick and one never drawn for 700 ticks and
  requires every car and ball byte to be identical; the bots' soak line (`tests/br_soak_baseline.txt`, 24 bot-minutes) must stay
  byte-identical across any render-only change.
* **engine / host** -- 92 assertions: module hygiene, every export the page host calls exists, no two global words share a slot,
  the title / pause / results flow by keys and by clicks, keyboard / right mouse / gamepad mapping, the camera through a wall-to-ceiling
  climb and a kickoff, HUD pixel guards, the frame budget. `tests/br_browser_test.mjs` serves the static bundle and drives it in
  headless Chromium (28 checks, 60 fps), with real clicks, a Gamepad API shim and focus loss.
* **can a person win?** `tests/br_human_player.mjs` is a keyboard-only player (only `br_key` events and what is on screen: no test hooks,
  no oracle) for the amber team's first car against EASY bots: chase the ball, hold F to carry it, run at the corner its throw already
  points at, let go once the throw is aimed within a few degrees of the corner's apex. **Seed 1 scores, reproducibly, at tick 12940**
  (the sim is deterministic), so yes, with the default controls. The honest rate is low: 1 of 7 four-minute matches (seeds 1-7), because
  the bots swarm the carrier and touch most throws in flight (a note for tuning EASY, not done: that is a design call).
* **found by looking at it** -- pausing did not freeze the view; after a goal scored from the ceiling the camera stayed upside down with
  its eye outside the arena; a carried ball hid the road; the overtime banner could never show. Each is fixed and has a test.

**Honest status:** verified in Node and headless Chromium only -- not on Firefox/Safari, not on a phone (no touch controls), and the
gamepad path was driven synthetically. No audio. Local play against bots only: there is no sign-in, matchmaking or stats yet, street
tricks / jukes and different car classes are not started, and the CHAMPION bot level has no league yet. Frame time in Node on a shared
vCPU: median ~8.4 ms, p99 ~10 ms. The WOTAN page (`/bedrockracers/`) is finished on a branch, not deployed. Details:
`stdlib/bedrockracers/README.md`, `HANDOFF.md`.

## `plugins/static_analysis` — multi-tier IR lifter + SSA (Python)

Headless binary analysis: `Lifter.open(path)` (ELF64 / PE32+ / raw x86-64) indexes functions and gives each
one `llil` -> `mlil` -> `ssa` / `optimized_ssa` -> `hlil`. SSA has φ-nodes, versioned `vars_read`/`vars_written`
and def-use maps; `optimize()` does constant propagation/folding, branch folding and dead-code elimination.
Run the tests with `python3 -m unittest plugins.static_analysis.test_static_analysis -v`.
**Status:** tested on hand-assembled snippets, hand-built ELF/PE images and gcc -O0/-O2 output. **Limits:** x86-64
integer subset only (SSE/x87/string ops/adc/sbb/mul are reported as `unimplemented`, never guessed); no jump-table
recovery or type inference; cross-block flag reuse is handled only through single-predecessor chains.

## Related

- `NORTHSTAR.md` — language design, VS0 Definition of Done, self-hosting plan
- `STDLIB.md` — every package above, with real signatures, grounding, and stated limitations
- `stdlib/` — the real `.prn` source tree, in build order
- `EMILY/BACKLOG.md` — S189-13 (VS0 compiler status) and S189-14/15 (stdlib build history)
- `ladybug` — EINHORN_INDUSTRIAL's own BDD testing framework, written in PARENA (`firefly`/
  `firefly/ladybug` above are its stdlib half)
- `longma` — a real PARENA port of a CSV split/generate tool (`stdlib/csv.prn` above)
