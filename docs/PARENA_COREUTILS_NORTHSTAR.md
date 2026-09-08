# PARENA Coreutils/Busybox — North Star

**Status:** v0 slice shipped this pass (real multi-call dispatch + 5 real applets). Full busybox
replacement (init/sh/mount) is a real, later, much larger undertaking — named honestly below, not
promised.

## Why this exists

Founder, real-time: "let's write our own parena powered busybox." Directly motivated by this
session's own EmilyOS/Alpine Raspberry Pi image work (`EmilyOS/docs/NORTHSTAR_DISTRO.md`): the
one remaining real gate before a first bootable image is Alpine's own `busybox` package running
its real `--install -s` trigger script (creating ~304 applet symlinks, including `/sbin/init`)
under a privileged `chroot` — confirmed live this same session by extracting and reading
busybox's actual `.trigger` script. A PARENA-native replacement for at least the applets EmilyOS
actually needs would remove that specific dependency on Alpine's own busybox package entirely,
and is a real, direct instance of this monorepo's own standing "PARENA native as much as
possible" theme (`NORTHSTAR_DISTRO.md`'s own 2026-08-25 guidance) — the same dogfooding
discipline `PITVIPER`/`DUNG`/`SAND`/`ECOWAR`/`MIXFORGE` already follow.

## Real tension named directly, not glossed over

`NORTHSTAR_DISTRO.md`'s own existing guidance says **"GNU tools stay for load-bearing
infrastructure... proven, correctness-critical pieces don't get experimentally swapped for
PARENA-native alternatives"** — and `init`/`sh`/`mount` are about as load-bearing as software
gets (a broken `init` means the machine doesn't boot at all). This is a real, deliberate
departure from that guidance for exactly this specific ask, not a silent contradiction of it:
the founder's own explicit ask here is to build these tools, not to switch to them by default
immediately. Real, honest sequencing that respects both: build and verify each applet on its own
merits first (this doc's own phased plan), and treat "does this actually replace Alpine's real
busybox in the shipped Pi image" as a separate, later, explicitly-approved decision — matching
`turbogrep`/`turbosed`'s own already-established precedent (real, working, available tools that
are NOT symlinked over the real system equivalents by default, even after verification).

## Real architecture, matching this codebase's own established pattern exactly

Every real PARENA "binary" in this repo already follows the same shape (`turbogrep`,
`editor-demo`, the `parena-selfhost` binary itself): PARENA-compiled logic + a real, hand-written
C host driver providing `main()`/`argv`/stdio. A PARENA-powered busybox is the same shape, not a
new one: `tools/parenabusybox_host.c` provides `main()`, does busybox's own real multi-call
dispatch (inspect `argv[0]`'s basename — or `argv[1]` when invoked directly as
`parenabusybox <applet> ...`, matching real busybox's own dual-invocation convention exactly),
and calls into real PARENA-compiled applet logic in `stdlib/coreutils/*.prn`.

## v0 shipped this pass — 5 real applets, real multi-call dispatch

- `stdlib/coreutils/echo.prn` — `echo-join`: real PARENA string-joining logic (space-separated
  args + trailing newline), not a C stub. `-n` flag (suppress trailing newline) handled in the
  host (flag parsing before the joined-string call, matching this whole stdlib's own convention
  of keeping argv-shaped concerns in the host driver, real logic in PARENA).
- `stdlib/coreutils/basename.prn` — `basename-of`: real path-basename logic (strip directory
  prefix, optional suffix strip) built from `string.prn`'s own existing primitives
  (`char-at`/`substring`), not reimplementing string scanning from scratch.
- `stdlib/coreutils/pwd.prn` — `pwd-format`: takes the real `getcwd(3)` result (a real, new
  `#target inline-c` primitive, `raw-getcwd`, matching this stdlib's own established FFI
  convention) and returns it verbatim — a real, if thin, PARENA function; the actual syscall is a
  one-line inline-c escape hatch, the same real, honest trust boundary every other syscall-backed
  primitive in this stdlib already crosses.
- `true`/`false` — deliberately trivial (exit code only, no real PARENA logic) — included as the
  real, minimal proof that multi-call dispatch itself works correctly for the two simplest real
  busybox applets that exist, before layering real logic on top.

Real, honest v0 boundary: no `sh`, no `mount`, no `init` yet — the three genuinely hard,
load-bearing applets a real boot depends on, each a real, separate, much larger undertaking
(a POSIX-ish shell needs a real parser/job-control/redirection/pipe implementation; `mount`/
`init` need real, privileged syscalls this repo's own sandbox can't even test end-to-end without
root). Not attempted this pass — named directly as the real next phases, not silently deferred.

## Real phased plan

- **Phase 0 (this pass, done)**: multi-call dispatch + 5 real applets (`echo`/`basename`/`pwd`/
  `true`/`false`), real tests, real `make` target.
- **Phase 1**: broaden the trivial-but-real applet set (`cat`, `head`, `wc -l`, `yes`, `sleep`,
  `env`) — each individually simple, each a real, incremental dogfooding win, no new hard
  primitives needed beyond what `io.prn`/`process.prn` likely already cover.
- **Phase 2**: `mount`/`umount` — needs a real `mount(2)`/`umount(2)` FFI wrapper (new primitive,
  `stdlib/os/mount.prn` or similar) — genuinely privileged, untestable end-to-end without real
  root or a real Pi, matching this session's own EmilyOS work's own real constraints.
  `stdlib/emilyos/fsacl.prn`'s own PARENA-mod-backed `setfacl` wrapper (2026-08-25) is real,
  direct, already-proven precedent for wrapping a privileged syscall-adjacent operation in PARENA.
- **Phase 3**: a real, minimal `sh` — the largest, hardest real phase. Real, honest scope
  decision needed before starting, not made here: a full POSIX shell (pipes/job control/globbing/
  here-docs) vs. a genuinely minimal `ash`-lite (sequential command execution, basic `$VAR`
  expansion, no job control) sized to what `/etc/init.d/*` scripts and `/sbin/init` itself
  actually need to run OpenRC — likely the latter, but a real audit of what those scripts use
  should happen before committing to either.
- **Phase 4**: `init` — once `sh`/`mount` exist, a real, minimal init (exec openrc, or replace it
  entirely with a PARENA-native service supervisor — a real, separate, much bigger design
  question, not decided here).
- **Phase 5 (explicitly deferred, a founder call, not decided here)**: whether any of this ever
  actually replaces Alpine's own real busybox package in the shipped EmilyOS Pi image, matching
  `turbogrep`/`turbosed`'s own "available, not default" precedent unless/until explicitly
  approved otherwise.

## Related

- `EmilyOS/docs/NORTHSTAR_DISTRO.md` — the real, direct motivation (Alpine/RPi image build).
- `stdlib/emilyos/fsacl.prn` — real precedent for a PARENA mod wrapping a privileged syscall.
- `tools/turbogrep_host.c`/`examples/editor_main.c` — the real "PARENA logic + C host driver"
  architecture this project reuses exactly, not reinvents.
