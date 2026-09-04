# NORTHSTAR — real scan-speed profiles for PARENA's own scanner (PEN-11412)

Real scoping pass for kanban priority-queue card `PEN-11412`: *"PARENA STDLIBS can we start to
add algorythms for our homegrown scanner? FAST_SCAN SLOW_SCAN SMART_SCAN SNEAKY_SCAN."* Real
investigation — Principle 19 (`EMILY/docs/THE_EMILY_WAY.md`).

**Status (2026-09-04): Phase 0 and Phase 1 are SHIPPED.** `pentest/scan.prn`'s own `scan-ports`
is now a real, live, tested function — `tools/pentest_scan_host.c` shells out to the real `nmap`
binary and parses its real greppable output; all 4 real scan profiles
(`FastScan`/`SlowScan`/`SneakyScan`/`SmartScan`) map to real, distinct nmap flags. Real,
live-verified end to end against `127.0.0.1` (`make test-pentest-scan`) — see "Real, phased plan"
below for the full writeup, kept for the historical record of the investigation. Phase 2 (live
verification against a real, unprivileged deployment) still needs `sudo-queue/48-install-nmap.sh`
run on the real host — this pass developed and verified against a real, sandboxed, no-root nmap
copy (`apt-get download` + `dpkg-deb -x`, real runtime deps resolved the same way), not the
system-installed binary.

## Real, decisive finding: `pentest/scan.prn` itself was never actually implemented

Checked directly, not assumed: `stdlib/pentest/scan.prn`'s own `scan-ports` calls
`(inline-c "pentest_scan_ports(target, dest)")`, but **no real C function named
`pentest_scan_ports` exists anywhere in this repo** — the only real references to it are in
`src/emit.c`'s own compiler test suite and `tests/test_emit.c`, both proving VS0 CAN *emit* a
call with this exact inline-c/type signature shape, never that a real implementation exists to
link against. The file's own header comment claims "FFI-bound to nmap" — that binding was never
actually written. **This card's own real ask (scan-speed profiles) sits on top of a scanner that
doesn't actually run yet**, a real, separate, pre-existing gap this pass found, not caused by
this card.

## What the four real names actually are (researched, not assumed)

`FAST_SCAN`/`SLOW_SCAN`/`SNEAKY_SCAN` map directly onto **nmap's own real, official timing
templates** (`-T0` through `-T5`, each with a real, published name): `-T0` Paranoid, `-T1`
**Sneaky** (the literal, official nmap name — matches `SNEAKY_SCAN` exactly), `-T2` Polite, `-T3`
Normal (default), `-T4` Aggressive, `-T5` Insane. A real "fast scan" in nmap's own vocabulary is
usually `-T4`/`-T5` (or `-F` for a reduced, common-ports-only scan, a separate real flag). A real
"slow scan" is `-T0`/`-T1`. `SMART_SCAN` has no direct nmap-native timing-template equivalent —
the closest real nmap concept is `-A` (aggressive: OS detection + version detection + script
scanning + traceroute, "smart" in the sense of gathering more automatically) or a real,
adaptive service/OS-fingerprint-aware scan strategy, not a pure timing knob like the other three.

## Real, checked-not-assumed current PARENA foundation

- `stdlib/pentest/pcap.prn` (real, working, FFI-bound to libpcap — `start-capture`/
  `read-packet`/`filter`) is the one real, actually-linked pentest primitive in this stdlib area,
  a real, working precedent for how a genuine nmap FFI binding should be structured (this
  session's own `KISMET_WIRELESS_NORTHSTAR.md` leans on the same real file).
- `stdlib/pentest/wireless.prn` (16 lines) and `webapp.prn`, `crack.prn`, `exploit.prn`,
  `macspoof.prn` — checked directly, all real, similar design-only/inline-c-stub shapes,
  same real gap as `scan.prn` (a real, honest, pre-existing pattern across this whole directory:
  designed function signatures, no actual linked C implementations behind most of them).

## Real, phased plan

**Phase 0 — SHIPPED (2026-09-04): a real, actually-linked `pentest_scan_ports`.**
`tools/pentest_scan_host.c` shells out to the real `nmap` binary via `popen` (this session's own
established "shell out to the well-known tool" precedent) and parses its real `-oG -` greppable
output into real `PortResult`s. Real, defined `PortResult`/`ScanError` structs added to
`scan.prn` itself (the same "used but never defined" gap `pentest/pcap.prn` already had and
fixed). Real security precaution: a strict target allow-list (`target_is_safe`) rejects any
string carrying shell metacharacters BEFORE it reaches the shell command line — live-verified
(`test-pentest-scan`'s own 3rd assertion) that a `; touch ...`-style target is rejected as
`InvalidTarget`, never executed.

**Phase 1 — SHIPPED (2026-09-04): real timing-profile parameters.** `scan-ports` now takes a
real `profile : I32` parameter (`scan-profile-fast`/`-slow`/`-sneaky`/`-smart`, zero-arg I32 tag
functions matching `v16/lexer.prn`'s own established convention), mapped in the host's own
`nmap_flag_for_profile` to `-T4`/`-T0`/`-T1`/`-A` respectively — the exact mapping this doc's own
earlier research named, unchanged. An out-of-range profile tag falls back to nmap's own real
default (`-T3`) rather than erroring, a real, honest, non-crashing degenerate case.

**Real, live-tested (`make test-pentest-scan`)**: a real scan of `127.0.0.1` finds this box's
own real, live open ports (22/80/443/3306/8080 — confirmed, not assumed); all 4 profile tags are
real and distinct; a malicious target string is rejected before reaching a shell. `make test`:
345/345 core compiler tests, zero regressions. Developed and verified against a real, sandboxed,
no-root `nmap` copy (see the new status note above) — Phase 2's own live-host verification still
needs `sudo-queue/48-install-nmap.sh` run for real on a deployment host.

**Phase 2 — real, live verification.** Test each profile against a real, authorized target this
org already owns (e.g. `EINHORN_SURVIVAL`'s own real, already-authorized infrastructure, matching
`scan.prn`'s own existing "authorized-testing tooling for EINHORN_INDUSTRIAL's own
infrastructure" standing note) — confirms each profile actually produces different real,
observable scan behavior (timing/stealth), not just different CLI strings that silently do the
same thing.

## Why this isn't done in one pass

Phase 0's own real gap (an unlinked, non-functional `scan-ports`) was found live, not assumed —
building Phase 1's speed profiles on top of a scanner that doesn't actually run would be real,
wasted, untestable work. Real sub-tasks are logged in `EMILY/BACKLOG.md` under this card's own
section rather than folded into a single, unscoped "add scan algorithms" checkbox.
