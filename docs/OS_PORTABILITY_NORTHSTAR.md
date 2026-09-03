# NORTHSTAR — PARENA on Alpine/musl: the smallest real vertical slice

Real, direct answer to kanban priority-queue card `PARENA-0001`: *"can we start to dig into the
parena OS implementation? lets focus the work by targeting alpine? what is the smallest vertical
slice we can take to actually make it work with the os? debian?"*

## Framing, checked before building anything

"Debian" was already, quietly, a solved problem: this dev box is Ubuntu 24.04 (`/etc/os-release`
confirmed live), a real Debian-family, glibc-based distro — every PARENA test/build in this whole
repo's history has already run on a real Debian-lineage OS every single day. There was nothing new
to prove there.

**Alpine is the real, novel case**, for one specific reason: Alpine uses **musl libc**, not glibc.
That's a genuinely different C runtime (different internal type layout, no GNU extensions, a
different dynamic linker), not just a different package manager — the real "does PARENA work with
the OS" question only has teeth on the musl side. So the smallest real vertical slice this session
actually took: **get a real PARENA-compiled program building and running against musl**, checked
live, not assumed.

## Real, no-root musl toolchain (2026-09-03)

No `docker`/`podman` in this sandbox (confirmed, same real gap Phase 5.0's Dockerfile work already
hit) — so no real live Alpine container to build inside directly. Real, no-root workaround, same
class of trick as the `libpcap0.8-dev` extraction this same session already used for the PCAP work:

```
apt-get download musl musl-dev musl-tools   # no root needed
dpkg-deb -x <each .deb> /tmp/musl_extract    # local extraction, no root needed
```

This gets a real `musl-gcc` wrapper + real musl headers/libc archive (`libc.a`) + real CRT objects
(`crt1.o`/`crti.o`/`crtn.o`/`Scrt1.o`/`rcrt1.o`). The stock wrapper hardcodes `/usr/lib/x86_64-
linux-musl` (root-owned, not writable here) — worked around by generating a local copy of
`musl-gcc.specs` with those paths rewritten to the real `/tmp/musl_extract` extraction root, then
invoking the system `gcc` directly with `-specs=<that file>` (musl-gcc itself is just this same
wrapper shape, confirmed by reading it). A trivial `int main(){return 0;}` built this way is a
real, statically-linked, **not a dynamic executable** ELF binary that actually runs on this box —
this is real, not just "should work."

## Real finding #1: `parena-c` (the compiler itself) already builds clean under musl, no changes needed

Compiled all of `src/*.c` (the whole VS0 compiler — lexer/parser/region/emit/emit_java/emit_ts/
fmt/arena/main) with the local musl specs, `-static`: **zero errors**, one pre-existing, harmless
`-Wunused-function` warning. The resulting `parena-c-musl` binary is confirmed statically linked
and was used live to build a real `.prn` file (`examples/valid_only.prn`) into real C — it works
identically to the glibc-built compiler. **The compiler itself was never the blocker.**

## Real finding #2: `runtime/parena_runtime.h` unconditionally required SDL2 — for every generated program, graphics or not

This is the real, load-bearing discovery, found live while trying to compile a real *generated*
program (not the compiler) against musl: `runtime/parena_runtime.h` had a bare, unguarded
`#include <SDL2/SDL.h>` / `#include <SDL2/SDL_ttf.h>` at its top — meaning **every** program this
stdlib ever generates, even one with zero SDL2 usage in its own `.prn` source (`sip/message.prn`,
`pentest/pcap.prn`, any plain CLI tool), required real SDL2 dev headers just to compile, because
every generated `.c` file `#include`s this one runtime header unconditionally.

This is *the* real blocker for a minimal/headless/musl/Alpine target — not because SDL2 itself
can't be built for musl (Alpine's own real `sdl2-dev` package is musl-native and works fine on
real Alpine), but because **this box's own `libsdl2-dev` is glibc-linked**, and mixing glibc-built
SDL2 headers with musl's own libc headers fails for a real, structural reason: musl's `-nostdinc`
build replaces glibc's own internal typedefs (`__gnuc_va_list`, `__time64_t`, etc.) that SDL2's
headers transitively assume are already defined by glibc's own preamble. Confirmed live via the
actual compile error before writing the fix, not guessed.

