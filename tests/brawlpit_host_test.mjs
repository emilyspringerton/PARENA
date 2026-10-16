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
const IDS = ['bp-canvas', 'bp-stage', 'bp-pill', 'bp-load', 'bp-load-msg', 'bp-stat', 'bp-pad-status', 'b-fullscreen', 'bp-hat', 'bp-hat-img', 'bp-hat-msg', 'bp-hat-link'];
for (const id of IDS) els[id] = mkEl(id);
els['bp-canvas'].hidden = true;
const winL = {};
let rafCb = null;
globalThis.window = { addEventListener(t, f) { (winL[t] ||= []).push(f); }, requestAnimationFrame(cb) { rafCb = cb; } };
let activeEl = null;
globalThis.document = { getElementById: (id) => els[id] || null, get activeElement() { return activeEl; }, body: { tag: 'body' },
  createElement() { return { width: 0, height: 0, toDataURL: () => 'data:image/png;base64,STUB', getContext() { return { set imageSmoothingEnabled(v) {}, drawImage() {}, fillText() {}, set font(v) {}, set textAlign(v) {}, set textBaseline(v) {},
    getImageData(x, y, w, h) {
      const d = new Uint8ClampedArray(w * h * 4);
      if (w === 32) { for (let i = 0; i < 1024; i++) { d[i * 4] = 200; d[i * 4 + 1] = i % 256; d[i * 4 + 2] = 40; d[i * 4 + 3] = i < 512 ? 255 : 0; } }
      else { for (let py = 0; py < h; py++) for (let px = 0; px < w; px++) { const o = (py * w + px) * 4; d[o] = Math.floor(px / 128) + 4 * Math.floor(py / 128) + 1; d[o + 1] = px & 255; d[o + 2] = py & 255; d[o + 3] = 255; } }
      return { data: d }; } }; } }; } };
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
  if (url === '/api/v1/hats/h1/image' || url === 'sprites.png') return { ok: true, blob: async () => ({ blob: true }) };
  return { ok: false, status: 404 };
};
// capture the instance the host creates so the test can read the wasm's memory
let captured = null;
const realInstantiate = WebAssembly.instantiate.bind(WebAssembly);
WebAssembly.instantiate = async (bytes, imports) => { const r = await realInstantiate(bytes, imports); captured = r; return r; };

const host = await import(pathToFileURL(path.join(dir, 'bp-host.js')).href);
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
ok(/^data:image\/png/.test(els['bp-hat-img'].src) && els['bp-hat'].hidden === false && els['bp-hat'].title === 'Wearing: Pixel Crown',
   'header badge shows the hat the wasm wears (the decoded 32x32 surface)');
ok(els['bp-hat-msg'].textContent === 'Wearing your Hat Store hat: Pixel Crown.' && els['bp-hat-link'].hidden === false && els['bp-hat-link'].href === '/store.html',
   'the status line under the canvas says the store hat is being worn');
const hatAddr = x.bp_hat_addr(0);
const hatBytes = new Uint8Array(x.memory.buffer, hatAddr, 4096);
ok(hatBytes[0] === 200 && hatBytes[1] === 0 && hatBytes[5] === 1 && hatBytes[3] === 255 && hatBytes[4 * 600 + 3] === 0,
   'the decoded hat RGBA bytes are in wasm memory (hat atlas slot 0)');
const nameBytes = new Uint8Array(x.memory.buffer, x.bp_hatname_addr(), 16);
ok(Buffer.from(nameBytes.slice(0, 11)).toString() === 'Pixel Crown' && nameBytes[11] === 0, 'the hat name is NUL-terminated ASCII in wasm memory');

