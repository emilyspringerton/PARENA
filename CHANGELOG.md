## 2026-08-20
- Migrated to Bazel as primary build system (bazelisk installed, MODULE.bazel/BUILD.bazel/.bazelrc with a real --config=asan, CI updated, verified green). Extended stdlib design: dataframe (pandas-equivalent), nn/tokenizer/sort + io/read-floats (grounded in porting the real gpt2-alpine-c source). (sess-20260820-0649-a3f19d93)

- VS0 lexer + parser + CLI implemented, satisfying NORTHSTAR.md's Definition of Done domain 1 (32 unit tests, balanced + imbalanced S-expressions). CI green (build, test, ASan/UBSan, real test.prn smoke test). (sess-20260820-0649-a3f19d93)

