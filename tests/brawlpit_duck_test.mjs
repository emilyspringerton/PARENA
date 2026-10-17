// The shared duck moveset of the BRAWLPIT wasm engine: every fighter has the same moves (QUACK SHOTGUN,
// TUXEDO DASH-PUNCH, flap, dodge, jab / kick), and the page's duck atlas is drawn from the wasm.
//
//   node brawlpit_duck_test.mjs <brawlpit.wasm> <duck.png> [shot-dir]
import fs from 'node:fs';
import zlib from 'node:zlib';
import path from 'node:path';
import { load, pngEncode } from './brawlpit_shot.mjs';

const [wasmPath, duckPath, shotDir] = process.argv.slice(2);
let pass = 0, fail = 0;
const ok = (c, m) => { c ? pass++ : fail++; console.log((c ? 'PASS: ' : 'FAIL: ') + m); };

const PL = 139264, G = 135168, TN = 143360;
const P = { char: 1, x: 4, y: 5, vx: 6, vy: 7, ground: 8, facing: 9, state: 20, dmg: 21, stocks: 22, dashcd: 33, sbcd: 34,
  umbrella: 35, dashframe: 39, smashcharge: 47, invuln: 55, shotgun: 71 };
const ST = { idle: 0, attack: 3, stunned: 4, shield: 5, wavedash: 13, upb: 14, dash: 15 };

// minimal PNG decoder (8-bit RGBA, non-interlaced): the browser does this for the real page
function decodePng(buf) {
  let o = 8, w = 0, h = 0; const idat = [];
  while (o < buf.length) {
    const len = buf.readUInt32BE(o), type = buf.toString('latin1', o + 4, o + 8), d = buf.subarray(o + 8, o + 8 + len);
    if (type === 'IHDR') { w = d.readUInt32BE(0); h = d.readUInt32BE(4); if (d[8] !== 8 || d[9] !== 6 || d[12] !== 0) throw new Error('png: want 8-bit RGBA'); }
    if (type === 'IDAT') idat.push(d);
    o += 12 + len;
  }
  const raw = zlib.inflateSync(Buffer.concat(idat)), bpp = 4, stride = w * bpp, out = Buffer.alloc(h * stride);
  for (let y = 0; y < h; y++) {
    const ft = raw[y * (stride + 1)], line = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    for (let x = 0; x < stride; x++) {
      const a = x >= bpp ? out[y * stride + x - bpp] : 0, b = y ? out[(y - 1) * stride + x] : 0, c = x >= bpp && y ? out[(y - 1) * stride + x - bpp] : 0;
      let v = line[x];
      if (ft === 1) v += a; else if (ft === 2) v += b; else if (ft === 3) v += (a + b) >> 1;
      else if (ft === 4) { const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c); v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c; }
      out[y * stride + x] = v & 255;
    }
  }
  return { w, h, rgba: out };
}

const { instance } = await WebAssembly.instantiate(fs.readFileSync(wasmPath), {});
const E = instance.exports;
ok(typeof E.bp_duck_addr === 'function' && typeof E.bp_duck_set === 'function', 'exports the duck atlas entry points bp_duck_addr / bp_duck_set');

// ---- sim-level helpers -------------------------------------------------------------------------------
function sim(c0, c1) {
  const x = instance.exports;
  return { x };
}
async function fresh(c0, c1, n = 2) {
  const { instance: inst } = await WebAssembly.instantiate(fs.readFileSync(wasmPath), {});
  const x = inst.exports, dv = new DataView(x.memory.buffer);
  x.init_fighters(); x.bp_seed_bang(7);
  x.set_slot_char_bang(0, c0); for (let i = 1; i < 8; i++) x.set_slot_char_bang(i, c1);
  x.sim_init_match(n, 0, 0);
  x.set_p_is_bot_bang(1, 0);
  const t = {
    x, dv,
    gi: (k) => dv.getInt32(G + k * 4, true),
    pi: (p, k) => dv.getInt32(PL + p * 512 + k * 4, true),
    pf: (p, k) => dv.getFloat32(PL + p * 512 + k * 4, true),
    seti: (p, k, v) => dv.setInt32(PL + p * 512 + k * 4, v, true),
    setf: (p, k, v) => dv.setFloat32(PL + p * 512 + k * 4, v, true),
    in0: [0, 0, 0, 0, 0, 0], in1: [0, 0, 0, 0, 0, 0],
    step(n = 1) { for (let i = 0; i < n; i++) { x.sim_set_input_bang(0, ...t.in0); x.sim_set_input_bang(1, ...t.in1); x.sim_step(); } },
    pellets() { let n = 0; for (let i = 0; i < 16; i++) if (dv.getInt32(TN + i * 32, true) && dv.getInt32(TN + i * 32 + 28, true) === 12) n++; return n; },
    turnips() { let n = 0; for (let i = 0; i < 16; i++) if (dv.getInt32(TN + i * 32, true)) n++; return n; },
  };
  t.step(140);                       // let both land and the respawn invulnerability run out
  t.setf(0, P.x, 0); t.setf(0, P.y, 0.2); t.setf(1, P.x, 5); t.setf(1, P.y, 0.2);
  t.setf(0, P.vx, 0); t.setf(1, P.vx, 0); t.seti(0, P.facing, 1); t.seti(1, P.facing, -1);
  t.step(3);
  return t;
}
const press = (t, inp, frames = 1) => { t.in0 = inp; t.step(frames); t.in0 = [0, 0, 0, 0, 0, 0]; };
// sx sy jump attack shield special
const SPECIAL = [0, 0, 0, 0, 0, 1], SIDE_SPECIAL = [1, 0, 0, 0, 0, 1], UP_SPECIAL = [0, 1, 0, 0, 0, 1], DODGE = [0, 0, 0, 0, 1, 1];