// ---- character portraits: atlas PNG -> sprite memory ------------------------------------------------
ok(((new DataView(x.memory.buffer)).getInt32(135168 + 31 * 4, true) & 0b1111111100) === 0b1111111100, 'engine knows portraits for characters 2..9 are loaded');
const sp = (cid, px, py) => new Uint8Array(x.memory.buffer, x.bp_sprite_addr(cid) + (py * 128 + px) * 4, 4);
ok(Array.from(sp(2, 0, 0)).join() === '1,0,0,255', 'atlas tile 0 lands in character 2\'s sprite slot (row 0, col 0)');
ok(Array.from(sp(5, 5, 3)).join() === '4,133,3,255', 'atlas tile 3 (x=384..511) lands in character 5\'s slot, pixel (5,3) intact');
ok(Array.from(sp(9, 127, 127)).join() === '8,255,255,255', 'the last tile\'s last pixel arrives intact (every row of every tile copied)');

// the wasm wears the hat by itself: nobody pressed anything on the select screen, and the engine has
// already made it player 1's hat (the pixels it draws are checked by brawlpit_engine_test.mjs)
ok((view.getInt32(135168 + 30 * 4, true) & 1) === 1, 'engine knows hat slot 0 is loaded');
ok(view.getInt32(135168 + 48 * 4, true) === 0, 'the equipped store hat is player 1\'s default hat -- the page never has to ask the player to pick it');

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

// ---- hat scenarios: each one loads a FRESH instance of the generated host against its own stubs ------------------------
// (the cases that matter in production: a curated hat whose art is an HTML-entity string, a signed-out visitor,
// nothing equipped, an expired sign-in, a missing image, a hat with no art at all)
const J = (v) => ({ ok: true, json: async () => v });
const S = (n) => ({ ok: false, status: n });
const B = () => ({ ok: true, blob: async () => ({ blob: true }) });
const SSO = 'https://iam.test/?redirect_uri=wotan';
let scenarioNo = 0;
async function scenario(session, routes) {
  const E = {}, canvases = [], fetches = [], L = {};
  for (const id of IDS) E[id] = { id, textContent: '', hidden: id === 'bp-hat' || id === 'bp-hat-link', disabled: false, src: '', href: '', title: '', listeners: {},
    addEventListener(t, f) { (this.listeners[t] ||= []).push(f); }, focus() {}, getBoundingClientRect: () => ({ left: 0, top: 0, width: 960, height: 540 }), getContext: () => ({ putImageData() {} }) };
  let raf = null, inst = null, now = 1000;
  globalThis.window = { addEventListener(t, f) { (L[t] ||= []).push(f); }, requestAnimationFrame(cb) { raf = cb; } };
  globalThis.document = { getElementById: (id) => E[id] || null, activeElement: null, body: {},
    createElement() {
      const c = { width: 0, height: 0, calls: [], toDataURL: () => 'data:image/png;base64,STUB' };
      const g = { font: '', fillStyle: '', set imageSmoothingEnabled(v) {}, set textAlign(v) {}, set textBaseline(v) {},
        drawImage() { c.calls.push(['drawImage']); },
        measureText() { return { actualBoundingBoxLeft: 5, actualBoundingBoxRight: 95, actualBoundingBoxAscent: 80, actualBoundingBoxDescent: 20 }; },
        translate(x, y) { c.calls.push(['translate', x, y]); }, scale(a, b) { c.calls.push(['scale', a, b]); },
        fillText(t) { c.calls.push(['fillText', t, g.font, g.fillStyle]); },
        getImageData(x, y, w, h) {
          const d = new Uint8ClampedArray(w * h * 4);
          if (w === 32) for (let i = 0; i < 1024; i++) { d[i * 4] = 200; d[i * 4 + 1] = 30; d[i * 4 + 2] = 40; d[i * 4 + 3] = 255; }
          return { data: d };
        } };
      c.getContext = () => g;
      canvases.push(c);
      return c;
    } };
  globalThis.getIdunaSession = () => session;
  globalThis.buildSsoURL = () => SSO;
  globalThis.fetch = async (url) => {
    fetches.push(url);
    if (url.endsWith('brawlpit.wasm')) return { ok: true, arrayBuffer: async () => wasmBytes.buffer.slice(wasmBytes.byteOffset, wasmBytes.byteOffset + wasmBytes.byteLength) };
    if (url === 'sprites.png') return B();
    return routes[url] || S(404);
  };
  WebAssembly.instantiate = async (bytes, imports) => { inst = await realInstantiate(bytes, imports); return inst; };
  const mod = await import(pathToFileURL(path.join(dir, 'bp-host.js')).href + '?s=' + (++scenarioNo));
  await waitFor(() => raf !== null);
  await settle(40);
  const xx = inst.instance.exports;
  const dv = () => new DataView(xx.memory.buffer);
  const frame2 = (n = 1) => { for (let i = 0; i < n; i++) { now += 16.667; const cb = raf; raf = null; cb(now); } };
  const key = (code, down) => { for (const f of L[down ? 'keydown' : 'keyup']) f({ keyCode: code, location: 0, preventDefault() {} }); };
  const tap2 = (code) => { key(code, true); frame2(3); key(code, false); frame2(2); };
  return { E, mod, canvases, fetches, x: xx, frame: frame2, tap: tap2,
    G: (k) => dv().getInt32(135168 + k * 4, true), P1hat: () => dv().getInt32(139264 + 59 * 4, true),
    glyph: () => canvases.flatMap((c) => c.calls).find((k) => k[0] === 'fillText'),
    calls: (name) => canvases.flatMap((c) => c.calls).filter((k) => k[0] === name) };
}
const HATS = '/api/v1/characters/c-1/hats';
const player = { token: 'tok', playerID: 'p-1' };
const mine = { '/api/v1/characters/by-player/p-1': J({ character_id: 'c-1' }) };

