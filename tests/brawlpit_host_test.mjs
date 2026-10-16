// Integration test: the PARENA-generated page host (bp-host.js) driving the real brawlpit.wasm,
// under Node with a minimal DOM stub. No browser, no mocks of the engine: the wasm is the actual
// module; only document / window / fetch / canvas are stand-ins so the generated JS can run.
//
//   node brawlpit_host_test.mjs <dir containing brawlpit.wasm and bp-host.js>
//
// What it proves: the host boots the wasm with NO imports, wires the canvas ImageData onto the
// wasm's own memory (no copy), forwards keyboard / pointer / gamepad input into the engine, and
// decodes an equipped hat into wasm memory -- and that a whole match can be started and played
// through that path, with the game logic living entirely in the wasm.
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const dir = process.argv[2];
const wasmBytes = fs.readFileSync(path.join(dir, 'brawlpit.wasm'));
let pass = 0, fail = 0;
const ok = (c, m) => { c ? pass++ : fail++; console.log((c ? 'PASS: ' : 'FAIL: ') + m); };
const settle = async (n = 30) => { for (let i = 0; i < n; i++) await new Promise((r) => setTimeout(r, 2)); };
const waitFor = async (cond, ms = 4000) => { const t0 = Date.now(); while (!cond() && Date.now() - t0 < ms) await new Promise((r) => setTimeout(r, 5)); };

// ---- DOM stand-ins ---------------------------------------------------------------------------
const els = {};
const mkEl = (id) => ({ id, textContent: '', hidden: false, disabled: false, src: '', listeners: {},
  addEventListener(t, f) { (this.listeners[t] ||= []).push(f); }, focus() { this.focused = true; },
  requestFullscreen() { this.fullscreen = true; },
  getBoundingClientRect: () => ({ left: 100, top: 50, width: 480, height: 270 }),
  getContext() { return ctx; } });
const ctx = { puts: [], putImageData(img, x, y) { this.puts.push({ img, x, y }); } };
for (const id of ['bp-canvas', 'bp-stage', 'bp-pill', 'bp-load', 'bp-load-msg', 'bp-stat', 'bp-pad-status', 'b-fullscreen', 'bp-hat', 'bp-hat-img'])
  els[id] = mkEl(id);
els['bp-canvas'].hidden = true;
const winL = {};
let rafCb = null;
globalThis.window = { addEventListener(t, f) { (winL[t] ||= []).push(f); }, requestAnimationFrame(cb) { rafCb = cb; } };
let activeEl = null;
globalThis.document = { getElementById: (id) => els[id] || null, get activeElement() { return activeEl; }, body: { tag: 'body' },
  createElement() { return { width: 0, height: 0, getContext() { return { set imageSmoothingEnabled(v) {}, drawImage() {}, fillText() {}, set font(v) {}, set textAlign(v) {}, set textBaseline(v) {},
    getImageData() { const d = new Uint8ClampedArray(4096); for (let i = 0; i < 1024; i++) { d[i * 4] = 200; d[i * 4 + 1] = i % 256; d[i * 4 + 2] = 40; d[i * 4 + 3] = i < 512 ? 255 : 0; } return { data: d }; } }; } }; } };
let pads = [null, null, null, null];
Object.defineProperty(globalThis, 'navigator', { value: { getGamepads: () => pads }, configurable: true, writable: true });
globalThis.ImageData = class { constructor(data, w, h) { this.data = data; this.width = w; this.height = h; } };
globalThis.createImageBitmap = async (b) => ({ bitmap: true, from: b });
globalThis.getIdunaSession = () => ({ token: 'tok123', playerID: 'p-1', displayName: 'tester' });
const apiCalls = [];
globalThis.fetch = async (url, opts) => {
  apiCalls.push({ url, opts });
  if (url.endsWith('brawlpit.wasm')) return { ok: true, arrayBuffer: async () => wasmBytes.buffer.slice(wasmBytes.byteOffset, wasmBytes.byteOffset + wasmBytes.byteLength) };
  if (url.startsWith('/api/v1/characters/by-player/')) return { ok: true, json: async () => ({ character_id: 'c-9' }) };
  if (url === '/api/v1/characters/c-9/hats') return { ok: true, json: async () => ([{ name: 'Top Hat', equipped: false }, { name: 'Pixel Crown', equipped: true, image_url: '/api/v1/hats/h1/image' }]) };
  if (url === '/api/v1/hats/h1/image') return { ok: true, blob: async () => ({ blob: true }) };
  return { ok: false, status: 404 };
};
// capture the instance the host creates so the test can read the wasm's memory
let captured = null;
const realInstantiate = WebAssembly.instantiate.bind(WebAssembly);
WebAssembly.instantiate = async (bytes, imports) => { const r = await realInstantiate(bytes, imports); captured = r; return r; };

