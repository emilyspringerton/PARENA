// tests/shankpit_survival_test.mjs -- SURVIVAL (the SHANKPHONE's survival button) as a whole: the shankpit-460 engine with the CITY
// level and the survival mods (stdlib/shankpit_survival/). Plays the mode headlessly through the same exports the page calls.
//   node tests/shankpit_survival_test.mjs <survival.wasm>
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const wasmPath = process.argv[2];
if (!wasmPath) { console.error('usage: shankpit_survival_test.mjs <wasm>'); process.exit(2); }
const here = path.dirname(fileURLToPath(import.meta.url));
let pass = 0, fail = 0;
const ok = (c, m) => { c ? pass++ : fail++; console.log((c ? 'PASS: ' : 'FAIL: ') + m); };

const fields = {};
for (const m of fs.readFileSync(path.join(here, '../stdlib/shankpit460/sp_fields.prn'), 'utf8').matchAll(/\(defn k-([a-z0-9-]+) \[\] : I32 (\d+)\)/g)) fields[m[1]] = +m[2];
// engine globals (sp_mem.prn) and survival globals (sv_mod.prn)
const G = { mode: 3, phase: 16, countdown: 11, endReason: 8, boxN: 127, mapId: 108, want: 245, tick: 0,
  svOn: 600, wave: 601, spawned: 602, nextMs: 603, owned: 604, best: 606, resting: 610, broken: 612, taken: 613 };
const SV = 524288, GUN = SV + 1024, CHEST = SV + 2048;
const quota = (w) => 4 + 3 * Math.max(1, w);

async function boot() {
  const { instance } = await WebAssembly.instantiate(fs.readFileSync(wasmPath), {});
  const x = instance.exports;
  x.sp_init();
  let now = 1000;
  const dv = () => new DataView(x.memory.buffer);
  return {
    x, dv,
    frame(n = 1) { for (let i = 0; i < n; i++) { now += 16; x.sp_frame(now); } },
    gi: (k) => dv().getInt32(135168 + k * 4, true),
    pf: (p, k) => dv().getFloat32(x.sp_player_addr(p) + fields[k] * 4, true),
    pi: (p, k) => dv().getInt32(x.sp_player_addr(p) + fields[k] * 4, true),
    setI: (p, k, v) => dv().setInt32(x.sp_player_addr(p) + fields[k] * 4, v, true),
    i32: (a) => dv().getInt32(a, true), f32: (a) => dv().getFloat32(a, true),
    fb: () => new Uint8Array(x.memory.buffer, x.sp_fb_addr(), 640 * 360 * 4),
    click(px, py) { x.sp_pointer(px, py); x.sp_button(0, 1); x.sp_button(0, 0); },
    tap(code) { x.sp_key(code, 1); this.frame(1); x.sp_key(code, 0); this.frame(1); },
    play() { this.frame(3); this.click(320, 204); x.sp_lock(1); },
    zombies() { const z = []; for (let p = 1; p <= 9; p++) if (this.pi(p, 'active') && this.pi(p, 'state') === 0) z.push(p); return z; },
    dist(p) { return Math.hypot(this.pf(p, 'x') - this.pf(0, 'x'), this.pf(p, 'z') - this.pf(0, 'z')); },
  };
}
const distinct = (fb) => { const s = new Set(); for (let i = 0; i < fb.length; i += 4 * 97) s.add((fb[i] << 16) | (fb[i + 1] << 8) | fb[i + 2]); return s.size; };

// ---- module hygiene ----------------------------------------------------------------------------------------------------------
{
  const mod = new WebAssembly.Module(fs.readFileSync(wasmPath));
  ok(WebAssembly.Module.imports(mod).length === 0, 'the module imports nothing (no allocator, no libc, no JS callbacks)');
  const ex = WebAssembly.Module.exports(mod).map((e) => e.name);
  ok(['sp_init', 'sp_frame', 'sp_key', 'sp_pointer', 'sp_button', 'sp_lock', 'sp_fb_addr', 'sp_snd_addr'].every((n) => ex.includes(n)),
    'it exports the shankpit-460 page contract, so the same host (sp-host.js) drives it');
}

