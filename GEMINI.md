# GEMINI.md — Guidance for Gemini / Antigravity in PARENA

## What This Is

`PARENA` is a systems language built from scratch:
- S-expression syntax
- Compile-time region-based memory safety (`Region(Source) ⪰ Region(Destination)`)
- Linear ownership for native resources
- Multi-target compilation (C99 first, JVM/TS/Wasm planned)
- Built-in editor/plugin API

See `NORTHSTAR.md` and `STDLIB.md` for language specifications and stdlib progress.

## Build and Test (Bazel)

```bash
# Build compiler and stdlib
bazel build //...

# Run core tests
bazel test //tests:test_lexer_parser

# AddressSanitizer build
bazel test --config=asan //tests:test_lexer_parser
```

## Commit and Operating Protocol

- Route founder direction through `emily observe` first.
- Every commit must include the active `session: <tag>` trailer (`emily session current`).
- Commit and push immediately upon verification.
