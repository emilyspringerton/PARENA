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

Real, honest v0 boundary: no `mount`, no `init` yet — genuinely hard, load-bearing applets a real
boot depends on, real, separate, larger undertakings needing privileged syscalls this repo's own
sandbox can't even test end-to-end without root. Not attempted this pass — named directly as real
next phases, not silently deferred.

## Phase 3 shipped this pass (same day, founder real-time: "zsh etc build it prn") — a real, minimal `sh`

Real, honest scope decision made BEFORE writing anything, grounded in a real audit rather than a
guess: read the actual OpenRC scripts in this session's own already-built EmilyOS Alpine rootfs
(`/etc/init.d/hostname`, `/etc/init.d/bootmisc`) and found they use real shell FUNCTIONS,
`if`/`[ ]` conditionals, and `${var:-default}` parameter expansion — genuinely closer to a full
POSIX shell than a "run sequential commands" toy. Running those real scripts is explicitly NOT
this pass's goal (a real, separate, much later milestone, named honestly rather than oversold);
this v0 is a real, working, useful minimal shell in its own right.

`stdlib/coreutils/sh.prn` ships real PARENA logic: `tokenize-line` (a real, quote-aware
word-splitter — single/double quotes suppress whitespace/`;` splitting and are stripped from the
output word; `;` is itself both a splitter and its own emitted token, letting the host split
sequential commands) and `expand-word` (real, minimal whole-word `$VAR` expansion via a new
`getenv(3)`-backed `raw-getenv` primitive). `tools/parenash_host.c` does the real process
management every actual shell needs: a REPL loop (interactive `$ ` prompt on a real tty, silent
script-mode otherwise — `parenash < script.sh` works too), `fork`/`execvp`/`waitpid` for real
external commands, and three real builtins that must run in the PARENT process (a forked child
could never affect the shell's own cwd/environment) — `cd`, `export NAME=value`, `exit [code]`.

Real, live-found VS0 emitter gap, named directly and worked around rather than silently
papered over: a `loop` whose own terminal (non-`recur`) branch resolves to `Unit` produces an
invalid `void __loop_result_N` C local (a real, confirmed `variable ... declared void` gcc
error) — `tokenize-line`'s own terminal branch gives itself a real, dummy `""` tail value instead
(the function's real result, `words`, is read from the enclosing `let` after the loop, never the
loop's own value) — a real, separate emitter bug, not fixed in `src/emit.c` itself this pass.

New `make parenash`/`test-parenash` targets. Real end-to-end test coverage
(`tests/test_parenash.c`) pipes real script text into the ACTUAL compiled binary via `popen`
(matching `test_parenabusybox.c`'s own "invoke the real binary" discipline) — 9 real assertions
covering quoting, `;`-sequencing, `$VAR` expansion via a real `export`, all 3 builtins (including
proving `cd` genuinely mutates the shell's own parent-process cwd, not a throwaway child's), and
the standard `127` "not found" exit code — all pass. `make test`: 347/347, zero regressions.

Real, honest v0 boundary, named directly: NO pipes, NO redirection, NO functions,
NO test builtin, NO job control/backgrounding, NO mid-word `$VAR` expansion (only a word that's
ENTIRELY `$NAME`), NO `${VAR}` brace form. This is a real, useful toy shell — not yet capable of
running the real OpenRC scripts the audit above found, which is the actual, much larger remaining
milestone before `sh` could ever be part of a real EmilyOS boot chain.

## Phase 3b shipped same day (founder real-time: "continue") — real `if`/`then`/`else`/`fi`

Real, deliberate architecture choice named directly: control-flow structure recognition
(`if`/`then`/`else`/`fi`) lives in `tools/parenash_host.c` as a plain recursive-descent walk over
the already-tokenized word array, NOT in `stdlib/coreutils/sh.prn` — PARENA's own real strength
in this package is string/token processing (tokenizing, quoting, `$VAR` lookup), and imperative
control-flow branching is a more natural fit for the same C layer that already does process
management. `exec_range(words, start, end)` recognizes a single-level `if COND; then BRANCH1;
[else BRANCH2;] fi` (COND/BRANCH1/BRANCH2 may themselves contain further `;`-separated commands,
handled by recursing back into `exec_range`), otherwise splits off and runs one simple command up
to the next top-level `;` and continues with the remainder. Real, honest v0 boundary: no `elif`,
no nesting (the first `then`/`fi` found closes the nearest-enclosing `if` — correct for the real,
common non-nested case, a real, named limitation for an `if` nested inside another `if`'s own
condition or branch).

Confirmed live before shipping that `test`/`[` already work today via the existing plain
`execvp` fallback (real system binaries, not shell builtins) — `if test -f /etc/passwd; then
echo has-passwd; else echo missing; fi` runs correctly end to end.

Real, live-found bug caught and fixed before this could ship as broken: a trailing `;` inside the
condition range (the real, common shape `if false; then ...` produces) fed into an empty tail
recursion whose own base case returns a fixed `0`, silently DISCARDING the real exit status just
computed and always taking the then-branch regardless of the condition's real result — confirmed
live via `if false; then echo yes; fi` wrongly printing `yes`. Fixed: a trailing `;` with nothing
meaningful after it now returns the already-computed real status directly instead of blindly
recursing into an empty range. 5 new real end-to-end assertions in `tests/test_parenash.c`
(covering the then-branch, no-else skip, else-branch, "code after `fi` still runs," and a
multi-command then-branch) — including one that names the exact bug just fixed and would have
caught it — all pass, alongside the original 9. `make test`: 347/347, zero regressions.

## Phase 3b continued, same day (founder: "keep working on Emily os busybox and all of that") — real `elif` + `${VAR:-default}`

**`elif`**: `exec_if_chain(words, start, end)` treats a real `elif` identically to a fresh `if`
(both find their own `then`, then the nearest of `elif`/`else`/`fi` as the branch boundary) —
when the boundary is another `elif`, it recurses right back into `exec_if_chain` starting at that
word, correctly sharing the SAME outer `fi` (there is exactly one, closing the whole chain) rather
than searching for a second one. Live-verified: a taken `elif` runs its own branch, a later
also-true `elif` never runs once an earlier condition already won, a real multi-`elif` chain
resolves to the first true condition among them, falling through to `else` when all are false, and
code after the chain's own `fi` still runs. Real, honest v0 boundary unchanged: still no nesting.

**`${VAR:-default}`/`${VAR-default}`**: real PARENA logic in `stdlib/coreutils/sh.prn` —
`find-dash-index` (a real, tail-recursive scan for the first `-`, safe because POSIX variable
names never contain one) plus `expand-param` (splits on that dash into `varname`/`default-val`,
looks up the real environment value, substitutes the default when empty). Real, honest,
DELIBERATE simplification named directly: since `raw-getenv` already can't distinguish "unset"
from "set but empty" (its own pre-existing v0 boundary), the plain `-` form is treated IDENTICALLY
to `:-` here rather than silently claiming a POSIX distinction this shell can't actually make.
`expand-word` also grew a plain `${VAR}` (no default operator) passthrough case. Real, honest,
STILL not attempted: no `:=`/`:+`/`#`/`%` operators, and no NESTED `${...}` inside a default value
(the real, live shape `${wipe_tmp:=${WIPE_TMP:-no}}` this session's own audited `bootmisc` script
actually uses) — a real, separate, later extension.

9 new real end-to-end assertions (5 for `elif`, 4 for parameter expansion) — all pass, alongside
the original 14. `make test`: 347/347, zero regressions.

## Real phased plan

- **Phase 0 (done)**: multi-call dispatch + 5 real applets (`echo`/`basename`/`pwd`/`true`/
  `false`), real tests, real `make` target.
- **Phase 1**: broaden the trivial-but-real applet set (`cat`, `head`, `wc -l`, `yes`, `sleep`,
  `env`) — each individually simple, each a real, incremental dogfooding win, no new hard
  primitives needed beyond what `io.prn`/`process.prn` likely already cover.
- **Phase 2**: `mount`/`umount` — needs a real `mount(2)`/`umount(2)` FFI wrapper (new primitive,
  `stdlib/os/mount.prn` or similar) — genuinely privileged, untestable end-to-end without real
  root or a real Pi, matching this session's own EmilyOS work's own real constraints.
  `stdlib/emilyos/fsacl.prn`'s own PARENA-mod-backed `setfacl` wrapper (2026-08-25) is real,
  direct, already-proven precedent for wrapping a privileged syscall-adjacent operation in PARENA.
- **Phase 3 (v0 done)**: a real, minimal `sh` — sequential `;`-separated commands, quote-aware
  tokenizing, whole-word `$VAR` expansion, `cd`/`export`/`exit` builtins.
- **Phase 3b (v0 done: `if`/`then`/`else`/`fi`, `elif`, and `${VAR:-default}` all shipped)**:
  real conditionals — `test`/`[` already work via the plain `execvp` fallback, confirmed live.
  **Not yet done**: nested `if`, real shell FUNCTIONS (`name() { ... }`), `:=`/`:+`/`#`/`%`
  parameter-expansion operators, and nested `${...}` inside a default value — real, concrete,
  named next slices, sized against this session's own real audit of `/etc/init.d/hostname`/
  `bootmisc` rather than guessed at. Real shell FUNCTIONS are the single largest remaining piece
  — both audited real scripts define at least one (`depend() { ... }`) — and still the real gate
  before this shell could run actual OpenRC scripts.
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