// ---- no survival global collides with an engine global ---------------------------------------------------------------------
{
  const seen = new Map(); const dup = [];
  const dirs = ['../stdlib/shankpit460', '../stdlib/shankpit_survival'];
  for (const d of dirs) for (const f of fs.readdirSync(path.join(here, d))) {
    if (!f.endsWith('.prn') || /fields|map_data/.test(f)) continue;
    for (const m of fs.readFileSync(path.join(here, d, f), 'utf8').matchAll(/\(defn ((?:g|o|kat|want)-?[a-z0-9-]*) \[\] : I32 (\d+)\)/g)) {
      if (m[1] === 'g-base') continue;
      const k = +m[2];
      if (seen.has(k) && seen.get(k) !== m[1]) dup.push(`${m[1]}/${seen.get(k)}=${k}`); else seen.set(k, m[1]);
    }
  }
  ok(dup.length === 0, 'every global word is used by one name only' + (dup.length ? ' -- ' + dup.join(', ') : ''));
}

// ---- the title is SURVIVAL over the CITY --------------------------------------------------------------------------------------
const g = await boot();
g.frame(5);
ok(g.gi(G.phase) === 0, 'it opens on the title screen');
ok(g.gi(G.mapId) === 3 && g.gi(G.boxN) >= 40 && g.gi(G.boxN) <= 127, `the CITY level is loaded behind it (${g.gi(G.boxN)} boxes)`);
ok(distinct(g.fb()) > 40, `the title draws the city (${distinct(g.fb())} distinct sampled colours)`);
{
  let n = 0; const fresh = 1;
  for (let i = 0; i < 16; i++) if (g.i32(CHEST + i * 32)) n++;
  ok(n === 10, `ten loot chests stand on the road ring (${n})`);
}

// ---- PLAY starts a run: knife only, nobody else on the street ------------------------------------------------------------------
g.play();
ok(g.gi(G.phase) === 1 && g.gi(G.mode) === 2 && g.gi(G.svOn) === 1, 'PLAY starts a survival run (mode 2), not a bot match');
ok(g.pi(0, 'weapon') === 0 && g.gi(G.owned) === 1, 'the hero starts with the knife and owns nothing else');
ok(g.zombies().length === 0, 'the street is empty at the start');
{
  let guns = 0; for (let i = 0; i < 64; i++) if (g.i32(GUN + i * 16)) guns++;
  ok(guns === 12, `twelve guns lie on the roads (${guns})`);
}
g.frame(140);   // the countdown
ok(g.gi(G.countdown) === 0, 'the GET READY countdown runs out');
g.tap(50);      // "2": the magnum, not owned
g.frame(2);
ok(g.pi(0, 'weapon') === 0, 'an unowned weapon cannot be selected');

// ---- the first zombie: 3 s in, on the 60..130 ring, and it comes for you ---------------------------------------------------------
{ let t = 0; while (g.zombies().length === 0 && t < 400) { g.frame(1); t++; } }
const zs = g.zombies();
ok(zs.length === 1 && g.gi(G.spawned) === 1, `one zombie arrives about three seconds in (${zs.length})`);
if (zs.length) {
  const z = zs[0], d0 = g.dist(z);
  ok(d0 >= 55 && d0 <= 135, `it lands on the ring around the hero (${d0.toFixed(1)} units away)`);
  ok(g.pi(z, 'health') === 100 && g.pi(z, 'shield') === 0 && g.pi(z, 'is-bot') === 1, 'it has 100 health, no shield');
  g.frame(300);
  const d1 = g.dist(z);
  ok(d1 < d0 - 20, `it walks at the hero (${d0.toFixed(1)} -> ${d1.toFixed(1)})`);
}
ok(g.gi(G.spawned) === 1, 'the next one waits out the 15 s spawn gap');

// ---- a gun on the road: walk onto it and it is yours ----------------------------------------------------------------------------
{
  let slot = -1;
  for (let i = 0; i < 64; i++) if (g.i32(GUN + i * 16) && g.i32(GUN + i * 16 + 4) !== 0) { slot = i; break; }
  const w = g.i32(GUN + slot * 16 + 4);
  g.x.sp_t_place(0, g.f32(GUN + slot * 16 + 8), 0, g.f32(GUN + slot * 16 + 12), 0, 0);
  g.frame(3);
  ok(g.i32(GUN + slot * 16) === 0, 'standing on a gun picks it up');
  ok((g.gi(G.owned) & (1 << w)) !== 0 && g.pi(0, 'weapon') === w, `it is owned and equipped (weapon ${w})`);
  ok(g.gi(G.taken) === 1, 'the run counts it');
}

