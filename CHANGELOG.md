## 2026-08-20
- 新增 mapbuilder(tools/layout/template)+world 資料模型、pty/shell/ssh/crypto 標準函式庫(具體 dogfood 進 PITVIPER 路徑)、gfd/browser(FFI 綁定真實瀏覽器引擎)、NORTHSTAR 記錄 V8 JIT 長期構想 (sess-20260820-0649-a3f19d93)
- 新增 ringo(matplotlib 對應套件,依 array + sdl2),真實 .prn 原始碼已通過 parena parse 驗證 (sess-20260820-0649-a3f19d93)
- 標準函式庫大量擴充:regex 全引擎家族、grep/sed/awk、vec/map、net tcp/udp/http、內建 sdl2、editor/*、otp (Erlang 風格 + scheduler)、media (audio/codec/stream)、sql 建構區塊、gpt2 移植數值套件全數轉為真實 .prn 原始碼(parena parse 驗證通過);NORTHSTAR 加入創辦人使命宣言;PITVIPER 修復 WSL 誤判 bug (sess-20260820-0649-a3f19d93)
- STDLIB.md: multi-engine regex family (regex/syntax, regex/nfa linear-guarantee, regex/pcre full backtracking, regex/posix, regex/glob) plus grep/sed/awk built on top; new expr + io/read-line dependencies; explicit batteries-included scope note (sess-20260820-0649-a3f19d93)
- Migrated to Bazel as primary build system (bazelisk installed, MODULE.bazel/BUILD.bazel/.bazelrc with a real --config=asan, CI updated, verified green). Extended stdlib design: dataframe (pandas-equivalent), nn/tokenizer/sort + io/read-floats (grounded in porting the real gpt2-alpine-c source). (sess-20260820-0649-a3f19d93)

- VS0 lexer + parser + CLI implemented, satisfying NORTHSTAR.md's Definition of Done domain 1 (32 unit tests, balanced + imbalanced S-expressions). CI green (build, test, ASan/UBSan, real test.prn smoke test). (sess-20260820-0649-a3f19d93)

