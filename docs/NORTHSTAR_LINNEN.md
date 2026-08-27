# Linnen — PARENA's own native UI widget framework

Founder, real-time: "we need a widget at the bottom to open settings" → "figure out the basic
settings? zoom i dunno something relevant we need it using parena react whatever the widget
library framework is called" → "have that framework called linnen" → "northstar it".

## What this is

**Linnen** is the real, native, host-driven UI widget framework PARENA's own editor (and,
longer-term, any other PARENA application with a real screen) is built out of. It already
exists in real, shipped, tested form — `stdlib/editor/widget.prn`'s own `Toggle` type, the first
real widget, is Linnen's first real slice, not a separate thing being folded in. This doc gives
that real, growing system a name and a real scoping story, the same "name it once it's real, not
before" discipline `ladybug`/`EmilyOS`'s own golden docs already follow.

**What Linnen is explicitly NOT**, despite the founder's own "parena react" framing as a way to
point at the *category* of thing (a real widget-library framework, not a hand-rolled one-off):
not a virtual-DOM/reconciliation framework, no diffing, no component tree, no re-render-on-
state-change scheduler. Every real widget in this codebase today is plain data (a real
`defstruct` — `Toggle{x y w h label-on label-off on}`) plus real, explicit, host-called
`render-*`/`*-hit?`/`*-flip` functions the editor's own SDL2 render loop calls directly, once per
frame, matching VS0's own real, current limits (no struct field mutation — `toggle-flip` returns
a real *new* `Toggle`, the caller rebinds its own local, the same functional-update shape
`Buffer`'s own `insert`/`delete-range` already establish) and this whole stdlib's own house style
(`editor/buffer.prn`'s own header comment already documents the identical constraint). "React" is
the founder's own real, useful pointer to the *category* — a real, general-purpose UI widget
toolkit, not yet-another one-off inline control — not a literal architecture match.

## Real, current state

- `Toggle` (`stdlib/editor/widget.prn`): the one real widget type that exists. Real fields:
  position/size, on/off state, two labels. Real functions: `new-toggle`, `toggle-hit?`,
  `toggle-on?`, `toggle-flip`, `toggle-label`, `render-toggle`. Two real callers in
  `examples/editor_main.c`: the auto-indent status-bar control and the file-tree sidebar
  visibility control, both in the hover-reveal bottom bar.
- Real, deliberate scope from `widget.prn`'s own header comment: ONE widget type, not a
  speculative button/slider/checkbox catalog nothing calls yet — "expand the API only when a
  real feature needs it," the same discipline `editor/buffer.prn` already established for its
  own real API surface.
- Everything else UI-shaped in the editor today (the file-tree sidebar's own row list, the
  Spotlight overlay's own query box + result rows) is still real, working, hand-rolled render
  code directly in `examples/editor_main.c` — not yet expressed as real Linnen widgets. Real,
  honest scope note: folding those into real, reusable Linnen widget types is real, separate,
  unstarted follow-up, not attempted by this doc.

## Real next increment: the Settings panel

The founder's own concrete, real trigger for naming this framework at all: "a widget at the
bottom to open settings," with zoom named as the first real, obvious setting ("figure out the
basic settings? zoom i dunno something relevant"). Real, scoped v0:

- A third real bottom-bar button (`Settings`, next to the existing auto-indent/file-tree
  toggles), same real `Toggle`-backed on/off shape those two already use.
- A real floating panel, shown when the Settings toggle is on, matching the Spotlight overlay's
  own real "modal box drawn on top of everything else" precedent (`examples/editor_main.c`'s own
  `spotlight_visible` render block) rather than a new, separate presentation pattern.
- Real v0 content: a Zoom control (current `zoom_percent` displayed as text, real `-`/`+` click
  regions that call the exact same `zoom_percent` adjustment logic the existing Ctrl+scroll/
  Ctrl+±  keybinds already use — one real, shared code path, not two that could drift). Other
  real settings (which the founder's own "i dunno something relevant" leaves open) are real,
  separate, unstarted follow-up, added only once a real, concrete need names them — the same
  "expand only when something real needs it" discipline this whole doc already draws from.
- Real, honest scope: the `-`/`+` zoom buttons are real, hand-rolled click regions (the same
  shape the file-tree sidebar's own row hit-testing and Spotlight's own row hit-testing already
  use), not yet a real, reusable Linnen "Button" or "Stepper" widget type — that generalization
  is real, separate follow-up once a second real caller actually needs one (`widget.prn`'s own
  "one real type, not a speculative catalog" rule applied here too).

## Real open questions, not resolved here

- **Editor-only, or a general PARENA UI toolkit?** Today every real Linnen widget is drawn by
  and lives inside `examples/editor_main.c`'s own SDL2 render loop — nothing about `Toggle`'s own
  real shape is editor-specific, but no second, non-editor PARENA application has tried to use it
  yet to prove that out. Real, deliberately unresolved until a real second consumer exists.
- **Does Linnen ever grow a real declarative/component layer** (something closer to the "react"
  framing) **on top of today's real, imperative, host-called-per-frame shape?** A real, much
  larger design question — not scoped here, and genuinely gated on VS0's own current limits (no
  struct mutation, no closures capturing real editor state cleanly yet) the same way every other
  "not yet" in this doc is.
- **Where do the file-tree sidebar and Spotlight overlay's own hand-rolled render code end up?**
  Real candidates for becoming real Linnen widget types themselves (a real "List"/"Panel" type)
  once there's a second real caller that would actually reuse them — not attempted in the
  Settings-panel v0 above, which deliberately reuses the *pattern* (modal box, click-region hit
  test) without yet extracting a shared type.

## Related

- `PARENA/NORTHSTAR.md` — the language's own top-level scoping doc; this doc is a real,
  narrower sibling for the editor's own UI-widget layer specifically, same relationship
  `EmilyOS/docs/NORTHSTAR_DISTRO.md` has to `EmilyOS/NORTHSTAR.md`.
- `stdlib/editor/widget.prn` — Linnen's own real, current, only implementation.
- `STDLIB.md`'s own `editor/ui` design sketch — a real, separate, still-speculative MOD-FACING
  API surface (third-party plugins setting gutter markers/diagnostics/popups), not the same
  thing as Linnen (the editor's own internal UI-widget layer) — don't conflate the two.
