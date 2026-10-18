// Real-browser test of BEDROCK RACERS: the static bundle (index.html + bedrockracers.wasm + br-host.js) served as a page would be,
// driven in Chromium like a player: load -> title -> a match -> pause -> focus loss -> a gamepad -> the bot-only view. Screenshots go
// to outdir so the look can be checked by eye.
//   scripts/build_bedrockracers_web.sh <bundle-dir>
//   NODE_PATH=<dir with node_modules/playwright> PLAYWRIGHT_BROWSERS_PATH=... node tests/br_browser_test.mjs <bundle-dir> [outdir]
import { createRequire } from 'node:module';
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const bundle = process.argv[2];
if (!bundle) { console.error('usage: br_browser_test.mjs <bundle-dir> [outdir]'); process.exit(2); }
const outdir = process.argv[3] || path.join(bundle, 'shots');
fs.mkdirSync(outdir, { recursive: true });
let pass = 0, fail = 0;
const ok = (c, m) => { c ? pass++ : fail++; console.log((c ? 'PASS: ' : 'FAIL: ') + m); };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// ---- a static server, nothing else ------------------------------------------------------------------------------------------
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.ts': 'text/plain' };
const server = http.createServer((req, res) => {
  const f = path.join(bundle, req.url === '/' ? 'index.html' : decodeURIComponent(req.url.split('?')[0]));
  if (!f.startsWith(path.resolve(bundle)) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); res.end('not found'); return; }
  res.writeHead(200, { 'content-type': MIME[path.extname(f)] || 'application/octet-stream' });
  fs.createReadStream(f).pipe(res);
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const base = `http://127.0.0.1:${server.address().port}`;

const browser = await chromium.launch();
const errors = [];
async function newPage(extraInit, sink = errors) {
  const ctx = await browser.newContext({ viewport: { width: 1280, height: 800 } });
  // a pad the test drives: navigator.getGamepads() answers with whatever window.__pad holds
  await ctx.addInitScript(() => {
    window.__pad = null;
    Object.defineProperty(navigator, 'getGamepads', { value: () => [window.__pad, null, null, null], configurable: true });
  });
  if (extraInit) await ctx.addInitScript(extraInit);
  const page = await ctx.newPage();
  page.on('pageerror', (e) => sink.push('pageerror: ' + e.message));
  page.on('console', (m) => { if (m.type() === 'error') sink.push('console: ' + m.text()); });
  page.on('requestfailed', (r) => sink.push('requestfailed: ' + r.url()));
  await page.goto(base + '/');
  await page.waitForSelector('#br-canvas:not([hidden])', { timeout: 15000 });
  return page;
}
const pill = (p) => p.locator('#br-pill').textContent();
async function waitPill(p, text, ms = 8000) {
  const t = Date.now();
  while (Date.now() - t < ms) { if ((await pill(p)) === text) return true; await sleep(50); }
  return false;
}
// the canvas's pixels, as the page painted them
const pixels = (p) => p.evaluate(() => { const c = document.getElementById('br-canvas'); return Array.from(c.getContext('2d').getImageData(0, 0, c.width, c.height).data); });
const distinct = (a) => { const s = new Set(); for (let i = 0; i < a.length; i += 4 * 97) s.add((a[i] << 16) | (a[i + 1] << 8) | a[i + 2]); return s.size; };
const diff = (a, b) => { let d = 0; for (let i = 0; i < a.length; i += 4) d += Math.abs(a[i] - b[i]) + Math.abs(a[i + 1] - b[i + 1]) + Math.abs(a[i + 2] - b[i + 2]); return d / (a.length / 4) / 3; };
// a click at a framebuffer coordinate (the canvas is 640x360 scaled to fit the page)
async function clickFb(p, x, y) {
  const box = await p.locator('#br-canvas').boundingBox();
  await p.mouse.click(box.x + (x / 640) * box.width, box.y + (y / 360) * box.height);
}
const shot = (p, name) => p.screenshot({ path: path.join(outdir, name + '.png') });
const setPad = (p, o) => p.evaluate((x) => {
  window.__pad = x ? { connected: true, axes: x.axes || [0, 0, 0, 0], buttons: Array.from({ length: 17 }, (_, i) => ({ pressed: (x.pressed || []).includes(i), value: (x.values || {})[i] || 0 })) } : null;
}, o);

// ---- load + title -------------------------------------------------------------------------------------------------------------------
const A = await newPage();
ok(await waitPill(A, 'Ready', 5000), 'the page loads and the engine says Ready');
ok((await A.locator('#br-stat').textContent()).includes('0 imports'), 'the footer reports the wasm has 0 imports (' + (await A.locator('#br-stat').textContent()) + ')');
ok(await A.locator('#br-fullscreen').isEnabled(), 'the fullscreen button is enabled once the game is running');
ok(await A.locator('#br-load').isHidden(), 'the loading panel is gone');
await sleep(400);
const title = await pixels(A);
ok(distinct(title) > 40, `the title is a picture (${distinct(title)} sampled colours)`);
const title2 = (await sleep(500), await pixels(A));
ok(diff(title, title2) > 0.05, `the title animates: the arena orbits (mean pixel change ${diff(title, title2).toFixed(2)})`);
await shot(A, '01-title');

// ---- a match --------------------------------------------------------------------------------------------------------------------------
await sleep(200); await A.keyboard.press('ArrowRight'); await sleep(120); await shot(A, '01b-title-teal');
await A.keyboard.press('ArrowLeft');
await A.keyboard.press('Enter');
ok(await waitPill(A, 'In a match'), 'Enter starts a match: the pill says In a match');
await sleep(1200);
await shot(A, '02-kickoff');
const k1 = await pixels(A); await sleep(500); const k2 = await pixels(A);
ok(diff(k1, k2) > 0.02, `the countdown frame is alive (mean pixel change ${diff(k1, k2).toFixed(2)})`);
await sleep(3200);                                   // past the kickoff countdown
// the HUD's SPEED readout is the probe: "0" is a few pixels of ink, a three-digit speed is many more
const inkAt = async () => { const a = await pixels(A); let n = 0; for (let y = 316; y < 334; y++) for (let x = 16; x < 56; x++) { const i = (y * 640 + x) * 4; if (a[i] > 200 && a[i + 1] > 200 && a[i + 2] > 185) n++; } return n; };
const idleInk = await inkAt();
await A.keyboard.down('w'); await A.keyboard.down('Shift');
await sleep(1400);
const driveInk = await inkAt();
await shot(A, '03-driving');
await A.keyboard.up('w'); await A.keyboard.up('Shift');
ok(driveInk > idleInk * 1.6 + 10, `holding W and Shift raises the SPEED readout: ${idleInk} px of digit ink at rest, ${driveInk} at speed`);
// right mouse: the magnet, and no browser menu over the game
const prevented = await A.evaluate(() => { const e = new MouseEvent('contextmenu', { cancelable: true, bubbles: true }); document.getElementById('br-canvas').dispatchEvent(e); return e.defaultPrevented; });
ok(prevented, 'a right click over the game does not open the browser menu');
const box = await A.locator('#br-canvas').boundingBox();
await A.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
await A.mouse.down({ button: 'right' }); await sleep(300); await A.mouse.up({ button: 'right' });
ok(await pill(A) === 'In a match', 'holding and letting go of the right button leaves the match running');
// pause, and the arrow keys do not scroll the page
await A.keyboard.press('Escape');
ok(await waitPill(A, 'Paused'), 'Esc pauses');
const p1 = await pixels(A); await sleep(600); const p2 = await pixels(A);
ok(diff(p1, p2) === 0, `a paused frame is perfectly still, even right after pausing a fast car (mean pixel change ${diff(p1, p2).toFixed(3)})`);
await shot(A, '05-paused');
await clickFb(A, 268, 181);
ok(await waitPill(A, 'In a match'), 'a click on RESUME resumes (the pointer maps from page pixels to framebuffer pixels)');
await A.keyboard.press('Escape');
ok(await waitPill(A, 'Paused'), 'Esc pauses again');
await A.keyboard.press('Escape');
ok(await waitPill(A, 'In a match'), 'Esc resumes');
const sy0 = await A.evaluate(() => window.scrollY);
await A.keyboard.down('ArrowDown'); await A.keyboard.down('Space'); await sleep(250); await A.keyboard.up('ArrowDown'); await A.keyboard.up('Space');
ok((await A.evaluate(() => window.scrollY)) === sy0, 'the arrow keys and Space do not scroll the page while the game has the keyboard');
// losing focus pauses a person's match and lets go of every key
await A.keyboard.down('w');
await A.evaluate(() => window.dispatchEvent(new Event('blur')));
ok(await waitPill(A, 'Paused'), 'losing window focus pauses the match');
await A.keyboard.up('w');
await A.keyboard.press('Escape');
ok(await waitPill(A, 'In a match'), 'and Esc brings it back');

// ---- frame rate in the real page ---------------------------------------------------------------------------------------------------------
const fps = await A.evaluate(() => new Promise((res) => { let n = 0; const t0 = performance.now(); const f = () => { n++; if (performance.now() - t0 < 3000) requestAnimationFrame(f); else res(n / ((performance.now() - t0) / 1000)); }; requestAnimationFrame(f); }));
console.log(`  page frame rate during a match (headless Chromium, software compositor): ${fps.toFixed(1)} fps`);
ok(fps > 40, `the page holds over 40 fps during a match (${fps.toFixed(1)})`);

// ---- a gamepad, on a fresh page ----------------------------------------------------------------------------------------------------------
const P = await newPage();
ok(await waitPill(P, 'Ready', 5000), 'a second page loads');
await setPad(P, { pressed: [0] }); await sleep(250); await setPad(P, { pressed: [] });
ok(await waitPill(P, 'In a match', 4000), 'pad A at the title starts a match');
await setPad(P, { values: { 7: 1 }, pressed: [7], axes: [0.8, 0, 0, 0] }); await sleep(300);
ok(await pill(P) === 'In a match', 'a held trigger and stick leave the match running');
await setPad(P, { pressed: [9] }); await sleep(200); await setPad(P, { pressed: [] });
ok(await waitPill(P, 'Paused', 3000), 'pad Start pauses');
await setPad(P, null);

// ---- the bot-only view -----------------------------------------------------------------------------------------------------------------------
const B = await newPage();
await sleep(300); await shot(B, '00-title-fresh');
await clickFb(B, 80, 40);                            // nothing there: a stray click does nothing
await clickFb(B, 389 + 10, 190);                     // the coral swatch
await clickFb(B, 368 + 40, 246);                     // HARD
await clickFb(B, 376, 285);                          // WATCH
ok(await waitPill(B, 'Watching bots'), 'a click on WATCH watches the bots');
await sleep(7000);
await shot(B, '04-spectator');
const s1 = await pixels(B); await sleep(500); const s2 = await pixels(B);
ok(distinct(s1) > 60 && diff(s1, s2) > 0.02, `the spectator view is alive (${distinct(s1)} sampled colours, mean pixel change ${diff(s1, s2).toFixed(2)})`);
await B.evaluate(() => window.dispatchEvent(new Event('blur')));
ok(await pill(B) === 'Watching bots', 'losing focus does not pause a match with nobody in it');

// ---- an embedding frame that is not allowed the gamepad feature: getGamepads() THROWS. The game must not freeze ------------------------------------------------
const padErrors = [];
const T = await newPage(() => { Object.defineProperty(navigator, 'getGamepads', { value: () => { throw new DOMException('Access to the feature "gamepad" is disallowed by permissions policy.', 'SecurityError'); }, configurable: true }); }, padErrors);
await T.keyboard.press('Enter');
ok(await waitPill(T, 'In a match'), 'with getGamepads() throwing (a frame without the gamepad feature) a match still starts');
await sleep(800);
const t1 = await pixels(T); await sleep(600); const t2 = await pixels(T);
ok(diff(t1, t2) > 0.01, `and the game keeps running: the frame still changes (mean pixel change ${diff(t1, t2).toFixed(2)}); the pad chain ended on its own`);
ok(padErrors.length <= 1, `the throw was reported at most once, not every frame (${padErrors.length})`);

// ---- fullscreen refused (an embedding frame without allowfullscreen) must not raise an unhandled rejection -----------------------------------------------
const fsErrors = [];
const F = await newPage(() => { Element.prototype.requestFullscreen = () => Promise.reject(new TypeError('Fullscreen is not allowed in this frame')); }, fsErrors);
await F.locator('#br-fullscreen').click(); await sleep(300);
ok(fsErrors.length === 0, 'a refused fullscreen request is swallowed: no unhandled rejection' + (fsErrors.length ? ': ' + fsErrors[0] : ''));
ok(await waitPill(F, 'Ready', 2000), 'and the page is unaffected');

ok(errors.length === 0, 'no page errors, console errors or failed requests' + (errors.length ? ': ' + errors.slice(0, 4).join(' | ') : ''));
await browser.close(); server.close();
console.log(`\n${pass} passed, ${fail} failed  (screenshots in ${outdir})`);
process.exit(fail ? 1 : 0);
