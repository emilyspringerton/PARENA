// run_clamp_smoke.mjs -- real, live execution proof for the `wasm-smoke` Makefile target
// (see its own header comment in ../../Makefile for the full story). Loads the real .wasm module
// `wasm-ld` just produced from PARENA-compiled LLVM IR and asserts real, correct F64 results --
// not a structural/bytes check, an actual WebAssembly.instantiate + function call.
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import path from "node:path";

const here = path.dirname(fileURLToPath(import.meta.url));
const bytes = readFileSync(path.join(here, "clamp.wasm"));
const { instance } = await WebAssembly.instantiate(bytes, {});
const clamp = instance.exports.clamp_f64;

const cases = [
  [5, 0, 10, 5],
  [-3, 0, 10, 0],
  [15, 0, 10, 10],
  [7.5, 0, 10, 7.5],
];
for (const [x, lo, hi, expected] of cases) {
  const got = clamp(x, lo, hi);
  if (got !== expected) {
    console.error(`FAIL: clamp_f64(${x}, ${lo}, ${hi}) = ${got}, expected ${expected}`);
    process.exit(1);
  }
  console.log(`clamp_f64(${x}, ${lo}, ${hi}) = ${got}`);
}
console.log("wasm-smoke: PASS (real WebAssembly execution, not a structural check)");
