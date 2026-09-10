# LLVM Backend — Scoping & Real Findings (2026-09-10)

## Origin

Founder real-time, mid-conversation on the AVR/Arduino editor work: after being asked to choose
between (A) documenting a required Windows toolchain install vs. (B) bundling a portable compiler
into the Windows release, the founder instead asked "no i told you use llvm doesnt that solve
it?... instead of that can we upgrade our compiler so that it supports LLVM directly?" — explicitly
naming this "a monumental ask." This doc is a real, grounded scoping pass (same posture as
`docs/DCGAN_PRIMITIVES_NORTHSTAR.md`: real findings and a phased plan, not a claim of completion)
answering that question directly, not a finished implementation.

## The two different things "supports LLVM" could mean

1. **Use clang (LLVM's C compiler) as the external C compiler**, in place of `gcc`/`avr-gcc`,
   wherever this repo currently shells out to a C compiler (`make editor-demo`'s `$(CC)`,
   `make avr-blink-upload`'s avr-gcc call). PARENA itself keeps emitting plain C text exactly as it
   does today — the only change is which binary turns that C into machine code.
2. **Make PARENA's own compiler (`parena`) emit LLVM IR directly** (or link `libLLVM`'s C API and
   do codegen in-process), replacing the current "`parena build` emits a `.c` file, something else
   compiles it" two-step pipeline with a single, self-contained tool that needs no external C
   compiler for the target platforms it supports.

These are very different sizes of change. (1) is real, scoped, and mostly proven below. (2) is the
literal ask ("upgrade OUR compiler") and is the genuinely monumental one — a new compiler backend,
not a toolchain swap.

## Real, verified findings (not assumed)

**LLVM/clang is fetchable no-sudo, same technique as the AVR toolchain.** `apt-get download
clang-18 llvm-18 libllvm18 libclang-cpp18 libclang1-18 libclang-common-18-dev` fetched ~75MB of
real `.deb`s, extracted via `dpkg -x` into a user-owned directory, no root needed — the same real
recipe `docs/AVR_ARDUINO_NORTHSTAR.md` already established for `gcc-avr`/`avrdude`. Full,
reproducible recipe (matches `Makefile`'s own `LLVM_TOOLCHAIN_ROOT` default):

```bash
mkdir -p /tmp/llvm-deb && cd /tmp/llvm-deb
apt-get download clang-18 llvm-18 libllvm18 libclang-cpp18 libclang1-18 libclang-common-18-dev

mkdir -p ~/.local/opt/llvm-toolchain
for f in *.deb; do dpkg -x "$f" ~/.local/opt/llvm-toolchain; done
```

`libclang-common-18-dev` is a real, easy-to-miss dependency: it's the package that ships clang's
own resource-dir headers (`stddef.h` etc, under `lib/clang/18/include/`) — omitting it produces a
real `fatal error: 'stddef.h' file not found` even though the compiler itself runs fine, since
those aren't part of `clang-18` proper.

**This build's LLVM genuinely includes a working AVR backend.** `llc-18 --version`'s own
"Registered Targets" list includes `avr - Atmel AVR Microcontroller` — not an assumption, the
actual Ubuntu-packaged LLVM 18 ships it, not something that needs to be built from source with a
special flag.

**Compiling PARENA-generated C for AVR via clang, with zero avr-gcc involvement, genuinely works
for the compile step**:

```
clang-18 -target avr -mmcu=atmega328p -DF_CPU=16000000UL -Os \
  -isystem <avr-toolchain>/usr/lib/avr/include \
  -c blink_main.c -o blink.o
```

Real-verified against the exact same `examples/avr/blink_main.c` + `examples/avr/blink_gen.c`
(compiled by PARENA itself) this repo already ships — a clean, zero-error, zero-avr-gcc compile to
a real AVR object file.

**Linking still genuinely needs `avr-ld` (from binutils-avr) — clang does not eliminate that
dependency.** clang's own AVR driver logic shells out to `avr-ld` for the link step (LLVM's own
`lld` linker does not carry AVR's memory-layout link scripts the way `avr-ld` does); it also
expects a specific sysroot auto-detection shape that a raw, hand-extracted `.deb` layout doesn't
match ("no avr-libc installation can be found on the system" — the files are there, clang's own
heuristic just doesn't recognize this exact layout). Worked around by pointing `-isystem` directly
at the real include dir and invoking `avr-ld` manually with the correct crt object
(`crtatmega328p.o`) and per-MCU `libatmega328p.a`/`libc.a` — real, verified: produces a linked
`.elf`.

**Two real, concrete compatibility gaps found between clang and avr-libc (not resolved yet)**:

1. avr-libc's `<util/delay.h>` (`_delay_ms`/`_delay_us`) uses `__builtin_avr_delay_cycles`, a
   GCC-only compiler builtin clang's AVR frontend does not implement — `undefined reference to
   '__builtin_avr_delay_cycles'` at link time. Real, avoidable workaround found: avr-libc's own
   `<util/delay_basic.h>` provides `_delay_loop_2`, an ordinary inline-asm function (not a GCC
   builtin) that compiles clean under clang — a real, usable substitute, just a different API than
   `blink_main.c` currently uses.
2. Linking against avr-libc's `libc.a` pulls in `exit()` (called by the crt startup code after
   `main` returns, standard AVR convention), which itself calls `_exit()` — normally supplied by
   GCC's `libgcc`. clang has no equivalent `libgcc.a` to link, and no bundled weak `_exit` stub was
   found, so `-latmega328p -lc` alone leaves `_exit` unresolved. Real, not-yet-tried candidate
   fixes: hand-supply a one-line `void _exit(int s) { for(;;); }` in the host file (an infinite
   `main` loop never legitimately reaches it anyway), or link with `-nostartfiles` and a fully
   custom minimal crt0 that never calls `exit` at all.

**Both gaps closed same day, Phase 0/1 shipped (2026-09-10, founder real-time: "continue working on
LLVM we want to do both plans first the clang rout then the direct AVR route start the clang
work").** `_delay_loop_2` (from `<util/delay_basic.h>`, an ordinary inline-asm function, not a GCC
builtin) replaced `_delay_ms` in `examples/avr/blink_main.c` — real, portable under both avr-gcc
and clang, kept as ONE shared host file rather than forking a clang-only copy. The `_exit` gap
closed by simply also linking against avr-gcc's own real `libgcc.a` (already present in
`AVR_TOOLCHAIN_ROOT` — no hand-rolled stub needed, confirmed live that `libgcc.a` genuinely defines
`_exit`, not just declares it). Result: a genuinely clean, zero-undefined-symbol `blink_clang.elf`
via `clang -target avr` + `avr-ld` alone (no avr-gcc invoked for compilation) — `avr-size` reports
224 bytes / 0.7% of an atmega328p (vs. avr-gcc's own 194 bytes for the identical program — a real,
small code-size difference, not investigated further here), and `avr-nm`/`avr-objdump` confirm a
real interrupt vector table, `main`, `next_led_state`, and `_exit` all correctly present and
resolved — the same real verification bar the avr-gcc build already met.

New Makefile targets `avr-blink-hex-clang`/`avr-blink-upload-clang` (Phase 1), coexisting with the
original avr-gcc-based `avr-blink-hex`/`avr-blink-upload` — real, parallel, not a replacement.
`LLVM_TOOLCHAIN_ROOT` (a new, persistent, no-sudo-acquired install at
`~/.local/opt/llvm-toolchain`, same real convention `AVR_TOOLCHAIN_ROOT` already established) plus
`AVR_GCC_ARCH`/`AVR_CRT` (real, separate, overridable variables for the crt/libc-arch selection
this manual link line needs — unlike avr-gcc's own `-mmcu` flag, this doesn't auto-derive from
`AVR_MCU`; a real, named, not-yet-automated limitation). Full run of
`make avr-blink-upload-clang` verified end to end: compiles, links, converts to `.hex`, and
correctly attempts the real serial port — failing only at the same expected port-open step
(`/dev/ttyACM0: No such file or directory`) the avr-gcc path already does. `editor-demo` still
builds clean and `make test` stays 347/347 after the `blink_main.c` change.

**Not yet done**: CI does not run either AVR/LLVM path (neither did before this work — a real,
pre-existing, honestly-named gap, not introduced here); adding it means CI would need to
apt-get-download both toolchains fresh on every run, a real added cost not decided on yet.

## What (1) — "clang instead of gcc" — would actually solve

clang runs natively and near-identically across Linux/macOS/Windows from one real toolchain
technology, unlike GCC/MinGW's more fragmented, DLL-dependent Windows story. If Phase 0's two gaps
get closed, PARENA's own AVR build tooling could plausibly use ONE compiler technology
(clang + `avr-ld`) on every platform instead of "apt-get-downloaded avr-gcc on Linux, `brew install
avr-gcc` on macOS, some Windows-specific avr-gcc distribution on Windows" — a real simplification.
It does NOT eliminate the need for a real toolchain download entirely (clang + binutils-avr must
still exist on the machine running the build — this doesn't shrink to "just parena.exe" for
Windows), and it does NOT touch the `make editor-demo` hot-reload path at all, which still needs a
full SDL2-linking native compiler (clang can do this too, but that's separately unverified here).

## What (2) — the literal "upgrade parena to emit LLVM directly" ask — would take

This is a real new compiler backend, the same category of work as `src/emit_ts.c`/`src/emit_java.c`
already are, but heavier: those emit readable TypeScript/Java source text; an LLVM backend would
need to either (a) emit LLVM IR text (`.ll`) — plausible with `src/emit.c`'s own existing
AST-walking shape, but still needs `clang`/`llc` as a downstream step to turn `.ll` into machine
code, so it doesn't by itself remove the "needs an LLVM toolchain installed" requirement — or (b)
link `libLLVM`'s C API (`llvm-c/Core.h` etc.) into `parena` itself and build IR + emit
object code in-process, which would genuinely make `parena` a self-contained compiler needing no
external `cc` at all for the platforms/targets it's linked against. (b) is the version that
actually delivers on "our compiler supports LLVM directly," but it means statically or dynamically
linking a large C++ library into what is currently a small, dependency-free, portable ~5-file C
program — a real, non-trivial build-system and portability change in its own right (also affects
`BURROW`'s own real, live-tracked "full parallel rewrite" effort, which mirrors `parena`'s current
C-emission architecture — a design decision here has a real, direct downstream consequence there
that isn't scoped in this doc).

## Real, phased plan (Phase 0/1/3-v0 shipped, Phase 2 not started)

- **Phase 0** (shipped 2026-09-10): close the two found clang/avr-libc gaps — a real
  `_exit` resolution (via avr-gcc's own real `libgcc.a`) and switching `examples/avr/blink_main.c`'s
  delay call to `_delay_loop_2`. Produced a genuinely clean, zero-undefined-symbol `blink_clang.elf`
  via clang + `avr-ld` alone, confirmed (via `avr-objdump`/`avr-nm`/`avr-size`, matching the existing
  avr-gcc build's own real verification bar) a correct, equivalent program.
- **Phase 1** (shipped 2026-09-10): `PARENA/Makefile`'s new `avr-blink-hex-clang`/
  `avr-blink-upload-clang` targets, real and parallel to the original `avr-blink-hex`/
  `avr-blink-upload` (both real toolchains coexisting, avr-gcc not replaced), on Linux — verified
  end to end with `make avr-blink-upload-clang`, failing only at the same expected port-open step
  the avr-gcc path already does.
- **Phase 2** (not started): verify the identical clang + `avr-ld` path on a real Windows CI
  runner — genuinely unchecked here; binutils-avr's `avr-ld` would need its own Windows-native or
  MinGW-cross build, which is a separate, not-yet-investigated acquisition problem of its own.
- **Phase 3, v0 shipped same day (2026-09-10)**: real, live "the literal ask" for the narrow scope
  this whole example already covers — `src/emit_llvm.c` is a new, real emitter (`parena build
  input.prn -o output.ll`, same real "dispatch by output extension" convention `emit_ts`/
  `emit_java` already established, zero new CLI flags) that walks the AST and emits genuine LLVM
  IR text directly — no C representation of the decision logic exists anywhere in this path.
  `examples/avr/blink.prn`'s own `next-led-state` compiles through it to real, correct LLVM IR
  (`define i1 @next_led_state(i1 %current) { entry: %0 = xor i1 %current, true ret i1 %0 }`), which
  `llc -mtriple=avr -mcpu=atmega328p -filetype=obj` lowers STRAIGHT to real AVR machine code —
  disassembly confirmed byte-for-byte sensible (`ldi r25,1` / `eor r24,r25` / `ret`, a correct,
  minimal, single-register implementation of boolean negation). Linked against a new host
  (`examples/avr/blink_main_llvm.c`, an `extern` declaration only — no `#include`, since the real
  function body lives in a separately-compiled object file) via `avr-ld` + avr-gcc's own real
  `libgcc.a` (same real `_exit`-resolution technique Phase 0 already established) into a
  genuinely correct, complete `blink_llvm.elf` — real vector table, real crt startup, `main`/
  `next_led_state`/`_exit` all present and correctly linked. New Makefile targets
  `avr-blink-hex-llvm`/`avr-blink-upload-llvm`, verified end to end via `make
  avr-blink-upload-llvm`, failing only at the same expected `avrdude` port-open step every other
  AVR target in this repo already does.

  Real, load-bearing detail found by actually disassembling the output, not assumed: `Bool` lowers
  to LLVM's `i1`, and AVR's default C calling convention passes/returns an `i1` value in exactly
  ONE 8-bit register (`r24`) — so the host's own `extern` declaration must be `unsigned char
  next_led_state(unsigned char)`, NOT `int next_led_state(int)` (which would wrongly expect a
  16-bit `r24:r25` pair) — a real, concrete ABI detail this emitter's own consumers need to get
  right, not a hypothetical concern.

  Real, honest v0 scope, named directly: scalar (I32/F64/Bool) params/returns only, no String (LLVM
  string constants need global declarations + pointer types — real, separate, not-attempted scope);
  `if` lowers to a real `select` instruction (not branch/phi — correct and simpler for this v0's
  pure, side-effect-free scope); `and`/`or` are honestly non-short-circuiting LLVM bitwise ops
  (observably identical for pure scalar values, named as a real semantic difference from every
  other backend's own short-circuiting `&&`/`||`); a call's own argument types are not independently
  re-verified against the callee's declared parameter types (the same real, narrow limitation
  `emit_java.c`/`emit_ts.c` already carry in spirit). 22 new real assertions
  (`tests/test_emit_llvm.c`, run via both `make test-emit-llvm` and real Bazel
  `bazelisk test //tests:test_emit_llvm`), covering successful emission (constants, Bool/`not`,
  if/select, F64 vs. I32 arithmetic opcode splits, forward-referencing calls via a real two-pass
  signature table) AND real, honest error paths (undeclared symbols, a float literal in I32
  context, mismatched `if`-branch types from a call's own real return type) — never silently-wrong
  IR. `make test`: 347/347, zero regressions; `editor-demo` still builds and smoke-tests clean.

  This closes "our compiler supports LLVM directly" for real, for the scope it was actually proven
  against — it is NOT the same as libLLVM linked in-process (no `libLLVM` C API usage at all here;
  `parena` still just writes a `.ll` text file, same "generate source text a separate real tool
  consumes" shape every other emitter in this repo already uses, just targeting IR text instead of
  Java/TS/C source text), and it does not yet cover the full PARENA language (Vec/Result/Region/
  String/pattern-matching are all real, separate, not-yet-attempted scope for this emitter
  specifically). The much larger, ORIGINALLY-scoped Phase 3 (`libLLVM` linked directly into
  `parena`, full language coverage, no external `llc`/`clang` needed at all) remains real,
  separate, unstarted work — this v0 is deliberately narrower, matching the exact same "one real
  scalar-function slice, not full language coverage" precedent SPIDERBEETLE's Java emitter and this
  repo's own TypeScript emitter both already established for their own first real target.

## Honest bottom line

Both plans the founder asked for are real and shipped, at least in v0 form. LLVM's AVR backend is
real, present, and no-sudo-fetchable. Phase 0/1 (the "clang route"): clang + `avr-ld` genuinely
produce a correct, verified AVR binary for `examples/avr/blink.prn`, coexisting with the original
avr-gcc path. Phase 3-v0 (the "direct AVR route" — the literal "our compiler supports LLVM
directly" ask): `parena` itself now has a real `src/emit_llvm.c` backend that emits genuine LLVM
IR text with zero C involved in the decision logic, which `llc` lowers straight to real, correct,
disassembly-verified AVR machine code. Both remain real, honest v0s, not final forms: Phase 2
(Windows) is unstarted — `avr-ld` itself would need a Windows-native build regardless of which
route is used — and the LLVM emitter covers only PARENA's own narrow scalar-function slice, not
the full language, and does not (yet) mean `parena` links `libLLVM` in-process; it still writes a
`.ll` text file for a separate real tool to consume, same shape every other emitter here uses.
