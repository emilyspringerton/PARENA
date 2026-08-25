# turbogrep / PARENA stdlib — full bottleneck audit

**Status:** real audit, 2026-08-25. Founder: "find all of the technical bottlenecks before we
move to hacks." This is the inventory pass — nothing below is fixed here except where marked;
each item is either confirmed by a real test/measurement or explicitly marked unverified.

## Performance

| # | Bottleneck | Status |
|---|---|---|
| P1 | `read-line` one `read()` syscall per byte | **Fixed** 2026-08-25 — buffered, 3,786x fewer syscalls (PARENA `e706737`) |
| P2 | Literal patterns running the full backtracking matcher | **Fixed** 2026-08-25 — literal fast path, 4x more (PARENA `15dc92c`) |
| P3 | General matcher (`Alt`/`Star`/`Group`) allocates a fresh `Vec` per character position tried | **Open, confirmed architecturally, not measured in isolation.** Real cost for any pattern that isn't a pure literal — everything through `Plus`/`Optional`/`CharClass`/`Anchor` (all unimplemented, see below) would hit this once built. |
| P4 | Multi-branch `Alt`-of-literals (e.g. `"foo\|bar"`) doesn't use the literal fast path | **Open, confirmed by code inspection.** `ast-literal-text`'s `single-branch-literal` only fires for exactly one branch; a real multi-word alternation falls through to the general matcher today. Bounded, natural extension of P2's own fix. |
| P5 | `ast-literal-text` recomputes "is this pattern literal" + rebuilds the flattened string (via `string/concat`, O(n) alloc per call) on **every** `is-match` call, not once per `compile` | **Open, confirmed by code inspection.** For the 213,709-line corpus this is 213,709 redundant small rebuilds of a 4-8 char string — real but almost certainly small next to P3/P4; not measured. |
| P6 | `raw_read_all_impl` (`read-string`) grows its buffer by fixed 4096-byte increments with a full `memcpy` each time — O(n²) for large files | **Open, confirmed by code inspection.** Not exercised by `grep.prn` (uses `read-line` only) — real risk only for `read-string`/`read-floats` callers, none in this stdlib's own real usage today. |
| P7 | Compiler-level: no int/float distinction (every numeric literal is `double`) | **Open, pre-existing, out of scope for turbogrep specifically** — worked around narrowly (`zero-i32`) rather than fixed. A real fix touches the whole compiler and every `.prn` file that relies on the current behavior; flagged in `PARENA/docs/NORTHSTAR_ADA_GNAT_MEMORY.md` already. |

## Correctness / feature gaps