{ // the curated catalogue: image_asset is the entity string IDUNA stores, there is no PNG
  const A = await scenario(player, { ...mine, [HATS]: J([
    { name: 'Joystick Cap', equipped: false, image_asset: '&#127905;' },
    { name: 'Top Hat, Second Casting', equipped: true, image_asset: '&#127914;' }]) });
  const ft = A.glyph();
  ok(!!ft && ft[1] === '\u{1F3AA}', 'curated hat: the entity string "&#127914;" is decoded to the real glyph before it is drawn (never drawn as literal text)');
  ok(ft && ft[2] === '100px serif' && ft[3] === '#d8b463', 'curated hat: drawn large in the page\'s serif/emoji font (gold, so a plain symbol shows on the dark stage), then fitted');
  const tr = A.calls('translate')[0], sc = A.calls('scale')[0];
  ok(tr && sc && Math.abs(sc[1] - 0.22) < 1e-9 && Math.abs(tr[1] - 6.1) < 1e-9 && Math.abs(tr[2] - 25.6) < 1e-9,
     'curated hat: the glyph\'s ink box is scaled to 22 px, centred on x=16, standing on row 30 (the head)');
  ok((A.G(30) & 1) === 1 && A.G(48) === 0, 'curated hat: the engine has it in slot 0 and made it player 1\'s default hat');
  const px = new Uint8Array(A.x.memory.buffer, A.x.bp_hat_addr(0), 8);
  ok(px[0] === 200 && px[1] === 30 && px[3] === 255, 'curated hat: the rasterized 32x32 surface is in wasm memory');
  ok(A.E['bp-hat'].hidden === false && /^data:image\/png/.test(A.E['bp-hat-img'].src) && A.E['bp-hat'].title === 'Wearing: Top Hat, Second Casting', 'curated hat: the header badge shows what is worn');
  ok(A.E['bp-hat-msg'].textContent === 'Wearing your Hat Store hat: Top Hat, Second Casting.', 'curated hat: the status line names the hat');
  // end to end through the generated host: practice match, lock in, fight -- no hat key is ever pressed
  A.tap(52); A.tap(13); A.tap(13); A.frame(220);
  ok(A.x.bp_phase() === 2 && A.P1hat() === 0, 'curated hat: in a match played through the host, player 1 wears it without being asked to pick it');
}
{ // a pixel hat, with the list wrapped in an object (the page accepts the bare array or {hats:[...]})
  const A = await scenario(player, { ...mine, [HATS]: J({ hats: [{ name: 'Crown', equipped: true, image_url: '/api/v1/hats/h1/image' }] }), '/api/v1/hats/h1/image': B() });
  ok((A.G(30) & 1) === 1 && A.G(48) === 0 && A.glyph() === undefined, 'pixel hat in a {hats:[...]} reply: worn, and no text is drawn for it');
}
{ // signed out
  const A = await scenario(null, {});
  ok(!A.fetches.some((u) => u.startsWith('/api/')), 'signed out: no IDUNA call is made');
  ok(/not signed in/.test(A.E['bp-hat-msg'].textContent) && A.E['bp-hat-link'].hidden === false && A.E['bp-hat-link'].href === SSO && A.E['bp-hat-link'].textContent === 'Sign in with IDUNA',
     'signed out: the status line says so and links to the IDUNA sign-in');
  ok(A.G(30) === 0 && A.G(48) === -1 && A.E['bp-hat'].hidden === true, 'signed out: no hat, and the badge stays hidden');
}
{ // signed in, nothing equipped
  const A = await scenario(player, { ...mine, [HATS]: J([{ name: 'Top Hat', equipped: false, image_asset: '&#127914;' }]) });
  ok(/no hat equipped/.test(A.E['bp-hat-msg'].textContent) && A.E['bp-hat-link'].href === '/store.html', 'nothing equipped: the status line points at the Hat Store');
  ok(A.G(30) === 0 && A.G(48) === -1 && A.glyph() === undefined, 'nothing equipped: no hat is loaded or drawn');
}
{ // the sign-in has expired
  const A = await scenario(player, { '/api/v1/characters/by-player/p-1': S(401) });
  ok(/expired/.test(A.E['bp-hat-msg'].textContent) && A.E['bp-hat-link'].href === SSO, 'HTTP 401: "your sign-in has expired", with a link to sign in again');
}
{ // no character
  const A = await scenario(player, { '/api/v1/characters/by-player/p-1': S(404) });
  ok(/no BRAWLPIT character/.test(A.E['bp-hat-msg'].textContent), 'HTTP 404 on the character: the status line says there is no character yet');
}
{ // the hat image is gone
  const A = await scenario(player, { ...mine, [HATS]: J([{ name: 'Pixel Crown', equipped: true, image_url: '/api/v1/hats/h9/image' }]) });
  ok(/Could not load the image for Pixel Crown \(HTTP 404\)/.test(A.E['bp-hat-msg'].textContent) && A.G(30) === 0, 'a missing hat image is reported as such (not as "no character"), and no half-hat is worn');
}
{ // a hat with no art at all: the monogram, so an equipped hat is never silently missing
  const A = await scenario(player, { ...mine, [HATS]: J([{ name: 'wizard hat', equipped: true, image_asset: 'hat_wizard' }]) });
  const ft = A.glyph();
  ok(!!ft && ft[1] === 'W' && ft[2] === 'bold 100px sans-serif' && ft[3] === '#d8b463' && A.G(48) === 0, 'no PNG and no glyph: a gold monogram of the hat\'s name is worn instead of nothing');
}
{ // the entity decoder itself
  const d = (await scenario(null, {})).mod.decodeEntities;
  ok(d('&#127914;') === '\u{1F3AA}' && d('&#x1F3AA;') === '\u{1F3AA}' && d('&#X1f3aa;') === '\u{1F3AA}', 'decodeEntities: decimal and hex numeric references');
  ok(d('a&#38;b') === 'a&b' && d('x&#65;&#66;y') === 'xABy' && d('plain') === 'plain' && d('') === '', 'decodeEntities: mixed text, several references, plain text, empty');
  ok(d('&#zz;') === '&#zz;' && d('&#123') === '&#123' && d('&#;') === '&#;' && d('&#99999999;') === '&#99999999;' && d('&amp;') === '&amp;' && d('&#') === '&#',
     'decodeEntities: malformed, unterminated, empty, out-of-range and named references pass through untouched');
}

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
