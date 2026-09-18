# PARENA

## What this is

A new systems language, built from scratch: S-expression syntax, compile-time region-based
memory safety (no GC, no manual free — region-rank escape checking, `Region(Source) ⪰
Region(Destination)`), linear ownership for native resources, and multiple compilation targets
(C first, then JVM/TypeScript/WebAssembly). Ships with its own editor/plugin API namespace
(`parena/plugin`, `parena/buffer`, `parena/events`, `parena/ui`) as a first-class part of the
design, not bolted on later — see `NORTHSTAR.md` for the full picture, grounded directly in the
real spec document in this repo (`Building Your Own Integrated Development Environment.docx`).

Positioned as GoblinFoxDragon's mod-surface scripting language candidate ("EduScript's scary
older sister" — EduScript is a narrow bytecode VM, PARENA compiles ahead-of-time to native code
with compile-time memory verification), but that integration is explicitly deferred — founder:
"northstar that bitch and build it pure before we have to think about how it plays with EDU
script." Read `NORTHSTAR.md` before assuming anything about mod-surface scope.

**Status: VS0 (the `parena-c` compiler: S-expr parser → region analyzer → C99 emitter) is real,
built, and CI-verified — all five DoD domains pass, see `NORTHSTAR.md`'s own current top status
for the detail. Stdlib and the editor/plugin API build out on top of it now, on their own
separate, ongoing timeline (`STDLIB.md`) — that work is not "done" the way VS0 itself is.**

## Stack

VS0 targets C (the compiler itself and its C99 output). Later targets (JVM 22+/Panama,
TypeScript, WebAssembly) are specified in `NORTHSTAR.md` but not started.

**Build system: Bazel** (`bazel build //...`, `bazel test //tests:test_lexer_parser`,
`bazel test --config=asan //tests:test_lexer_parser` for the sanitizer build) — CI's primary
build path as of 2026-08-20. `bazelisk` (installed via `go install
github.com/bazelbuild/bazelisk@latest`, no sudo) resolves the exact version pinned in
`.bazelversion`. A plain `Makefile` is kept alongside for fast local iteration — see its own
header comment.

## Related Repos

- `GoblinFoxDragon` — `docs2/MOD_SURFACE_NORTHSTAR.md` names PARENA as the not-yet-located
  invented language EduScript would be weighed against; that comparison is real, separate,
  deferred work.
- `EMILY` — RSI loop / backlog coordination for cross-repo work.

## Founder Real-Time Direction

Whenever the founder gives real-time direction — a new ask, a correction, a "can we also..." —
route it through `emily observe -s info "Founder real-time: <summary>"` first, even if it isn't
this repo's usual domain, then sprint-plan it into `EMILY/BACKLOG.md` (`emily backlog curate`,
scoped into a real SECTION/sub-item, not just a one-line log), and only then implement. See
`EMILY/docs/THE_EMILY_WAY.md` Principle 18 ("Pave the Cow Paths").

## Apple Filing Protocol

After any meaningful change, file an Apple:
```bash
emily apples post -t completion -repo PARENA "<title>" "<body with commit hash>"
```
Then mark the item done in `EMILY/BACKLOG.md` and commit.

## CHANGELOG Protocol

After any meaningful change, update CHANGELOG.md:
```bash
emily changelog add PARENA "<what changed>"
# or manually: append a dated bullet under ## YYYY-MM-DD in PARENA/CHANGELOG.md
```

## README Reality — SAGA reconciliation (standing instruction, monorepo-wide)

Founder real-time, 2026-09-18: if a change of yours **substantially changes the claim of this project's core README**,
then per SAGA protocols (`EMILY/docs/SAGA_SYSTEM_AUDIT_2026-07-18.md`, HQ-SPEC-DOC-102: intent ↔ claim ledger ↔ reality)
you **must update `README.md` in the same unit of work** so it reflects current reality. The README is the project's public
claim; it must not lag behind the code.

- **When it applies:** a capability is added or removed; status moves ("design only" → "working", "planned" → "shipped");
  the stack, build, run or install steps change; a claim in the README is now false or stale; or you add a **meaningful,
  genuinely interesting piece of kit** (a new tool, engine capability, protocol, pipeline, game system). For that last case
  especially: put it in the README — what it is, how to run it, and its honest status and limits.
- **When it does not:** ordinary fixes, refactors and small features that leave the README's claims true.
- **How:** re-read the README against what you just changed; fix or delete stale lines (including "not built yet" notes that
  are now built); verify any new claim by actually running it, and mark anything untested as untested; commit the README
  with (or immediately after) the change, and mention it in the CHANGELOG entry.

## Frame-Break Reframing

Founder-sourced prompting technique (REDGARDEN/NORTHSTAR.md §28, full origin in
REDGARDEN/docs2/MULTI_AGENT_RD_RESEARCH_NOTES.md §5): given a request, name the underlying
structural/systemic pattern it's one instance of — one level of abstraction up — as an added
lens during planning/triage/judgment calls. Use it to spot the general case behind a specific
ask. It augments judgment, it does not replace doing the work: direct, concrete execution of
the literal task asked for still happens every time.

## Commit Protocol (standing instruction)

Always commit and push completed work immediately — don't wait to be asked. This is the default for every repo in this monorepo.

Every commit — human-written or produced by automated code paths (git-commit helpers in emily-agent, emily.cli, IDUNA handlers, etc.) — must carry the active `emily session` fingerprint as a `session: <tag>` trailer (blank line, then the trailer). This was silently missing from several independently-implemented automated commit helpers across the monorepo until an audit on 2026-08-10 (founder, real-time: "where in the fuck is my llm session id anywhere"). If you add a new automated git-commit code path anywhere, wire in the session tag the same way — don't assume an existing helper already does it.
