// run_browser_check.mjs -- loads index.html in headless Chromium (Playwright) and asserts both
// halves of the page: WebAssembly training learned XOR, and the WGSL matmul matched the PARENA
// CPU reference. Usage: serve this directory, then
//   node run_browser_check.mjs http://127.0.0.1:8765/index.html
//
// Headless Chromium has no hardware GPU, so this forces WebGPU onto SwiftShader (Chromium's
// software Vulkan). That proves the kernel and the JS plumbing are correct; it says nothing
// about speed on a real GPU, and nothing about iPhone Safari specifically.
import { chromium } from "playwright";

const url = process.argv[2] ?? "http://127.0.0.1:8765/index.html";
const browser = await chromium.launch({
  args: ["--enable-unsafe-webgpu", "--enable-features=Vulkan", "--use-vulkan=swiftshader",
         "--use-webgpu-adapter=swiftshader", "--disable-vulkan-surface"],
});
const page = await browser.newPage();
page.on("pageerror", (e) => console.error("pageerror:", e.message));
await page.goto(url);
await page.waitForFunction(() => window.__results && window.__results.gpu, null, { timeout: 60000 });
const { train, gpu } = await page.evaluate(() => window.__results);
await browser.close();

console.log(`browser train: loss ${train.first.toFixed(4)} -> ${train.last.toFixed(4)} in ${train.ms.toFixed(0)} ms`);
if (!train.correct || train.last >= 0.05) {
  console.error("FAIL: in-browser WebAssembly training did not learn XOR");
  process.exit(1);
}
if (!gpu.available) {
  console.error("FAIL: WebGPU unavailable even with SwiftShader flags");
  process.exit(1);
}
console.log(`browser gpu: ${gpu.outputs} outputs, max rel error vs PARENA f64 ${gpu.worst.toExponential(2)}`);
if (!gpu.pass) {
  console.error("FAIL: WGSL matmul disagrees with the PARENA CPU reference");
  process.exit(1);
}
console.log("wasm-train browser check: PASS");
