# wasm_train — training a network with no Python

A small neural network trained entirely from PARENA code, running natively, in WebAssembly, and
in a browser, with one GPU kernel checked against the PARENA math. Built 2026-09-27 (EMILY
BACKLOG SECTION 555) as the three steps agreed in place of a "PARENA replaces PyTorch +
Gymnasium" design that had no backward pass, no WGSL emitter, and syntax the compiler doesn't
accept.

| Step | What | Verified by |
|---|---|---|
| 1 | `stdlib/nn_train.prn`: 2-layer MLP (n-in → h tanh → 1 sigmoid), BCE loss, hand-written backprop, per-sample SGD, deterministic init | `make test-nn-train` (in CI): finite-difference gradient check on all 26 params of a 3-5-1 net (worst rel err ~1e-8), XOR 0.72 → 0.0004 BCE, bit-identical reruns |
| 2 | The same generated C compiled with Emscripten (`train_glue.c` → `nn_train.mjs` + 21 KB `nn_train.wasm`) | `make wasm-train`: XOR trains under Node/WebAssembly; all 17 params match the gcc build to 6e-15 |
| 3 | One hand-written WGSL kernel (`webgpu_matmul.mjs`): the first-layer matmul `X·W1ᵀ + b1` | `run_browser_check.mjs`: headless Chromium, 256×16·16×32 = 8192 outputs vs PARENA `hidden-pre` in f64, max rel error 4.6e-7 |

## Run it

```sh
make wasm-train                        # needs emcc on PATH (emsdk); rebuilds nn_train.mjs/.wasm
cd examples/wasm_train
npx http-server -p 8765 -s .           # any static server; must be http(s), not file://
# open http://127.0.0.1:8765/index.html
node run_browser_check.mjs             # headless check (needs the playwright package)
```

The built `nn_train.mjs`/`nn_train.wasm`/`nn_train_gen.c` are committed, so the page works
without emsdk. Regenerate them with `make wasm-train` after changing `stdlib/nn_train.prn`.

## Honest limits

- **Not tested on an iPhone.** Everything above ran in desktop Chromium and Node. The WebGPU run
  used SwiftShader (software Vulkan), which proves the kernel is correct but says nothing about
  real GPU speed. Safari's WebGPU support depends on the iOS version.
- **The GPU is not in the training loop yet.** The kernel is checked against the CPU on the
  first-layer forward pass only. Training runs on the WebAssembly CPU path (3000 XOR epochs ≈ 1 s
  in Chromium). WebGPU is async and the C code is sync, so moving training onto it means
  restructuring into batched JS-driven steps, not a drop-in swap.
- **f32 on the GPU, f64 on the CPU.** WGSL has no f64, so expect ~1e-6 agreement, not equality.
- **Small model only.** One hidden layer, one output, SGD, no batching. Every parameter write
  boxes a new 16-byte arena cell (`vec/set-at!` on a `(Vec F64)`), so memory grows per step;
  `nt_setup` frees the arena on retrain. Larger models need an unboxed F64 buffer first.
- **Some seeds get stuck.** 3 of seeds 0–9 sit at the XOR ln 2 saddle with lr 0.5. Seed 7 (the
  default) trains.
- **PARENA doesn't emit WGSL.** The kernel is hand-written JS/WGSL and trusted only because the
  page compares it to the PARENA output.

The runtime fix this needed: `runtime/parena_runtime.h`'s `pty_open_impl` called `forkpty`,
which doesn't exist under Emscripten. It now returns -1 there (`#ifdef __EMSCRIPTEN__`), so any
PARENA C output builds for the web with `-DPARENA_NO_GRAPHICS` and no pinned runtime copy.