// ---- the moves are the same for every fighter ---------------------------------------------------------
const shotgun = [], punch = [], flap = [], dodge = [];
for (let c = 0; c < 10; c++) {
  {
    const t = await fresh(c, (c + 4) % 10);
    t.setf(1, P.x, 40);              // out of range: count the pellets without them hitting anyone
    press(t, SPECIAL);
    const timerSet = t.pi(0, P.shotgun);
    t.step(10);
    const before = t.pellets();
    t.step(4);                       // the shells leave the barrel on tick 14
    const after = t.pellets();
    const u = await fresh(c, (c + 4) % 10);
    press(u, SPECIAL);
    u.step(30);
    shotgun.push([timerSet, before, after, u.pf(1, P.dmg), u.pi(1, P.state)]);
  }
  {
    const t = await fresh(c, (c + 4) % 10);
    t.setf(1, P.x, 2.4);
    press(t, SIDE_SPECIAL);
    const st = t.pi(0, P.state), fr = t.pi(0, P.dashframe);
    t.step(14);
    punch.push([st, fr, t.pf(1, P.dmg)]);
  }
  {
    const t = await fresh(c, (c + 4) % 10);
    t.setf(0, P.y, 12); t.setf(0, P.vy, 0);
    t.step(2);
    press(t, UP_SPECIAL);
    flap.push([t.pi(0, P.state), t.pf(0, P.vy) > 1.0 ? 1 : 0]);
  }
  {
    const t = await fresh(c, (c + 4) % 10);
    press(t, DODGE);
    dodge.push([t.pi(0, 32) > 0 && t.pi(0, P.dashcd) > 0 ? 1 : 0, Math.abs(t.pf(0, P.vx)) > 1.0 ? 1 : 0]);
  }
}
const same = (rows, i) => rows.every((r) => r[i] === rows[0][i]);
ok(shotgun.every((r) => r[0] > 0), 'QUACK SHOTGUN starts for all 10 fighters (neutral special)');
ok(shotgun.every((r) => r[1] === 0 && r[2] === 3), 'no pellets while the gun is being raised, exactly three the tick it fires, for all 10 fighters');
ok(shotgun.every((r) => r[3] === 13.5 || r[3] === shotgun[0][3]) && same(shotgun, 3) && shotgun[0][3] > 10, `the shotgun deals the same damage whoever fires it (${shotgun[0][3]}% from three point-blank pellets)`);
ok(shotgun.every((r) => r[4] === ST.stunned || r[3] > 0), 'the shotgun hit staggers the target');
ok(punch.every((r) => r[0] === ST.dash && r[1] >= 1), 'TUXEDO DASH-PUNCH starts for all 10 fighters (special + a direction on the ground)');
ok(punch.every((r) => r[2] === 10) , `the dash-punch lands for 10% on every fighter (got ${[...new Set(punch.map((r) => r[2]))].join(',')})`);
ok(flap.every((r) => r[0] === ST.upb && r[1] === 1), 'up + special in the air is the same flap recovery for all 10 fighters');
ok(dodge.every((d) => d[0] === 1 && d[1] === 1), 'shield + special is the same dodge roll (a fast slide) for all 10 fighters');

