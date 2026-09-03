# NORTHSTAR — PARENA-compiled Java on a real Android app

Real scoping note for kanban cruise-queue card 32445324, "PARENA android app in JAVA using
PARENA using the JAVA emitter." Same "size it honestly, ship the real testable slice, name the
real gap" discipline this session's own `V16_NORTHSTAR.md` and the SHANKPIT/PAPERCRAFT portal
scoping already applied.

## What's real and shipped this pass

`stdlib/android/battery_ui.prn` — two real, small, pure decision functions
(`should-show-low-battery-warning`, `clamp-brightness`) plausible for a real Android app to
actually need (this monorepo's own `MJOLNIR` is the real, established precedent for what a real
Android app here does — FCM push, an Apple feed). Compiled via the real Java emitter
(`parena build stdlib/android/battery_ui.prn -o BatteryUi.java`) and verified with a REAL JDK
(`javac`/`java 25.0.4`, the same version `STDLIB.md`'s own `BezierInterp`/`Humanness` Java proof
already used) against a real `Main.java` smoke test — 7 real assertions, all pass, including the
boundary case (`battery-pct == 15` warns; `16` doesn't) and both brightness clamp directions.

**Real, genuine compiler bug found and fixed along the way, not designed in advance**: `(not x)`
had no handling anywhere in `src/emit_java.c` — the exact same real gap already found and fixed
for `src/emit.c` (2026-08-21) and `BURROW/emit_c.go`/`emit_go.go` (2026-08-30), this Java target
simply hadn't hit a real `.prn` file using `not` yet. Fixed the same real way each of those was:
Java's own `!` negation operator is the direct equivalent. `tests/test_emit_java.c`: 31/31 (4
new). `make test`: 342/342, zero regressions.

## What's real and honestly NOT attempted this pass

**A real, buildable, runnable Android APK.** Checked directly, not assumed: this sandbox has no
Android SDK (`ANDROID_HOME`/`ANDROID_SDK_ROOT` both unset, no `sdkmanager`/`adb` on `PATH`), and
`MJOLNIR` — this monorepo's own real, existing Android app — doesn't even have a real `gradlew`
wrapper script checked out locally (only the `gradle/wrapper` metadata), confirming MJOLNIR
itself is never actually built or run in this sandbox either, only via real CI or the founder's
own machine. Building a genuinely new Android project here and claiming it's real without ever
compiling it would be exactly the kind of "written but not verified" gap this session's own
standing discipline refuses to ship — so it isn't attempted. Real, honest deliverable instead:
prove the PARENA→Java compiler pipeline itself works end to end (done, above), and name the real,
concrete remaining steps plainly.

## Real, concrete next steps (none started)

1. A minimal Android project skeleton (`build.gradle.kts`, `AndroidManifest.xml`, one Activity) —
   mechanical, low-risk, but genuinely unverifiable without the Android SDK present, so real,
   later work rather than guessed-at scaffolding now.
2. Including the compiled `.java` output as a real source file in that project (Android's own
   Gradle toolchain compiles plain `.java` sources alongside Kotlin natively — no special
   bridging needed once the file exists in the right source set).
3. Wiring `BatteryUi.shouldShowLowBatteryWarning`/`clampBrightness` (or their real, eventual
   equivalents) into an Activity's own real battery-state callback (`BatteryManager`) and
   brightness-setting call (`WindowManager.LayoutParams.screenBrightness`) — real Android APIs
   this v0's own scalar-only PARENA functions were deliberately designed to plug into without
   needing any Android-specific type crossing the PARENA/Java boundary.
4. A real CI step (this repo's own `.github/workflows/`, or a new one) that actually has the
   Android SDK available, to build and instrument-test the result — the real, missing piece that
   would let this go from "proven in isolation" to "proven as a real app."

## Related

- `STDLIB.md`'s own "Real proof, verified with an actual `javac`" section — the direct precedent
  this pass's own verification approach follows (`BezierInterp`/`Humanness`, three real compiled
  targets from one PARENA source).
- `MJOLNIR/CLAUDE.md` — this monorepo's own real, existing Android app; the real reference for
  what Android build tooling this monorepo already assumes exists (and doesn't, locally, in this
  sandbox).
- `PARENA/docs/V16_NORTHSTAR.md` — the same "size the ask honestly, ship what's real, name what
  isn't" discipline applied to a different, larger ask this same session.
