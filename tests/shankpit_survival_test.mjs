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

// ---- a frame of the city costs little enough for a phone -------------------------------------------------------------------------
{
  const t0 = performance.now(); g.frame(120); const ms = (performance.now() - t0) / 120;
  console.log(`info: ${ms.toFixed(2)} ms per frame (sim + 640x360 software render) on this box`);
  ok(ms < 16, 'a frame takes under 16 ms here');
}

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
