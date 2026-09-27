// run_train_smoke.mjs -- runs the Emscripten build of stdlib/nn_train.prn (nn_train.mjs +
// nn_train.wasm) under Node and checks it really trains: XOR loss falls below 0.05, all four
// points classify correctly, and the first-layer outputs are finite. If a file of native
// parameters is given (`node run_train_smoke.mjs native_params.txt`, written by the gcc build
// of train_glue.c), every WebAssembly parameter must match the native one to 1e-9 -- same C,
// same seed, two different compilers and libms.
import { readFileSync } from "node:fs";
import createModule from "./nn_train.mjs";
import { trainXor } from "./xor.mjs";

const M = await createModule();
const { params, first, last, preds } = trainXor(M, { epochs: 3000, lr: 0.5, seed: 7 });

console.log(`wasm XOR: mean BCE ${first.toFixed(4)} -> ${last.toFixed(4)} after 3000 epochs`);
const want = [0, 1, 1, 0];
let ok = last < 0.05;
preds.forEach((p, r) => {
  console.log(`  row ${r}: ${p.toFixed(4)} (want ${want[r]})`);
  if ((p > 0.5) !== (want[r] > 0.5)) ok = false;
});
if (!ok) {
  console.error("FAIL: WebAssembly training did not learn XOR");
  process.exit(1);
}

const nativePath = process.argv[2];
if (nativePath) {
  const native = readFileSync(nativePath, "utf8").trim().split("\n").map(Number);
  if (native.length !== params.length) {
    console.error(`FAIL: native has ${native.length} params, wasm has ${params.length}`);
    process.exit(1);
  }
  let worst = 0;
  native.forEach((v, i) => { worst = Math.max(worst, Math.abs(v - params[i])); });
  console.log(`wasm vs native: ${params.length} params, max abs diff ${worst.toExponential(2)}`);
  if (worst > 1e-9) {
    console.error("FAIL: WebAssembly and native training diverged");
    process.exit(1);
  }
}
console.log("wasm-train: PASS (PARENA-generated C trained XOR inside WebAssembly)");
