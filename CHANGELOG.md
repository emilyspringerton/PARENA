## 2026-08-20
- C emitter 加入算術/比較/if/一般函式呼叫,朝自我託管邁進一步。CI 已確認綠燈(run 32384381363) (sess-20260820-0649-a3f19d93)
- pitviper/tiling(真實 i3wm 互動模型)stdlib 設計 (sess-20260820-0649-a3f19d93)
- pentest/* 六個套件(Kali 對應工具集)、idvault + pitviper/expand(刻意淺層設計)、依賴關係圖完整重新整理(57 個套件) (sess-20260820-0649-a3f19d93)
- 六個新 stdlib 套件設計:media/tts(F5-TTS sidecar)、pitviper/quicklook(macOS Quick Look 互動模型)、net/vpn+packetradio+mesh(WireGuard/AX.25+APRS/Meshtastic)、cli+config(Cobra/Viper 對應,自我託管的真實第一目標) (sess-20260820-0649-a3f19d93)
- VS0 domain 4(記憶體驗證)完成 + CI 修復(ASan/Valgrind 物件檔混用 bug);新增 pitviper/protocol + compress/lz4 + profile + staticanalysis + git 五個新 stdlib 套件設計 (sess-20260820-0649-a3f19d93)
- 新增 firefly(基礎測試函式庫,Go testing.T 形狀)+ firefly/gomega(matcher chain)+ scarab(BDD + test runner,Ginkgo 形狀),甲蟲命名;開始為 vec/map/world 寫真實測試 (sess-20260820-0649-a3f19d93)
- VS0 domain 2(region analyzer)真正實作完成:單一遍歷 scope 追蹤器,通過 NORTHSTAR DoD 自己的正例/反例驗收(逐字相同的錯誤訊息、正確行號),8 個測試涵蓋真實邊界案例,ASan/UBSan 乾淨,CI 綠燈已用 API 直接確認 (sess-20260820-0649-a3f19d93)
- 新增 mapbuilder(tools/layout/template)+world 資料模型、pty/shell/ssh/crypto 標準函式庫(具體 dogfood 進 PITVIPER 路徑)、gfd/browser(FFI 綁定真實瀏覽器引擎)、NORTHSTAR 記錄 V8 JIT 長期構想 (sess-20260820-0649-a3f19d93)
- 新增 ringo(matplotlib 對應套件,依 array + sdl2),真實 .prn 原始碼已通過 parena parse 驗證 (sess-20260820-0649-a3f19d93)
- 標準函式庫大量擴充:regex 全引擎家族、grep/sed/awk、vec/map、net tcp/udp/http、內建 sdl2、editor/*、otp (Erlang 風格 + scheduler)、media (audio/codec/stream)、sql 建構區塊、gpt2 移植數值套件全數轉為真實 .prn 原始碼(parena parse 驗證通過);NORTHSTAR 加入創辦人使命宣言;PITVIPER 修復 WSL 誤判 bug (sess-20260820-0649-a3f19d93)
- STDLIB.md: multi-engine regex family (regex/syntax, regex/nfa linear-guarantee, regex/pcre full backtracking, regex/posix, regex/glob) plus grep/sed/awk built on top; new expr + io/read-line dependencies; explicit batteries-included scope note (sess-20260820-0649-a3f19d93)
- Migrated to Bazel as primary build system (bazelisk installed, MODULE.bazel/BUILD.bazel/.bazelrc with a real --config=asan, CI updated, verified green). Extended stdlib design: dataframe (pandas-equivalent), nn/tokenizer/sort + io/read-floats (grounded in porting the real gpt2-alpine-c source). (sess-20260820-0649-a3f19d93)

- VS0 lexer + parser + CLI implemented, satisfying NORTHSTAR.md's Definition of Done domain 1 (32 unit tests, balanced + imbalanced S-expressions). CI green (build, test, ASan/UBSan, real test.prn smoke test). (sess-20260820-0649-a3f19d93)

