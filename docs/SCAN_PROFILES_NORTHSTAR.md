# NORTHSTAR — real scan-speed profiles for PARENA's own scanner (PEN-11412)

Real scoping pass for kanban priority-queue card `PEN-11412`: *"PARENA STDLIBS can we start to
add algorythms for our homegrown scanner? FAST_SCAN SLOW_SCAN SMART_SCAN SNEAKY_SCAN."* Real
investigation before any code — Principle 19 (`EMILY/docs/THE_EMILY_WAY.md`), no code written
this pass.

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

## Real, phased plan (none started)

**Phase 0 — a real, actually-linked `pentest_scan_ports` (blocking, not optional).** Write the
real C shim FFI-binding `nmap` (either shelling out to the real `nmap` binary and parsing its
real output — this session's own established "shell out to the well-known tool" precedent,
matching `stdlib/shell.prn`'s own real convention — or linking `libnmap`/using nmap's own real
XML output format) and wire it into the runtime so `scan-ports` actually runs. Nothing below this
line is real until this exists — the same "real blocking Phase 0" shape this session found
repeatedly (netcode, level format, HTML/CSS renderer).

**Phase 1 — real timing-profile parameters.** Extend `scan-ports` (or add a real
`scan-ports-profile` variant) with a real `ScanProfile` enum (`FastScan`/`SlowScan`/`SneakyScan`/
`SmartScan`), each mapping to a real nmap CLI flag combination per the research above (`-T4`/
`-T0`/`-T1`/`-A` as the real, concrete starting mapping — refinable once Phase 0 is real and
testable against an actual target).

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