| # | Gap | Status |
|---|---|---|
| C1 | `CharClass` (`[abc]`, `[a-z]`, `[^abc]`) unimplemented | **Confirmed live, dangerous**: `turbogrep '[0-9]' file` with a real digit-containing line returned **zero matches** — real grep found it. Silent wrong answer, no error. Falls to match-node's own catch-all `(_ (vec/new dest))` — same "empty, not an error" shape as an exceeded budget (see P/C below), compounding the silence. |
| C2 | `Anchor` (`^`, `$`) unimplemented | **Confirmed by code inspection** (same catch-all as C1) — not independently re-tested, same real risk class as C1. |
| C3 | `Plus` (`+`) unimplemented | **Confirmed by code inspection**, same catch-all. |
| C4 | `Optional` (`?`) unimplemented | **Confirmed by code inspection**, same catch-all. |
| C5 | Nested `Concat` (inside a `Group`) only exposes its own single best candidate, not a full candidate set | **Known, documented limitation from the closures rewrite** (`regex/pcre.prn`'s own header comment). Real impact needs `Plus`/`Star` interacting through a `Group` boundary — not independently exercisable yet since `Plus` (C3) doesn't exist. |
| C6 | `find`/`find-all`: `Match.groups` always empty, no capture-group support at all | **Confirmed by code inspection** — `find`'s own construction hardcodes `:groups (vec/new dest)`. |
| C7 | `replace` is a stub (`text` returned unchanged, doesn't actually substitute) | **Confirmed, already flagged honestly in the source** — blocked on a real, separate Vec-of-struct elem-type-hint compiler gap (same root cause noted for `build-replaced-string`, see item G below). |
| C8 | No case-insensitive matching (`grep -i` equivalent) | **Confirmed absent** — no flag, no code path anywhere in `regex/pcre.prn`/`grep.prn`. |
| C9 | No backreferences (`$1`, `\1`) | **Confirmed absent**, downstream of C6 (no captures to reference). |
| C10 | Binary file handling | **Unverified.** Real grep detects and skips (or warns on) binary files; `turbogrep`/`io.prn` have no such check — an arbitrary binary file would be read and searched as if it were text. Not tested against a real binary file this pass. |
| C11 | `MatchBudget` (100,000 steps, hardcoded in `grep.prn`) silently reports "no match" instead of an error when exceeded | **Partially tested, inconclusive.** A 400,006-byte single-line file with a literal pattern matched correctly — but that request went through the new literal fast path (P2), which doesn't consult the budget at all. The ORIGINAL concern (a long line through the general `Alt`/`Star` matcher exhausting the budget and silently reporting no-match) is **not actually re-tested** — flagged as still open/unverified, not resolved by this test. |
| C12 | `sed.prn` — real compile failure, unfixed | **Confirmed**: `unknown identifier 'line' at line 28` when built alongside its real dependencies (string/array/io/regex-syntax/regex-pcre). Never compile-tested before this audit. |
| C13 | `awk.prn` — real, distinct compile failure, unfixed | **Confirmed**: `unknown identifier 'line' at line 30`, same class of error as C12 but a separate location — not investigated further. |

## Compiler-level correctness hazards (found this session, not turbogrep-specific)

| # | Hazard | Status |
|---|---|---|
| G1 | Struct/enum registry is flat, not module-scoped — `regex/nfa.prn` and `regex/pcre.prn` both declare a `Regex` struct with different fields; building both together silently shadows one | **Confirmed, worked around** (never build both engines together), **not fixed**. Real risk for any future multi-engine or multi-module build that happens to reuse a common type name. |
| G2 | Vec-of-struct elem-type-hint gap for a plain local (not a struct field, not an `&mut` parameter) | **Confirmed** — root cause of C7 (`replace`). The existing hint mechanism only covers struct fields and `&mut`-typed parameters; a `let`-bound local `(Vec SomeStruct)` from a function's own return value gets no hint, so `vec/get`+`deref` on it produces invalid C (`void*` dereference). Real, general fix would unblock more than just `replace`. |
| G3 | `#target` bodies are trusted verbatim with zero auto-boxing/type-checking | **Working as designed, not a bug** — but the real source of most of this session's own "real, silently wrong" findings (io.prn's original raw-value-instead-of-boxed-Result bugs). Listed here as a standing hazard class, not a specific defect: any future hand-written `#target` body can reintroduce this exact class of silent error. |

## What this audit does NOT cover

Not independently tested this pass: `net/*.prn`, `pty.prn`, `thread.prn`, `crypto/*.prn`, or any
other stdlib file outside the direct `grep`/`turbogrep` dependency chain (`string`, `array`, `io`,
`regex/syntax`, `regex/pcre`, `grep`, and the two attempted-but-failing `sed`/`awk`). Real,
plausible surface area given this session's own track record (every file actually gcc-verified
so far needed at least one real fix) — flagged as unknown, not assumed clean.

## Priority read, for the founder's own call before picking the next "hack"

Highest real risk, ranked by "how likely is this to produce a silently wrong answer a user would
trust": **C1 (CharClass) > C11 (MatchBudget, unverified) > C2/C3/C4 (Anchor/Plus/Optional) > C10
(binary files)**. Highest real remaining perf lever: **P3/P4** (general matcher + multi-literal
Alt), likely bigger than P5/P6/P7 combined for real-world grep usage. G1/G2 are compiler-level and
would each unblock multiple stdlib gaps at once rather than one at a time.