// ---- a chest: knife it open, a gun falls out ---------------------------------------------------------------------------------------
{
  let c = -1;
  for (let i = 0; i < 16; i++) if (g.i32(CHEST + i * 32) && g.i32(CHEST + i * 32 + 4) === 0) { c = i; break; }
  const cx = g.f32(CHEST + c * 32 + 12), cz = g.f32(CHEST + c * 32 + 16);
  const gunsBefore = [...Array(64).keys()].filter((i) => g.i32(GUN + i * 16)).length;
  g.tap(49);  // "1": the knife
  g.x.sp_t_place(0, cx, 0, cz + 2.6, 0, -25);   // facing -z (yaw 0), looking down at the chest
  g.frame(2);
  g.x.sp_button(0, 1); g.frame(30); g.x.sp_button(0, 0); g.frame(2);
  ok(g.i32(CHEST + c * 32) === 0 && g.gi(G.broken) === 1, 'a wooden chest breaks under the knife (chest_rules: 100 hp, blades x1.2)');
  const gunsAfter = [...Array(64).keys()].filter((i) => g.i32(GUN + i * 16)).length;
  ok(gunsAfter === gunsBefore + 1 || g.gi(G.taken) === 2, 'and leaves a gun behind');
}

// ---- clearing a wave: the next one is bigger and comes after a rest ---------------------------------------------------------------
{
  for (const z of g.zombies()) { g.setI(z, 'health', 0); g.setI(z, 'state', 1); }
  g.x.sp_t_poke(G.spawned, quota(1));
  g.frame(3);
  ok(g.gi(G.wave) === 2 && g.gi(G.spawned) === 0 && g.gi(G.resting) === 1, 'killing the whole quota clears wave 1; wave 2 rests first');
  ok(g.zombies().length === 0, 'the corpses are cleared off the street');
  g.frame(6000 / 16 + 5);
  ok(g.gi(G.spawned) === 1, 'wave 2 starts after the 6 s intermission');
}

// ---- death is the end: stand still and the zombies get you ------------------------------------------------------------------------
{
  g.x.sp_t_place(0, 0, 0, 0, 0, 0);
  let t = 0;
  while (g.gi(G.phase) === 1 && t < 20000) { g.frame(10); t += 10; }
  ok(g.gi(G.phase) === 2 && g.gi(G.endReason) === 4, `a hero who stands still is killed and the run ends (${(t * 16 / 1000).toFixed(0)} s)`);
  ok(g.pi(0, 'state') === 1, 'the hero did not respawn');
  ok(g.gi(G.best) >= 2, 'the best wave is remembered');
  g.frame(30);
  g.click(320, 260);
  g.frame(2);
  ok(g.gi(G.phase) === 1 && g.gi(G.wave) === 1 && g.pi(0, 'state') === 0 && g.pi(0, 'weapon') === 0, 'AGAIN starts a fresh run with the knife');
}

// ---- trees and wood (sv_trees.prn + tree_rules.prn) ---------------------------------------------------------------------------
{
  const T = SV + 6144, tw = (i, k) => g.i32(T + i * 32 + k * 4), tf = (i, k) => g.f32(T + i * 32 + k * 4);
  const n = g.gi(623);
  ok(n >= 15 && n <= 48, `a forest grows in the city (${n} trees)`);
  const kinds = new Set(); for (let i = 0; i < n; i++) kinds.add(tw(i, 0));
  ok(kinds.size === 3, `pine, oak and birch all grow (${[...kinds].join(',')})`);
  ok(g.gi(G.boxN) <= 127 - 16, `the forest leaves room for barricades (${g.gi(G.boxN)} boxes)`);
  // walk up to a birch (80 hp, knife 40 a stab) and chop it
  let b = -1; for (let i = 0; i < n; i++) if (tw(i, 0) === 3 && tw(i, 7) === 1) { b = i; break; }
  if (b < 0) for (let i = 0; i < n; i++) if (tw(i, 7) === 1) { b = i; break; }
  const kind = tw(b, 0), x = tf(b, 3), z = tf(b, 4), wood0 = g.gi(622);
  g.tap(49);
  g.x.sp_t_place(0, x, 0, z + 2.2, 0, 0);
  for (let k = 0; k < 8 && tw(b, 7) === 1; k++) { g.x.sp_button(0, 1); g.frame(24); g.x.sp_button(0, 0); g.frame(2); }
  ok(tw(b, 7) === 0, `the knife fells a tree (kind ${kind})`);
  const bonus = { 1: 4, 2: 6, 3: 3 }[kind];
  ok(g.gi(622) - wood0 >= bonus + 2, `chopping gives wood: a log per hit plus the fell bonus (${wood0} -> ${g.gi(622)})`);
  ok(g.f32(163840 + tw(b, 1) * 32 + 4) < -100, 'the felled trunk leaves the world (no collision)');
  // build: wood buys a barricade (a box), and without wood there is none
  const boxes = g.gi(G.boxN), w = g.gi(622);
  g.x.sp_t_place(0, x, 0, z + 2.2, 180, 0);
  g.tap(66); g.frame(2);
  ok(g.gi(G.boxN) === boxes + 1 && g.gi(622) === w - 5, `B builds a barricade for 5 wood (${w} -> ${g.gi(622)})`);
  g.x.sp_t_poke(622, 2); g.tap(66); g.frame(2);
  ok(g.gi(G.boxN) === boxes + 1 && g.gi(626) === 2, 'with 2 wood nothing is built (NOT ENOUGH WOOD)');
  // regrowth (birch 60 s, oak 120 s): step past it
  g.x.sp_t_place(0, x + 20, 0, z + 20, 0, 0);
  g.frame(Math.ceil(125000 / 16));
  ok(tw(b, 7) === 1 || g.gi(G.phase) !== 1, 'a felled tree grows back');
}