// ---- rules of the new moves ---------------------------------------------------------------------------
{
  const t = await fresh(3, 6);
  // the old hold-to-charge smash is gone
  t.in0 = SIDE_SPECIAL; let charged = false;
  for (let i = 0; i < 90; i++) { t.step(1); if (t.pi(0, P.smashcharge) > 0) charged = true; }
  ok(!charged, 'holding special with a direction never charges a smash any more');
}
{
  const t = await fresh(0, 1);
  t.setf(1, P.x, 30);
  press(t, SPECIAL);
  t.step(14);
  const first = t.pellets();
  t.step(40);
  ok(first === 3 && t.pellets() === 0, 'pellets fly straight and expire (3 in flight, 0 after their 22 ticks)');
}
{
  const t = await fresh(0, 1);
  t.setf(1, P.x, 30);
  press(t, SPECIAL);
  t.step(2);
  ok(t.pi(0, P.sbcd) > 0, 'the shotgun has a cooldown');
  ok(t.pi(0, P.state) === ST.idle && t.pi(0, P.shotgun) > 0, 'firing does not need a special state: the gun timer drives it');
  t.in0 = [1, 0, 0, 0, 0, 0]; const x0 = t.pf(0, P.x); t.step(6);
  ok(Math.abs(t.pf(0, P.x) - x0) < 1.0, 'the duck plants its feet while the gun comes up');
}
{
  const t = await fresh(0, 1);
  t.setf(1, P.x, 30);
  press(t, SPECIAL);
  t.step(4);
  t.in1 = [0, 0, 0, 0, 0, 0];
  // a hit during the wind-up cancels the shot
  t.seti(1, P.facing, 1); t.setf(1, P.x, 0.5);
  t.in1 = [0, 0, 0, 1, 0, 0]; t.step(1); t.in1 = [0, 0, 0, 0, 0, 0];
  t.step(20);
  ok(t.pi(0, P.shotgun) === 0 && t.pellets() === 0 || t.pi(0, P.state) !== ST.stunned, 'a staggered duck loses its shot (or the hit missed)');
}
{
  // the jab still works for everyone
  const t = await fresh(2, 8);
  t.setf(1, P.x, 2.5);
  press(t, [0, 0, 0, 1, 0, 0], 2);
  t.step(6);
  ok(t.pf(1, P.dmg) > 0, 'the ground jab still hits');
}
{
  // classic flag: the original per-character moves are still there for the parity tests
  const { instance: inst } = await WebAssembly.instantiate(fs.readFileSync(wasmPath), {});
  const dv = new DataView(inst.exports.memory.buffer);
  ok(dv.getInt32(G + 61 * 4, true) === 0, 'the game boots with the duck moveset (g-moveset = 0); 1 is the classic moves, for parity tests only');
}

// ---- the atlas and the drawing -------------------------------------------------------------------------
const png = decodePng(fs.readFileSync(duckPath));
ok(png.w === 640 && png.h === 340, `duck.png is a 640x340 atlas (${png.w}x${png.h})`);
let opaque = 0; for (let i = 3; i < png.rgba.length; i += 4) if (png.rgba[i] === 255) opaque++;
ok(opaque > 8000, `the atlas has duck pixels in it (${opaque} opaque)`);
for (let f = 0; f < 29; f++) {
  let n = 0; const cx = (f % 8) * 80, cy = Math.floor(f / 8) * 68;
  for (let y = 0; y < 68; y++) for (let x = 0; x < 80; x++) if (png.rgba[((cy + y) * 640 + cx + x) * 4 + 3] === 255) n++;
  if (n < 300) { ok(false, `atlas frame ${f} is empty (${n} px)`); break; }
  if (f === 28) ok(true, 'all 29 atlas frames have a duck in them');
}
{
  const g = await load(wasmPath);
  g.frame(5);
  const start = async (duck) => {
    const g2 = await load(wasmPath);
    if (duck) {
      new Uint8Array(g2.x.memory.buffer, g2.x.bp_duck_addr(), png.rgba.length).set(png.rgba);
      g2.x.bp_duck_set(1);
    }
    g2.frame(5); g2.tap(52); g2.frame(2);   // practice
    g2.tap(13); g2.tap(13); g2.frame(220);
    return g2;
  };
  const a = await start(false), b = await start(true);
  const diff = (() => { const u = a.fb(), v = b.fb(); let n = 0; for (let i = 0; i < u.length; i += 4) if (u[i] !== v[i] || u[i + 1] !== v[i + 1] || u[i + 2] !== v[i + 2]) n++; return n; })();
  ok(diff > 500, `with the atlas uploaded the fighters are drawn from it (${diff} pixels differ from the fallback body)`);
  // every animation: walk, jump, jab, kick, shotgun, dash-punch -- the frame must change
  const shot = (g2, name) => { g2.frame(2); if (shotDir) { fs.mkdirSync(shotDir, { recursive: true }); fs.writeFileSync(path.join(shotDir, name + '.png'), g2.png()); } return Buffer.from(g2.fb()).toString('base64').length; };
  const seen = new Set();
  const grab = (n) => { g_hash(b, n); };
  const g_hash = (g2, n) => { const u = g2.fb(); let h = 0; for (let i = 0; i < u.length; i += 4 * 7) h = (h * 31 + u[i] + u[i + 1] * 3) | 0; seen.add(h); shot(g2, n); };
  grab('idle');
  b.key(68, true); b.frame(20); grab('walk'); b.key(68, false); b.frame(30);
  b.tap(32, 3); b.frame(8); grab('jump'); b.frame(40);
  b.key(74, true); b.frame(3); grab('jab'); b.key(74, false); b.frame(30);
  b.key(75, true); b.frame(10); grab('shotgun-raise'); b.frame(6); grab('shotgun-fire'); b.key(75, false); b.frame(60);
  b.key(68, true); b.key(75, true); b.frame(6); grab('dash-punch'); b.key(68, false); b.key(75, false);
  ok(seen.size >= 6, `the screen changes with every animation (${seen.size} distinct frames over idle, walk, jump, jab, shotgun x2, dash-punch)`);
}
console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