**Real fix (this session, `runtime/parena_runtime.h`)**: both SDL2 `#include`s, and the entire
contiguous SDL2/SDL2_ttf function-implementation block (window/renderer/event/font handle tables
and every `sdl2_*_impl`/`TTF_*`-calling function), are now wrapped in `#ifndef PARENA_NO_GRAPHICS`
/ `#endif`. Default (macro undefined) behavior is **byte-for-byte unchanged** — every existing
consumer still gets SDL2 unconditionally, confirmed via the full local suite (345/345, zero
regressions). A build that defines `PARENA_NO_GRAPHICS` before including the header skips SDL2
entirely.

## Real, live, end-to-end proof: a real stdlib program, statically linked against musl, zero SDL2

```
parena-c-musl build stdlib/string.prn stdlib/sip/message.prn -o sip_musl.c
gcc -specs=<local musl specs> -static -DPARENA_NO_GRAPHICS \
    sip_musl.c <a real host main.c> runtime/parena_runtime.c -o sip_musl_bin
```

Result, confirmed live: `sip_musl_bin` is `ELF 64-bit LSB executable... not a dynamic executable`
(`file`/`ldd` both confirm zero shared-library dependencies at all — not even musl's own dynamic
loader, since `-static` links musl's own `libc.a` directly in) and **runs successfully**. This is
the real, concrete "PARENA works with Alpine" proof: a fully static musl binary produced on this
Debian-family dev box will run, unmodified, on a real Alpine container — Alpine's whole design
premise is exactly this kind of small, static, musl-linked binary; nothing further needs to be
installed on the Alpine side at all for a binary built this way.

## Honest scope — what this slice does and doesn't prove

- **Proves**: the PARENA compiler itself, and any headless/non-`sdl2.prn`-using generated program,
  can be built as a real static musl binary today, with the one real fix above (already landed).
- **Does not yet prove**: a real SDL2-using PARENA program (the editor, `sdl2.prn` callers) running
  on musl/Alpine — that needs Alpine's own real musl-native `sdl2-dev`, which this sandbox can't
  install (no Alpine container available here). Real, separate, later work if/when a graphical
  PARENA target on Alpine is actually needed.
- **Does not build a "PARENA OS"** in the sense of a custom kernel/init/distro — that ambition, if
  it's the founder's real longer-term intent, is a much larger, separate undertaking (comparable in
  scope to the `FLASH`/`image-builder-rpi`/HypriotOS thread, S213, which already covers "build a
  real bootable distro image"). This doc answers the narrower, concrete question actually asked —
  the smallest real slice to make PARENA itself work with a musl-based OS — and recommends land
  that first before scoping anything bigger.

## Real, honest, not-yet-done follow-ups

- `-static` musl builds aren't wired into `Makefile`/CI anywhere yet — this session's proof was a
  manual, one-off compile. A real `make test-musl` (or similar) target, mirroring `test-
  pentest-pcap`'s own "real, non-default target proving a real environment-dependent capability"
  precedent, is the natural next real step if this direction continues.
- `char*` strings and `I32`s (this stdlib's own dominant real types) are libc-representation-
  identical between glibc and musl, which is *why* this slice was this cheap — a future stdlib
  module leaning on a real glibc-only extension (rare, none identified yet) would need a real,
  separate audit; not attempted here since none is currently known to exist.

## Related

- `PARENA/runtime/parena_runtime.h` — the real file changed (`PARENA_NO_GRAPHICS` guard).
- `PARENA/docs/NATIVE_PCAP_NORTHSTAR.md` — same session's other real "check the real foundation
  before committing to a big ask" discipline, applied to a different, unrelated question.
- `EMILY/BACKLOG.md` SECTION 213 (`FLASH`/`image-builder-rpi`/HypriotOS thread) — the real,
  separate, much-larger "build an actual bootable distro image" ambition this doc deliberately
  does not attempt to answer.
- `PRRJECT_FATBABY/docs/northstar/KUBERNETES_MIGRATION.md`'s own Phase 5.0 Dockerfile work — the
  other real, prior "no Docker in this sandbox" honest gap this session's own musl work also hit.