// ---- telecrystals: the WORLDMAP contract, played (sv_crystal.prn + stdlib/worldmap/wm_rules.prn) ---------------------------------------
{
  const C = { cast: 630, ms: 631, need: 632, msg: 633, guard: 635, jumps: 636, fizzles: 637 }, WOOD = 622;
  const h = await boot();
  h.play(); h.frame(140);   // the countdown
  const at = (x, z) => { h.x.sp_t_place(0, x, 6, z, 0, 0); h.setI(0, 'health', 100); };
  const pos = () => [h.pf(0, 'x'), h.pf(0, 'z')];
  const near = (a, b, e = 1.5) => Math.hypot(a[0] - b[0], a[1] - b[1]) < e;
  // the wmap and its generated table agree (tools/sv_wmap_gen.py --check is the real freshness test; this checks the wasm)
  const wmap = fs.readFileSync(path.join(here, '../stdlib/shankpit_survival/survival_city.wmap'), 'utf8').split('\n')
    .filter((l) => l.startsWith('crystal ')).map((l) => l.split(/\s+/));
  ok(wmap.length === 4, 'survival_city.wmap carries four crystals');
  const hub = wmap[1], ne = wmap[2];                                  // SV_PLAZA_TO_SOUTHWEST, SV_NORTHEAST_RETURN
  const P = (r) => [+r[2], +r[3]], D = (r) => [+r[6], +r[7]];
  const first = wmap[0];

  // not in a ring: G does nothing
  h.setI(1, 'active', 0); h.x.sp_t_poke(WOOD, 9); at(426, 142); h.tap(71); h.frame(5);
  ok(h.gi(C.cast) === 0 && near(pos(), [426, 142]), 'G outside every ring starts nothing');

  // inside a ring, enough wood: the jump fires at COMMIT (600 ms), not at the total (1000 ms), and costs wood only then
  h.x.sp_t_poke(WOOD, 9); at(...P(first)); h.tap(71);
  ok(h.gi(C.cast) === 1 && h.gi(C.need) === 600, `G in the ring starts a cast that commits at ${h.gi(C.need)} ms`);
  h.frame(20);
  ok(h.gi(C.cast) === 1 && h.gi(WOOD) === 9, 'mid-cast: still here, nothing charged yet');
  h.frame(30);
  ok(near(pos(), D(first)), `the jump lands at the wmap destination (${pos().map((v) => v.toFixed(0))})`);
  ok(h.gi(WOOD) === 9 - +first[9] && h.gi(C.jumps) === 1, `cost ${first[9]} wood was charged at commit (9 -> ${h.gi(WOOD)})`);
  ok(Math.abs(h.pf(0, 'yaw') - (+first[8] * Math.PI / 180)) < 1e-3, 'the hero faces the crystal\'s yaw');

  // the 250 ms re-cast guard: an immediate G cannot restart a cast
  at(...P(hub)); h.tap(71);
  ok(h.gi(C.cast) === 0, 'right after a jump the re-cast guard refuses G');
  h.frame(20);

  // not enough wood
  h.x.sp_t_poke(WOOD, 2); at(...P(hub)); h.tap(71);
  ok(h.gi(C.cast) === 0 && h.gi(C.msg) === 3 && h.gi(WOOD) === 2, 'with too little wood the crystal refuses (nothing charged)');
  h.frame(30);

  // leaving the ring + 1.5 interrupts; staying inside the slack does not
  h.x.sp_t_poke(WOOD, 9); at(...P(hub)); h.tap(71); h.frame(10);
  h.x.sp_t_place(0, +hub[2] + 10 + 1.0, 6, +hub[3], 0, 0); h.frame(3);                // 1.0 past the edge: inside the 1.5 slack
  ok(h.gi(C.cast) === 2, 'a step just past the ring (inside the 1.5 slack) does not interrupt');
  h.x.sp_t_place(0, +hub[2] + 10 + 3.0, 6, +hub[3], 0, 0); h.frame(2);                // 3.0 past: out
  ok(h.gi(C.cast) === 0 && h.gi(C.msg) === 2 && h.gi(WOOD) === 9 && h.gi(C.jumps) === 1, 'leaving the ring interrupts the cast, uncharged');
  h.frame(30);

  // a free return needs no wood
  h.x.sp_t_poke(WOOD, 0); at(...P(ne)); h.tap(71); h.frame(40);
  ok(near(pos(), D(ne)) && h.gi(WOOD) === 0, `the free return crystal works with no wood (${pos().map((v) => v.toFixed(0))})`);
  h.frame(30);

  // travel from combat (GFD's gate): the cast waits an extra 742 ms and a d20 of 4 or less fizzles it
  let jumped = 0, fizzled = 0, needOk = true;
  for (let t = 0; t < 14 && (!jumped || !fizzled); t++) {
    h.x.sp_t_poke(WOOD, 9); at(...P(first)); h.frame(30);
    // a living zombie 12 units away = a fight (sv-fight-radius 45)
    h.setI(1, 'active', 1); h.setI(1, 'state', 0); h.setI(1, 'health', 100);
    h.x.sp_t_place(1, +first[2] + 12, 6, +first[3], 0, 0);
    h.x.sp_t_poke(C.guard, 0);
    h.tap(71);
    if (h.gi(C.cast) !== 1) { needOk = false; break; }
    if (h.gi(C.need) !== 600 + 742) needOk = false;
    const j0 = h.gi(C.jumps), f0 = h.gi(C.fizzles);
    for (let k = 0; k < 130 && h.gi(C.cast) === 1; k++) { at(...P(first)); h.setI(1, 'health', 100); h.x.sp_t_place(1, +first[2] + 12, 6, +first[3], 0, 0); h.frame(1); }
    if (h.gi(C.jumps) > j0) { jumped++; if (h.gi(WOOD) !== 9 - +first[9]) needOk = false; }
    if (h.gi(C.fizzles) > f0) { fizzled++; if (h.gi(WOOD) !== 9 || !near(pos(), P(first), 15) || h.gi(C.msg) !== 4) needOk = false; }
    h.setI(1, 'active', 0); h.setI(1, 'state', 0);
    h.frame(30);
  }
  ok(needOk, 'in a fight the cast commits at 600 + 742 ms, a fizzle charges nothing and leaves you at the crystal, a jump charges wood');
  ok(jumped > 0 && fizzled > 0, `the d20 gate both lets casts through and fizzles them (${jumped} jumped, ${fizzled} fizzled)`);

  // every crystal and every destination is on open street (not inside a building)
  const blocked = (x, z) => { for (let i = 1; i < h.gi(G.boxN); i++) {
      const a = (n) => h.f32(163840 + i * 32 + n);                     // map box record: x y z w h d
      const cx = a(0), cy = a(4), cz = a(8), w = a(12), hh = a(16), d = a(20);
      if (cy - hh / 2 < 8 && Math.abs(x - cx) < w / 2 + 2 && Math.abs(z - cz) < d / 2 + 2) return true; } return false; };
  const bad = wmap.flatMap((r) => [P(r), D(r)]).filter(([x, z]) => blocked(x, z));
  ok(bad.length === 0, 'every crystal and destination stands on open street' + (bad.length ? ' -- blocked: ' + JSON.stringify(bad) : ''));
}

// ---- a frame of the city costs little enough for a phone -------------------------------------------------------------------------
{
  const t0 = performance.now(); g.frame(120); const ms = (performance.now() - t0) / 120;
  console.log(`info: ${ms.toFixed(2)} ms per frame (sim + 640x360 software render) on this box`);
  ok(ms < 16, 'a frame takes under 16 ms here');
}

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