await import(pathToFileURL(path.join(dir, 'bp-host.js')).href);
await waitFor(() => rafCb !== null);
await settle(20);

ok(captured !== null, 'host fetched and instantiated brawlpit.wasm');
ok(WebAssembly.Module.imports(captured.module).length === 0, 'the wasm module has zero imports (no malloc, no libc, no JS callbacks)');
const x = captured.instance.exports;
ok(els['bp-pill'].textContent === 'Running in WebAssembly', 'status pill says the game is running in WebAssembly');
ok(els['bp-canvas'].hidden === false && els['bp-load'].hidden === true, 'canvas shown, loading panel hidden');
ok(/imports/.test(els['bp-stat'].textContent) && /0 imports/.test(els['bp-stat'].textContent), 'footer reports the real import count from the module (0)');
ok(!!rafCb, 'host started the animation-frame loop');
ok(els['b-fullscreen'].disabled === false, 'fullscreen button enabled');
ok(Object.keys(winL).sort().join() === 'blur,gamepadconnected,gamepaddisconnected,keydown,keyup', 'host listens for keys, blur and gamepads');

let t = 1000;
const frame = (n = 1) => { for (let i = 0; i < n; i++) { t += 16.667; const cb = rafCb; rafCb = null; cb(t); } };
const keydown = (code, loc = 0) => { const e = { keyCode: code, location: loc, prevented: false, preventDefault() { this.prevented = true; } }; for (const f of winL.keydown) f(e); return e; };
const keyup = (code, loc = 0) => { for (const f of winL.keyup) f({ keyCode: code, location: loc, preventDefault() {} }); };
const tap = (code, frames = 3) => { keydown(code); frame(frames); keyup(code); frame(2); };

frame(3);
const put = ctx.puts[ctx.puts.length - 1];
ok(ctx.puts.length >= 3 && put.x === 0 && put.y === 0, 'each frame blits the framebuffer to the canvas');
ok(put.img.data.buffer === x.memory.buffer, 'the canvas ImageData is a view onto the wasm memory itself (no copy)');
ok(put.img.width === 960 && put.img.height === 540, 'framebuffer is 960x540');
const uniq = (arr, step = 4 * 97) => { const s = new Set(); for (let i = 0; i < arr.length; i += step) s.add(arr[i] | arr[i + 1] << 8 | arr[i + 2] << 16); return s.size; };
ok(uniq(put.img.data) > 12, 'the wasm painted a real image (the title screen), not a blank buffer');
ok(x.bp_phase() === 0, 'game starts on the title screen');

// ---- keyboard goes in through the host, the wasm decides what it means -------------------------------
tap(52);                      // title: key 4 = PRACTICE (nothing hurts: movement checks below are deterministic)
ok(x.bp_phase() === 1, 'a menu hotkey on the title opens fighter select (menu logic runs in wasm)');
tap(39);                      // right: next fighter
tap(13);                      // lock in P1
tap(13);                      // stage -> fight
ok(x.bp_phase() === 2, 'locking in fighter and stage starts a match');
const e1 = keydown(40); keyup(40);
ok(e1.prevented, 'arrow keys are consumed so the page does not scroll');
const e2 = keydown(65); keyup(65);
ok(!e2.prevented, 'letter keys are left alone');
frame(200);                   // READY / 3 / 2 / 1 / GO
const simBefore = x.bp_sim_frames();
frame(60);
ok(x.bp_sim_frames() > simBefore + 40, 'the simulation advances in wasm at a fixed 60 Hz while frames are fed');

// move right: P1 keys are WASD; D = 68
const PL = 139264;
const f32 = (a) => new DataView(x.memory.buffer).getFloat32(a, true);
const x0 = f32(PL + 16);
keydown(68); frame(25); keyup(68);
ok(f32(PL + 16) > x0 + 4, 'holding D moves player 1 right (key -> host -> bp_key -> wasm physics)');
const x1 = f32(PL + 16);
keydown(272, 0); keyup(272);   // stray right-shift id must not crash anything
frame(3);

