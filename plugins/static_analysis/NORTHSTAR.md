# static_analysis — NORTHSTAR: the reverse engineer's workbench

Founder real-time, 2026-10-14: the toolkit must cover the full set of affordances a working
reverse engineer expects from a modern interactive disassembler/decompiler, so that an arbitrary
`.exe` can be taken apart and understood end to end — and **the hexes must be visible**.

This document describes the *affordances* — what a person sitting in front of the tool can do —
not an implementation checklist. Each section ends with an honest **Today** line (what
`plugins/static_analysis` really does as of this commit) so the gap stays visible.

Principle: **every view is a projection of one database.** Bytes, disassembly, each IR tier,
pseudo-code, types, names and comments all address the same offsets; selecting anything anywhere
selects it everywhere. Nothing is a one-off report.

---

## 1. See the bytes

The ground floor. If the bytes aren't on screen, nothing above them can be trusted.

- **Hex view**: offset gutter, hex columns, ASCII/UTF-16 side panel, configurable row width and
  grouping (1/2/4/8 bytes), endianness toggle, file offset ↔ virtual address ↔ RVA switch.
- **Selection is linked**: click a byte → the instruction, IR node, pseudo-code line and
  containing section/segment light up; click any of those → the exact bytes highlight. A decoded
  instruction shows its **raw encoding broken into prefix / opcode / ModRM / SIB / disp / imm**.
- **Byte-level edit and patch**: overwrite bytes, NOP-out a range, assemble text into bytes at
  an address, flip a conditional jump; patches are layered over the original, listed, revertible,
  and exportable as a patched binary or a diff.
- **Highlight and annotate bytes**: colour ranges, bookmark offsets, attach comments.
- **Search**: byte patterns with wildcards, ASCII/UTF-8/UTF-16 strings, immediates, regexes,
  instruction-sequence patterns; results are a clickable list, not a one-shot dump.
- **Entropy and structure strip**: a whole-file overview bar (sections, entropy, code vs data vs
  strings vs zero-fill) for spotting packed/encrypted regions at a glance.

**Today:** the lifter reads ELF64/PE32+/raw bytes and decodes x86-64; there is no hex viewer, no
raw-encoding breakdown, no patching, no search UI. Byte offsets are not yet carried through to
the IR tiers.

## 2. Understand the container

- Headers parsed field by field, each field a navigable, annotated struct overlay on the hex.
- Sections/segments, permissions, entry points, TLS callbacks, exception tables.
- **Imports and exports** with library, ordinal and hint; delay-loaded and forwarded entries.
- Relocations, debug directories, resources (icons, manifests, version info, dialogs, embedded
  files extractable as bytes), overlay/appended data, signatures.
- **Strings**, with encoding detection and cross-references back to the code that uses them.
- Packer/compiler/language fingerprints; section-entropy and import-table anomalies surfaced as
  hints, not verdicts.

**Today:** section/segment and symbol/import basics sufficient to find functions. No resources,
relocations, TLS, exception tables, or strings pass.

## 3. Disassemble — linear and recursive

- Linear sweep **and** recursive descent, with the two reconcilable when they disagree.
- **Data vs code is a human-correctable decision**: mark bytes as code, data, array, string,
  struct, or padding; the analysis re-runs from the correction.
- Function discovery from entry points, exports, vtables, call targets, prologue signatures,
  exception tables; manual create/delete/redefine of function boundaries.
- Basic blocks and a **control-flow graph** that can be viewed as graph, linear listing, or
  split; overview minimap; collapse/expand, colour by edge kind (true/false/unconditional/jump
  table/exception), highlight a path.
- Jump-table and switch recovery; indirect-call target sets; tail calls; no-return detection;
  thunks and import stubs recognised.
- **Cross-references** both ways, with kind (call/jump/data read/write/address-taken/string).
- Call graph and callers/callees trees, navigable to arbitrary depth.
- Multiple architectures and calling conventions, user-overridable per function.

**Today:** x86-64 integer subset, recursive-descent function discovery from symbols/entry,
CFG per function. No jump-table recovery, no SSE/x87/string ops, no ARM, no manual redefinition,
no xref database.

## 4. Read the same function at several levels

The central idea: one function, several **lifting tiers**, each a faithful, navigable view.

| Tier | What the reader sees |
|------|----------------------|
| Disassembly | the machine's own words |
| Low-level IR | every register, flag and stack effect explicit; architecture-neutral |
| Mid-level IR | stack slots promoted to variables, flags folded into conditions, calls with arguments |
| SSA form of either | every variable assigned once; merge points explicit; def-use chains one click away |
| High-level IR / pseudo-code | loops, conditionals, switches, member access, readable expressions |

Affordances inside the tiers:
- Switch tier in place without losing your spot; **side-by-side** any two tiers.
- Hover a variable → every definition and use, across SSA versions, highlighted.
- Show the *reason* for a simplification: why a branch was folded, which constant reached here.
- Per-instruction **"what does this lift to"** drill-down, down to the raw bytes (§1).
- Toggle optimisation passes on/off to see intermediate states; compare before/after.
- Flow-sensitive facts surfaced: possible values, ranges, constant sets, aliasing, stack depth.

**Today:** this section is the core of what exists — LLIL, MLIL, HLIL, SSA with φ-nodes and
def-use chains, constant propagation/folding, DCE, branch folding, structuring. Missing: value-set
ranges, alias analysis, pass toggles, explanation provenance, linked UI.

## 5. Decompile to something a human reads

