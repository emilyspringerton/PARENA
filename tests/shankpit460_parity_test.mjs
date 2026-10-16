// tests/shankpit460_parity_test.mjs -- the shankpit-460 wasm engine vs shankpit-460's own C engine.
//
//   node tests/shankpit460_parity_test.mjs <shankpit460.wasm> <parity_ref binary> [map file]
//
// Both engines run the same scripted scenarios (movement, jumping, crouch-slides, box collisions, every
// weapon, the katana dash, the sniper's storm charges) and their per-player state is compared at every
// checkpoint. The C side is shankpit-460's real physics.h / net_sim.h / local_game.h -- the online movement
// path the live server runs. Intended deviations (walls stop bullets, delayed projectile deaths, respawn
// placement) are kept out of these scenarios: nobody dies and every shot has a clear line.
import fs from 'node:fs';
import { spawnSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const [wasmPath, refPath, mapPath = '/tmp/claude-1000/src/shankpit-460/maps/v0_shankpit.map'] = process.argv.slice(2);
if (!wasmPath || !refPath) { console.error('usage: shankpit460_parity_test.mjs <wasm> <ref> [map]'); process.exit(2); }
const here = path.dirname(fileURLToPath(import.meta.url));

// word indices of the player record, from the generated accessors
const fields = {};
for (const m of fs.readFileSync(path.join(here, '../stdlib/shankpit460/sp_fields.prn'), 'utf8').matchAll(/\(defn k-([a-z0-9-]+) \[\] : I32 (\d+)\)/g)) fields[m[1]] = +m[2];

const { instance } = await WebAssembly.instantiate(fs.readFileSync(wasmPath), {});
const ex = instance.exports;
const mem = () => new DataView(ex.memory.buffer);
const fget = (p, name) => mem().getFloat32(ex.sp_player_addr(p) + fields[name] * 4, true);
const iget = (p, name) => mem().getInt32(ex.sp_player_addr(p) + fields[name] * 4, true);

function wasmRun(lines) {
  const dumps = [];
  let active = [];
  for (const line of lines) {
    const t = line.trim().split(/\s+/);
    switch (t[0]) {
      case 'reset': ex.sp_t_reset(); active = []; break;
      case 'seed': ex.sp_t_seed(+t[1]); break;
      case 'spawn': ex.sp_t_spawn(+t[1], Math.fround(+t[2]), Math.fround(+t[3]), Math.fround(+t[4])); if (!active.includes(+t[1])) active.push(+t[1]); break;
      case 'cmd': ex.sp_t_cmd(+t[1], Math.fround(+t[2]), Math.fround(+t[3]), Math.fround(+t[4]), Math.fround(+t[5]), +t[6], +t[7]); break;
      case 'step': ex.sp_t_step(+t[1]); break;
      case 'dump': {
        const rows = [];
        for (const p of active.slice().sort((a, b) => a - b)) {
          rows.push({
            p, pos: ['x', 'y', 'z'].map((k) => fget(p, k)), vel: ['vx', 'vy', 'vz'].map((k) => fget(p, k)),
            ang: [fget(p, 'yaw'), fget(p, 'pitch')], hp: iget(p, 'health'), sh: iget(p, 'shield'), st: iget(p, 'state'),
            k: iget(p, 'kills'), d: iget(p, 'deaths'), wpn: iget(p, 'weapon'),
            ammo: [0, 1, 2, 3, 4, 5].map((w) => iget(p, 'ammo' + w)), rt: iget(p, 'reload-timer'),
            ac: iget(p, 'attack-cooldown'), dash: iget(p, 'dash-timer'), storm: iget(p, 'storm-charges'), abc: iget(p, 'ability-cooldown'),
          });
        }
        dumps.push(rows);
        break;
      }
    }
  }
  return dumps;
}

function refRun(lines) {
  const r = spawnSync(refPath, [mapPath], { input: lines.join('\n') + '\n', encoding: 'utf8', maxBuffer: 1 << 28 });
  if (r.status !== 0) throw new Error('ref failed: ' + r.stderr);
  const dumps = []; let cur = [];
  for (const l of r.stdout.split('\n')) {
    if (l === 'END') { dumps.push(cur); cur = []; continue; }
    const t = l.trim().split(/\s+/);
    if (t[0] !== 'P') continue;
    const n = (i) => +t[i];
    cur.push({ p: n(1), pos: [n(3), n(4), n(5)], vel: [n(7), n(8), n(9)], ang: [n(11), n(12)], hp: n(14), sh: n(16), st: n(18), k: n(20), d: n(22), wpn: n(24),
      ammo: [n(26), n(27), n(28), n(29), n(30), n(31)], rt: n(33), ac: n(35), dash: n(37), storm: n(39), abc: n(41) });
  }
  return dumps;
}

let worst = 0, checked = 0, bad = 0, scen = 0;
const worstBy = {};
let truncated = 0;
function compare(name, a, b) {
  // The engines deliberately differ once somebody dies (the C engine respawns a sniper-storm kill in the same tick and
  // puts a respawn on spawns[id % n]; this one delays the death and picks the safest spawn), so a scenario is compared up
  // to the first checkpoint where a death appears on either side.
  const cut = a.findIndex((rows, i) => rows.some((r) => r.d > 0) || b[i].some((r) => r.d > 0));
  if (cut >= 0) { a = a.slice(0, cut); b = b.slice(0, cut); truncated++; }
  if (a.length !== b.length) { console.log(`FAIL ${name}: ${a.length} dumps vs ${b.length}`); bad++; return; }
  for (let i = 0; i < a.length; i++) {
    if (a[i].length !== b[i].length) { console.log(`FAIL ${name}@${i}: player counts differ`); bad++; return; }
    for (let j = 0; j < a[i].length; j++) {
      const x = a[i][j], y = b[i][j];
      const diffs = [];
      for (const key of ['pos', 'vel', 'ang']) for (let k = 0; k < x[key].length; k++) {
        const d = Math.abs(x[key][k] - y[key][k]);
        const tol = 2e-4 + 2e-6 * Math.abs(y[key][k]);
        worst = Math.max(worst, d); worstBy[name] = Math.max(worstBy[name] || 0, d);
        if (d > tol) diffs.push(`${key}[${k}] wasm ${x[key][k]} c ${y[key][k]}`);
      }
      for (const key of ['hp', 'sh', 'st', 'k', 'd', 'wpn', 'rt', 'ac', 'dash', 'storm', 'abc']) if (x[key] !== y[key]) diffs.push(`${key} wasm ${x[key]} c ${y[key]}`);
      for (let k = 0; k < 6; k++) if (x.ammo[k] !== y.ammo[k]) diffs.push(`ammo${k} wasm ${x.ammo[k]} c ${y.ammo[k]}`);
      checked++;
      if (diffs.length) { console.log(`FAIL ${name} checkpoint ${i} player ${x.p}: ${diffs.slice(0, 5).join('; ')}`); bad++; return; }
    }
  }
}

// ---- scenario builders ---------------------------------------------------------------------------------
let seed = 12345;
const rnd = () => { seed = (seed * 1103515245 + 12345) >>> 0; return ((seed >>> 8) & 0xffff) / 65536; };
const pick = (...xs) => xs[Math.floor(rnd() * xs.length)];
const F = (v) => String(Math.fround(v));

function moveScenario(name, start, ticks, opts = {}) {
  const L = ['reset', 'seed 7', `spawn 0 ${start.join(' ')}`];
  let yaw = opts.yaw ?? 0;
  for (let t = 0; t < ticks; t += 6) {
    yaw += pick(-30, -10, 0, 0, 5, 20, 90) * (opts.turny ?? 1);
    const fwd = pick(1, 1, 1, 0, -1, 0.5), str = pick(0, 0, 1, -1, 0.7);
    const buttons = (rnd() < 0.3 ? 1 : 0) | (rnd() < (opts.crouchP ?? 0.15) ? 4 : 0);
    L.push(`cmd 0 ${fwd} ${str} ${F(yaw)} ${F(pick(-20, 0, 10))} ${buttons} -1`, 'step 6', 'dump');
  }
  return [name, L];
}

const scenarios = [];
scenarios.push(moveScenario('open-field walk', [-260, 0.5, -40], 600));
scenarios.push(moveScenario('slide + jump', [270, 0.5, 60], 600, { crouchP: 0.5 }));
scenarios.push(moveScenario('into the rocks', [-150, 0.5, 150], 900, { turny: 0.3 }));
scenarios.push(moveScenario('north base', [0, 6.5, -330], 900));
scenarios.push(moveScenario('mid tower', [30, 4.5, 20], 900, { turny: 0.4 }));
scenarios.push(moveScenario('south base', [6, 6.5, 332], 900));
scenarios.push(moveScenario('perimeter wall', [380, 0.5, 0], 600, { yaw: 90, turny: 0.1 }));
for (let i = 0; i < 6; i++) scenarios.push(moveScenario('random walk ' + i, [(rnd() - 0.5) * 600, 0.5, (rnd() - 0.5) * 600], 700, { turny: 1 }));

// held vs tapped jump: the double-gravity arc of the online path
scenarios.push(['jump arcs', ['reset', 'spawn 0 -260 0.5 -40',
  'cmd 0 0 0 0 0 1 -1', 'step 40', 'dump', 'cmd 0 0 0 0 0 0 -1', 'step 20', 'dump',
  'cmd 0 0 0 0 0 1 -1', 'step 1', 'dump', 'cmd 0 0 0 0 0 0 -1', 'step 30', 'dump',
  'cmd 0 1 0 0 0 5 -1', 'step 60', 'dump']]);

// weapons: shooter 0 at the origin of a clear lane, target 1 straight ahead (yaw 0 faces -z)
function shootScenario(name, wpn, dist, buttons, ticks, extra = []) {
  const L = ['reset', 'seed 99', 'spawn 0 -260 0.5 -40', `spawn 1 -260 0.5 ${-40 - dist}`,
    `cmd 0 0 0 0 0 0 ${wpn}`, 'step 1', 'dump'];
  for (let t = 0; t < ticks; t += 5) L.push(`cmd 0 0 0 0 0 ${buttons} ${wpn}`, 'step 5', 'dump');
  return [name, L.concat(extra)];
}
for (const w of [0, 1, 2, 3, 4, 5]) for (const d of [3, 12, 40]) scenarios.push(shootScenario(`weapon ${w} @${d}`, w, d, 2, 90));
scenarios.push(shootScenario('magnum reload', 1, 12, 2 | 8, 160));
scenarios.push(shootScenario('ar spray', 2, 25, 2, 120));
scenarios.push(shootScenario('shotgun at point blank', 3, 4, 2, 60));
scenarios.push(shootScenario('sniper storm', 4, 60, 2 | 32, 220));
scenarios.push(shootScenario('katana dash', 5, 14, 32, 40));
scenarios.push(shootScenario('katana dash + slash', 5, 8, 32 | 2, 60));
// aimed shots: target off the lane
scenarios.push(['aimed pitch', ['reset', 'seed 3', 'spawn 0 -260 0.5 -40', 'spawn 1 -260 0.5 -80',
  ...Array.from({ length: 12 }, (_, i) => [`cmd 0 0 0 ${i * 0.7} ${-i * 0.3} 2 1`, 'step 25', 'dump']).flat()]]);
// a target walking through the line of fire
scenarios.push(['moving target', ['reset', 'seed 5', 'spawn 0 -260 0.5 -40', 'spawn 1 -240 0.5 -90',
  'cmd 1 0 -1 0 0 0 1',
  ...Array.from({ length: 16 }, () => ['cmd 0 0 0 340 0 2 2', 'step 6', 'dump']).flat()]]);

for (const [name, lines] of scenarios) {
  scen++;
  compare(name, wasmRun(lines), refRun(lines));
}
console.log(`${scen} scenarios (${truncated} compared up to the first death), ${checked} player-checkpoints compared, worst |diff| ${worst.toExponential(2)}${bad ? '' : ''}`);
if (bad) { console.log(`${bad} scenario(s) FAILED`); process.exit(1); }
console.log('parity: all scenarios match');