// two keyboards merge for a lone human: arrow-left works as well
keydown(37); frame(25);
ok(f32(PL + 24) < -0.5, 'in one-player mode the arrow keys also drive player 1 (velocity turns left)');
keyup(37);

// ---- pointer ----------------------------------------------------------------------------------------
for (const f of els['bp-canvas'].listeners.pointerdown) f({ clientX: 100 + 240, clientY: 50 + 135, buttons: 1 });
const view = new DataView(x.memory.buffer);
ok(view.getInt32(131072 + 1024, true) === 480 && view.getInt32(131072 + 1028, true) === 270 && view.getInt32(131072 + 1032, true) === 1,
   'pointer position is mapped from CSS pixels onto the 960x540 framebuffer');
for (const f of els['bp-canvas'].listeners.pointerup) f({ clientX: 100, clientY: 50, buttons: 0 });

// ---- gamepad ----------------------------------------------------------------------------------------
const mkpad = (axes, pressed) => ({ axes, buttons: Array.from({ length: 17 }, (_, i) => ({ pressed: pressed.includes(i), value: pressed.includes(i) ? 1 : 0 })) });
pads = [mkpad([0.9, -0.1, 0, 0], [0, 7]), null, null, null];
frame(3);
const padBase = 131072 + 1280;
ok(view.getInt32(padBase, true) === 1, 'a connected gamepad is reported to the engine');
ok(Math.abs(view.getFloat32(padBase + 4, true) - 0.9) < 1e-6, 'left stick x reaches the engine');
ok((view.getInt32(padBase + 20, true) & 0b1) === 1 && ((view.getInt32(padBase + 20, true) >> 7) & 1) === 1, 'pressed buttons are packed into the bitmask');
pads = [null, null, null, null];
frame(2);
ok(view.getInt32(padBase, true) === 0, 'an unplugged gamepad is reported gone');
for (const f of winL.gamepadconnected) f({});
ok(/No controller/.test(els['bp-pad-status'].textContent), 'the controller label tracks plug state');
pads = [mkpad([0, 0, 0, 0], []), null, null, null];
for (const f of winL.gamepadconnected) f({});
ok(/Controller connected \(1\)/.test(els['bp-pad-status'].textContent), 'the controller label reports a connected pad');
pads = [null, null, null, null];

// ---- hats: IDUNA -> PNG -> RGBA bytes in wasm memory --------------------------------------------------
const authed = apiCalls.filter((c) => c.url.startsWith('/api/v1/characters/'));
ok(authed.length >= 2 && authed.every((c) => c.opts && c.opts.headers && c.opts.headers.Authorization === 'Bearer tok123'),
   'IDUNA API calls carry the shared session token');
ok(els['bp-hat-img'].src === '/api/v1/hats/h1/image' && els['bp-hat'].hidden === false, 'header badge shows the equipped hat');
const hatAddr = x.bp_hat_addr(0);
const hatBytes = new Uint8Array(x.memory.buffer, hatAddr, 4096);
ok(hatBytes[0] === 200 && hatBytes[1] === 0 && hatBytes[5] === 1 && hatBytes[3] === 255 && hatBytes[4 * 600 + 3] === 0,
   'the decoded hat RGBA bytes are in wasm memory (hat atlas slot 0)');
const nameBytes = new Uint8Array(x.memory.buffer, x.bp_hatname_addr(), 16);
ok(Buffer.from(nameBytes.slice(0, 11)).toString() === 'Pixel Crown' && nameBytes[11] === 0, 'the hat name is NUL-terminated ASCII in wasm memory');

// the wasm draws the hat on the fighter: toggle P1's hat in select and compare pixels is overkill;
// check the engine accepted the hat slot
ok((view.getInt32(135168 + 30 * 4, true) & 1) === 1, 'engine knows hat slot 0 is loaded');

// ---- blur releases keys ------------------------------------------------------------------------------------
keydown(68);
for (const f of winL.blur) f({});
frame(2);
const xs = f32(PL + 16);
frame(30);
ok(Math.abs(f32(PL + 16) - xs) < 3.5, 'window blur releases held keys (no stuck movement)');

// ---- sustained run: no traps, still painting -------------------------------------------------------------------
const before = ctx.puts.length;
for (let i = 0; i < 600; i++) { if (i % 17 === 0) tap(74, 1); frame(1); }
ok(ctx.puts.length - before === 600 + Math.floor(600 / 17 + 1) * 2 + 0 || ctx.puts.length - before >= 600, 'ran 600+ more frames with no trap');

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