- Structured pseudo-code: `if/else`, `while`, `for`, `do-while`, `switch`, `break/continue`;
  `goto` only where structure genuinely can't be recovered, and flagged.
- Expression reconstruction with sensible precedence, inlined single-use temporaries, folded
  constants, recognised idioms (division by multiplication, bit tricks, `memcpy`-like loops).
- Local variable naming, retyping and **splitting/merging** by hand, with the decompile refreshing.
- Indentation-faithful, copyable, diff-able across edits.
- Clear marking of uncertainty: unresolved indirect calls, assumed argument counts, guessed types.

**Today:** structured output with the above control constructs and goto fallback. No switch
recovery, no idiom recognition, no manual variable split/merge, no uncertainty markers.

## 6. Types, structs and signatures

- A **type library**: primitives, pointers, arrays, structs, unions, enums, function pointers,
  typedefs; import from headers; system/platform libraries preloaded.
- Apply a type to a variable, global, argument, return value, or memory range; the decompile and
  field accesses (`p->len`) update everywhere.
- **Struct recovery from usage**: infer fields from offsets/strides observed across functions;
  propose a layout; let the human accept, rename, resize.
- Function signatures and calling-convention overrides, including variadics and user-defined
  conventions. Propagation of types through calls, returns and globals.
- C++/Rust/Go/Swift name demangling; RTTI and vtable recovery; class hierarchies as a view.

**Today:** none — no type inference; fields print as `p->field_N`.

## 7. Names, comments, bookmarks — the analyst's memory

- Rename anything: functions, variables, labels, globals, struct fields; propagates everywhere.
- Comments at address, function, and variable scope; tags with categories; bookmarks.
- Undo/redo for **every** analyst action; named snapshots; an action history one can read.
- A **database file** that persists all of the above and reloads without reanalysis; mergeable
  between analysts, with conflicts shown instead of silently resolved.

**Today:** analysis is recomputed from the binary each run; nothing persists.

## 8. Navigate at scale

- Go-to anything: address, symbol, string, xref, bookmark; fuzzy command palette over all actions.
- Back/forward navigation history; split panes, tabs, detachable views.
- Symbol list, function list (sortable by size, complexity, xrefs, entropy), string list,
  import/export list, type list — each filterable and linked.
- Triage views: "largest functions", "functions calling X", "functions touching this global",
  "unreferenced code", "functions that never return".
- Diff two binaries or two versions of one: matched/changed/added functions and the byte/IR deltas.

**Today:** programmatic API only (`lift_function`, `to_ssa`, `decompile`); no navigation UI.

## 9. Make it dynamic without leaving the page

- Attach an emulator or a live debugger: set breakpoints at an address *on any tier*, step by
  instruction or by IR node, watch registers/memory/flags, and see the same linked views update.
- Concrete execution to **resolve what static analysis can't**: decrypt a string table, unpack a
  stub, take an indirect jump — results feed back into the database as annotations.
- Taint and symbolic helpers: "which inputs reach this comparison", "what value makes this
  branch go the other way".
- Trace import/replay: load a recorded trace and overlay hit counts and values on the listing.

**Today:** none.

## 10. Scriptable and extensible — the plugin surface

- Everything the UI can do is available to scripts: iterate functions, blocks, instructions, IR
  nodes at any tier, read/write bytes, annotate, patch, query xrefs, run passes.
- Write **new analyses as passes** that run on any tier and publish results (tags, comments,
  highlights, new IR views); write architecture lifters, file-format loaders, and calling-convention
  definitions as plugins.
- **Workflows**: a configurable, orderable pipeline of analysis passes, per binary or per project,
  with the ability to insert a custom step or replace a stock one.
- Headless/batch: run the same engine over a corpus from the command line and emit JSON.
- Plugins authored in PARENA itself, using `parena/plugin`, `parena/buffer`, `parena/events`,
  `parena/ui`, so the extension surface is the language's own.

**Today:** pure-Python library API, headless; headless batch works (800 functions stress run). No
workflow/pass registry, no plugin hooks into a UI.

## 11. Trust and reproducibility

- Every analysis result carries **provenance**: which pass produced it, from which bytes, with
  which assumptions; analyst edits are distinguished from automatic output.
- Deterministic output: same bytes + same settings → same database, byte for byte.
- SSA/IR **verifiers** run continuously and report violations loudly (already: `ssa.check()`).
- Unknowns are explicit (`unimplemented`, `unresolved`), never papered over.

**Today:** determinism and SSA verification hold; provenance does not exist yet.

---

## Sequencing (proposed — sprint-plan into EMILY/BACKLOG.md)

1. **Bytes first (§1):** hex view with offset/VA/RVA mapping, encoding breakdown, and
   byte→instruction→IR→pseudo-code linkage. Requires every IR node to carry its source byte range.
2. **Persistence + names/comments + undo (§7):** the database, so analyst work survives.
3. **xref database, strings, container depth (§2–3):** make the binary navigable.
4. **Jump tables, more instruction coverage, data/code correction (§3).**
5. **Types and struct recovery (§6)** — biggest lift in decompile quality.
6. **Value-range/alias analysis and explanation provenance (§4, §11).**
7. **Diff, emulation, debugger bridge (§8–9).**
8. **Pass registry / workflows / plugin hooks (§10),** exposing all of the above to PARENA plugins.

Out of scope for this document: GPL/proprietary tool compatibility, UI toolkit choice, and
mod-surface integration (deferred per the repo-level NORTHSTAR).
