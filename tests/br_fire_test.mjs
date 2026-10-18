// tests/br_fire_test.mjs -- BEDROCK RACERS: ON FIRE (NBA Jam / NBA Street), measured on the compiled wasm. The rules (two goals in a
// row lights a car, another team's goal does not let it), the effects (a full tank, faster throws, a longer magnet, a hot pass),
// every way a fire goes out, the light table (the goal flash filling it and dying away, burning cars, the burning ball, plumes,
// the drama scalar), the particles (spawning, ageing, dying), and determinism: a match with fire in it replays bit for bit, and
// the cosmetics never feed back into it.
//   node tests/br_fire_test.mjs <bedrockracers.wasm>
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot, KF } from './br_harness.mjs';
const W = process.argv[2];
if (!W) { console.error('usage: br_fire_test.mjs <wasm>'); process.exit(2); }
let pass = 0, fail = 0;
const ok = (c, m) => { c ? pass++ : fail++; console.log((c ? 'PASS: ' : 'FAIL: ') + m); };
const here = path.dirname(fileURLToPath(import.meta.url));
const memSrc = fs.readFileSync(path.join(here, '../stdlib/bedrockracers/web/br_mem.prn'), 'utf8');
const fireSrc = fs.readFileSync(path.join(here, '../stdlib/bedrockracers/web/br_fire.prn'), 'utf8');
const G = {}; for (const m of memSrc.matchAll(/\(defn g-([a-z0-9-]+) \[\] : I32 (\d+)\)/g)) G[m[1]] = +m[2];
const FEED_BASE = +memSrc.match(/\(defn feed-base \[\] : I32 (\d+)\)/)[1];
const FIRE_TICKS = +fireSrc.match(/\(defn fire-ticks \[\] : I32 (\d+)\)/)[1];
const FO = {}; for (const m of fireSrc.matchAll(/\(defn fo-([a-z-]+) \[\] : I32 (\d+)\)/g)) FO[m[1]] = +m[2];
const DT = 0.016, CRADLE_F = 17, CRADLE_U = 9, TOWER = 450;
const towerXZ = (t) => [t & 1 ? TOWER : -TOWER, t & 2 ? TOWER : -TOWER];

// ---- helpers -------------------------------------------------------------------------------------------------------------------------
const away = (g) => g.x.br_t_ball(0, 120, 480, 0, 0, 0);
function orient(g, s, ux, uy, uz, fx, fy, fz) {
  g.setCar(s, 'ux', ux); g.setCar(s, 'uy', uy); g.setCar(s, 'uz', uz); g.setCar(s, 'fx', fx); g.setCar(s, 'fy', fy); g.setCar(s, 'fz', fz);
  g.setCar(s, 'rx', fy * uz - fz * uy); g.setCar(s, 'ry', fz * ux - fx * uz); g.setCar(s, 'rz', fx * uy - fy * ux);
}
function put(g, s, x, z, fx, fz, team = Math.floor(s / 3)) {
  const l = Math.hypot(fx, fz); g.x.br_t_place(s, x, 4, z); orient(g, s, 0, 1, 0, fx / l, 0, fz / l); g.setCar(s, 'team', team);
}
const cmd = (g, s, thr, magnet, o = {}) =>
  g.x.br_t_cmd(s, thr, o.steer || 0, o.pitch || 0, o.roll || 0, 0, o.lift || 0, o.boost || 0, 0, magnet);
const cradle = (g, s) => [0, 1, 2].map(i => g.pos(s)[i] + g.fwd(s)[i] * CRADLE_F + g.up(s)[i] * CRADLE_U);
const bpos = (g) => [g.ball('x'), g.ball('y'), g.ball('z')];
const bvel = (g) => [g.ball('vx'), g.ball('vy'), g.ball('vz')];
const sub = (a, b) => a.map((v, i) => v - b[i]);
const len = (a) => Math.hypot(...a);
const relSpeed = (g, s) => len(sub(bvel(g), g.vel(s)));
async function bench() { const g = await boot(W); away(g); return g; }
// a match on the rules where every car is a "human" with its command words at zero: nobody moves unless a test moves it
async function quietMatch(seed = 3) { const g = await boot(W); g.x.br_t_begin(seed, 3, 0xfff); return g; }
const burning = (g, s) => g.car(s, 'fire-t') > 0;
// the feed: 8 lines of {code, a, b, tick}; the newest last
function feed(g) {
  const n = g.g(G['feed-head']), dv = g.dv(), out = [];
  for (let i = Math.max(0, n - 8); i < n; i++) {
    const a = FEED_BASE + (i % 8) * 64; out.push({ code: dv.getInt32(a, true), a: dv.getInt32(a + 4, true), b: dv.getInt32(a + 8, true) }); }
  return out;
}
const lastFeed = (g) => feed(g).at(-1) || {};
// the light table
function lights(g) {
  const base = g.x.br_lights_addr(), dv = g.dv(), n = g.x.br_light_count(), out = [];
  for (let i = 0; i < 16; i++) {
    const a = base + 32 + i * 32, f = (k) => dv.getFloat32(a + k * 4, true);
    out.push({ x: f(0), y: f(1), z: f(2), r: f(3), g: f(4), b: f(5), power: f(6), radius: f(7) }); }
  return { n, list: out.slice(0, n), all: out };
}
const nearest = (L, p) => L.list.map(l => ({ l, d: len(sub([l.x, l.y, l.z], p)) })).sort((a, b) => a.d - b.d)[0];
// the particles
function parts(g) {
  const base = g.x.br_parts_addr(), dv = g.dv(), out = [];
  for (let q = 0; q < 256; q++) {
    const a = base + q * 64, i = (k) => dv.getInt32(a + KF[k] * 4, true), f = (k) => dv.getFloat32(a + KF[k] * 4, true);
    if (i('ttl') > 0) out.push({ ttl: i('ttl'), life: i('life'), kind: i('kind'), x: f('x'), y: f('y'), z: f('z'), vy: f('vy'), size: f('size') }); }
  return out;
}

// ---- catching fire -----------------------------------------------------------------------------------------------------------------------
// br_t_score(who, assist, tower) runs the real on-goal! with those two as the last touches. Slots 0..2 are team 0, 3..5 team 1.
{ const g = await quietMatch(); g.x.br_t_score(0, -1, 1);
  const after1 = burning(g, 0) || g.car(0, 'fire-streak') !== 1;
  g.x.br_t_score(0, -1, 1); const f = lastFeed(g);
  ok(!after1 && burning(g, 0) && g.car(0, 'fire-t') === FIRE_TICKS && g.car(0, 'fires') === 1 && g.car(0, 'air') === 100,
    `two goals in a row by the same car: ON FIRE (not after the first; ${FIRE_TICKS} ticks = ${(FIRE_TICKS * DT).toFixed(1)} s, a full tank)`);
  ok(f.code === 3 && f.a === 0 && f.b === 0 && g.g(G['fire-ann-id']) === 1 && g.g(G['fire-ann-slot']) === 0 && g.g(G['fire-ann-t']) > 0,
    '... announced: feed line "0 IS ON FIRE" (code 3, after the goal\'s own line) and the fire banner'); }
{ const g = await quietMatch(); g.x.br_t_score(0, -1, 1); g.x.br_t_score(1, 0, 1);
  ok(burning(g, 0) && !burning(g, 1) && g.car(1, 'fire-streak') === 1,
    'scorer, then the assist on a team-mate\'s goal: the car in on both is on fire, the team-mate (in on one) is not'); }
{ const g = await quietMatch(); g.x.br_t_score(0, -1, 1); g.x.br_t_score(3, -1, 0); g.x.br_t_score(0, -1, 1);
  ok(!burning(g, 0) && g.car(0, 'fire-streak') === 1 && g.car(0, 'fires') === 0,
    'two goals split by ANOTHER team\'s goal: no fire (the streak starts again)'); }
{ const g = await quietMatch(); g.x.br_t_score(0, -1, 1); g.x.br_t_score(1, -1, 1); g.x.br_t_score(0, -1, 1);
  ok(!burning(g, 0) && g.car(0, 'fire-streak') === 1, 'two goals split by a team-mate\'s goal it was not in on: no fire either'); }
{ const g = await quietMatch(); g.x.br_t_score(0, -1, 0); g.x.br_t_score(0, -1, 0);
  ok(!burning(g, 0) && g.car(0, 'fire-streak') === 0, 'own goals never light anyone'); }
// the real path: car 0 dunks into tower 1 twice in a played match (goal pause, restart, kickoff and all)
async function dunk(g, tower) {
  const sx = tower & 1 ? 1 : -1, sz = tower & 2 ? 1 : -1;
  g.x.br_t_place(0, sx * 459, 200, sz * 450); orient(g, 0, -sx, 0, 0, 0, 1, 0); g.setCar(0, 'attached', 0); g.setCar(0, 'vy', 120);
  g.x.br_t_give(0); for (let t = 0; t < 90; t++) { g.x.br_t_play(1); if (g.g(G['goal-t']) > 0) return t; } return -1;
}
{ const g = await quietMatch(); g.x.br_t_setg(G['cmd-magnet'], 1); g.x.br_t_play(5);
  const a = await dunk(g, 1); const mid = burning(g, 0); g.x.br_t_play(300);
  const restarted = g.g(G['goal-t']) === 0 && g.g(G.countdown) === 0 && g.ball('y') < 100;
  const b = await dunk(g, 1);
  ok(a >= 0 && b >= 0 && restarted && !mid && burning(g, 0) && g.g(G.score) === 2 && g.car(0, 'goals') === 2,
    `the real thing: two dunks in a row in a played match (kickoff between them) and car 0 is ON FIRE`); }

// ---- what the fire does ------------------------------------------------------------------------------------------------------------------
// two cars in the air burning lift and boost for 1.5 s: the burning one's tank stays full
{ const g = await bench();
  for (const s of [0, 1]) { g.x.br_t_place(s, s * 200 - 100, 100, -200); orient(g, s, 0, 1, 0, 0, 0, 1); g.setCar(s, 'attached', 0); }
  g.x.br_t_ignite(0); let lo0 = 100;
  for (let t = 0; t < 94; t++) { for (const s of [0, 1]) cmd(g, s, 0, 0, { lift: 0.6, boost: 1 }); g.x.br_t_step(1); lo0 = Math.min(lo0, g.car(0, 'air')); }
  ok(lo0 === 100 && g.car(1, 'air') < 60 && g.car(0, 'air-used') > 40,
    `on fire the tank stays full: lift + boost for 1.5 s leaves it at ${lo0} (a cold car: ${g.car(1, 'air').toFixed(0)}); the spend is still counted`); }
{ const g = await bench(); put(g, 0, 0, -200, 0, 1); g.x.br_t_step(5); g.x.br_t_give(0); g.x.br_t_ignite(0); g.setCar(0, 'air', 50);
  cmd(g, 0, 0, 1); g.x.br_t_step(1);
  ok(g.car(0, 'air') === 100 && g.ball('holder') === 0, '... even while carrying the ball (which normally stops the refill)'); }
// the throw: car velocity + 260 along the nose, 35% faster on fire
async function throwSpeed(fire, auto = false) {
  const g = await bench(); put(g, 0, 0, -300, 0, 1); g.x.br_t_step(5); g.x.br_t_give(0); if (fire) g.x.br_t_ignite(0);
  if (auto) { for (let t = 0; t < 340 && g.ball('holder') === 0; t++) { cmd(g, 0, 0, 1); g.x.br_t_step(1); } }
  else { cmd(g, 0, 0, 1); g.x.br_t_step(3); cmd(g, 0, 0, 0); g.x.br_t_step(1); }
  return { g, sp: relSpeed(g, 0) };
}
{ const cold = await throwSpeed(false), hot = await throwSpeed(true), hotAuto = await throwSpeed(true, true);
  ok(Math.abs(cold.sp - 260) < 3 && Math.abs(hot.sp - 351) < 4 && Math.abs(hotAuto.sp - 175.5) < 4,
    `on fire a throw is 35% faster: ${hot.sp.toFixed(0)} vs ${cold.sp.toFixed(0)} cold (the five-second auto-throw: ${hotAuto.sp.toFixed(0)}, half of it)`);
  ok(hot.g.ball('fire') === 1 && cold.g.ball('fire') === 0, '... and the burning car\'s throw leaves alight (a cold car\'s does not)'); }
// the magnet: a still ball 42 from the cradle -- outside the normal reach (34), inside the burning one (51)
async function farBall(fire) {
  const g = await bench(); put(g, 0, 0, -200, 0, 1); g.x.br_t_step(10); if (fire) g.x.br_t_ignite(0);
  const c = cradle(g, 0); g.x.br_t_ball(c[0], 11, c[2] + 42, 0, 0, 0); const b0 = bpos(g); let caught = -1;
  for (let t = 0; t < 120 && caught < 0; t++) { cmd(g, 0, 0, 1); g.x.br_t_step(1); if (g.ball('holder') === 0) caught = t; }
  return { caught, moved: len(sub(bpos(g), b0)) };
}
{ const cold = await farBall(false), hot = await farBall(true);
  ok(cold.caught < 0 && cold.moved < 1 && hot.caught >= 0 && hot.caught * DT < 1.2,
    `on fire the magnet reaches 50% further: a ball 42 out is reeled in and caught (${(hot.caught * DT).toFixed(2)} s); cold, it never moves (${cold.moved.toFixed(2)})`); }
// a burning car's pass, face to face 80 apart: it comes in fast, and only the team-mate it was thrown to can hold it
async function hotPass(team1) {
  const g = await bench(); put(g, 0, 0, -250, 0, 1); put(g, 1, 0, -136, 0, -1, team1); g.x.br_t_step(5); g.x.br_t_give(0); g.x.br_t_ignite(0);
  cmd(g, 1, 0, 1); cmd(g, 0, 0, 1); g.x.br_t_step(2); cmd(g, 0, 0, 0); g.x.br_t_step(1); cmd(g, 0, 0, 1);
  let caught = -1, arrive = 0;
  for (let t = 0; t < 50 && caught < 0; t++) { arrive = relSpeed(g, 1); g.x.br_t_step(1); if (g.ball('holder') === 1) caught = t; }
  return { g, caught, arrive };
}
{ const mate = await hotPass(0), enemy = await hotPass(1);
  ok(mate.caught >= 0 && mate.g.car(0, 'passes') === 1 && mate.g.ball('fire') === 0 && enemy.caught < 0,
    `a burning car's pass (arriving at ${mate.arrive.toFixed(0)}, over the ordinary 260) is caught by the team-mate, who does not burn it on; an enemy cannot hold it`); }

// ---- going out ---------------------------------------------------------------------------------------------------------------------------
{ const g = await quietMatch(); g.x.br_t_score(0, -1, 1); g.x.br_t_score(0, -1, 1); const lit = burning(g, 0);
  g.x.br_t_score(4, -1, 0); const f = feed(g).at(-1);
  ok(lit && !burning(g, 0) && g.car(0, 'fire-streak') === 0 && f.code === 4 && f.a === 0 && f.b === FO.scored &&
     g.g(G['fire-ann-id']) === 2 && g.g(G['fire-ann-why']) === FO.scored,
    'another team scores: the fire goes out (feed "fire out", reason: another team scored)'); }
// car 0 burning and carrying, parked; car 3 drives into its tail at 150
async function bump(team) {
  const g = await bench(); put(g, 0, 0, 0, 0, 1); put(g, 3, 0, -40, 0, 1, team); g.x.br_t_step(5); g.x.br_t_give(0); g.x.br_t_ignite(0);
  g.setCar(3, 'vz', 150); let loose = -1;
  for (let t = 0; t < 30; t++) { cmd(g, 0, 0, 1); cmd(g, 3, 1, 0); g.x.br_t_step(1); if (loose < 0 && g.ball('holder') !== 0) loose = t; }
  return { g, loose };
}
{ const e = await bump(1), m = await bump(0); const f = feed(e.g).at(-1);
  ok(e.loose >= 0 && !burning(e.g, 0) && f.code === 4 && f.b === FO.stolen && e.g.ball('fire') === 0,
    `losing the ball to a steal (an enemy's tackle, tick ${e.loose}) puts the fire out -- the ball's too`);
  ok(m.loose < 0 && burning(m.g, 0), '... a team-mate\'s bump does not'); }
// an enemy's body knocking the carried ball out of the cradle is a steal as well
{ const g = await bench(); put(g, 0, 0, 0, 0, 1); put(g, 3, 0, 70, 0, -1, 1); g.x.br_t_step(5); g.x.br_t_give(0); g.x.br_t_ignite(0);
  g.setCar(3, 'vz', -100); let loose = -1;
  for (let t = 0; t < 30; t++) { cmd(g, 0, 0, 1); cmd(g, 3, 1, 0); g.x.br_t_step(1); if (loose < 0 && g.ball('holder') !== 0) loose = t; }
  ok(loose >= 0 && !burning(g, 0) && g.car(3, 'steals') === 1, `... and so does an enemy driving into the carried ball (tick ${loose})`); }
{ const g = await bench(); put(g, 0, 0, -200, 0, 1); g.x.br_t_ignite(0);
  g.x.br_t_setg(G['goal-t'], 100); g.x.br_t_step(50); g.x.br_t_setg(G['goal-t'], 0);
  const paused = g.car(0, 'fire-t') === FIRE_TICKS;
  g.x.br_t_step(FIRE_TICKS - 1); const last = burning(g, 0) && g.car(0, 'fire-t') === 1;
  g.x.br_t_step(1); const f = lastFeed(g);
  ok(paused && last && !burning(g, 0) && f.code === 4 && f.b === FO.burnt,
    `45 s of play and it burns out (on tick ${FIRE_TICKS}; the clock stands still while the ball is dead after a goal)`); }
{ const g = await quietMatch(); g.x.br_t_score(0, -1, 1); g.x.br_t_score(0, -1, 1); g.x.br_t_score(0, -1, 0);
  ok(!burning(g, 0) && lastFeed(g).b === FO['own-goal'], 'an own goal puts the culprit\'s fire out'); }
{ const g = await quietMatch(); g.x.br_t_score(0, -1, 1); g.x.br_t_score(0, -1, 1); g.x.br_t_setg(G['goal-t'], 0);
  g.x.br_t_step(1000); const before = g.car(0, 'fire-t');
  g.x.br_t_score(1, 0, 1); const afterAssist = g.car(0, 'fire-t');
  g.x.br_t_score(0, -1, 1); const f = lastFeed(g);
  ok(before === FIRE_TICKS - 1000 && afterAssist === before && g.car(0, 'fire-t') === FIRE_TICKS && f.code === 5 && g.car(0, 'fires') === 1,
    'scoring while on fire stokes it back to the full 45 s ("STILL ON FIRE", not a second fire); an assist does not'); }
{ const g = await bench(); put(g, 0, 0, -200, 0, 1); g.x.br_t_ignite(0); g.setCar(0, 'active', 0); g.x.br_t_step(1);
  ok(!burning(g, 0) && lastFeed(g).b === FO.left, 'a car that leaves the match goes out with it'); }

// ---- the ball alight -----------------------------------------------------------------------------------------------------------------------
{ const g = await bench(); put(g, 0, 0, -400, 0, 1); g.x.br_t_step(5); g.x.br_t_give(0); g.x.br_t_ignite(0); cmd(g, 0, 0, 1); g.x.br_t_step(2);
  const held = g.ball('fire') === 1; cmd(g, 0, 0, 0); g.x.br_t_step(1); cmd(g, 0, 0, 0);
  let out = -1; for (let t = 0; t < 200 && out < 0; t++) { g.x.br_t_step(1); if (g.ball('fire') === 0) out = t; }
  ok(held && Math.abs(out - 124) <= 2, `the ball burns while a burning car holds it, and for ${(out * DT).toFixed(2)} s after its throw`); }
{ const g = await bench(); put(g, 0, 0, -200, 0, 1); put(g, 3, 0, -90, 0, -1); g.x.br_t_step(5); g.x.br_t_give(0); g.x.br_t_ignite(0);
  cmd(g, 0, 0, 1); g.x.br_t_step(1); cmd(g, 0, 0, 0); g.x.br_t_step(1); cmd(g, 0, 0, 0); let hit = -1;
  for (let t = 0; t < 40 && hit < 0; t++) { g.x.br_t_step(1); if (g.ball('touch') === 3) hit = t; }
  ok(hit >= 0 && g.ball('fire') === 0, `a car that is not burning puts the ball out by playing it (it hit car 3 on tick ${hit})`); }

// ---- the light table -----------------------------------------------------------------------------------------------------------------------
// a goal: team 1's car 3 scores into tower 2. The tower flashes white-hot, then teal, and is gone in about a second.
{ const g = await quietMatch(5); g.x.br_t_play(10); const L0 = lights(g);
  g.x.br_t_score(3, -1, 2); g.x.br_t_play(1);
  const L1 = lights(g), [tx, tz] = towerXZ(2), f0 = L1.list[0], fl1 = g.x.br_flash();
  ok(L0.n === 0 && L1.n >= 1 && Math.abs(f0.x - tx) < 1 && Math.abs(f0.z - tz) < 1 && f0.y > 180 && f0.power > 8.5 && f0.radius > 800 && fl1 > 0.98,
    `a goal fills the light table: a flash at the scoring tower (power ${f0.power.toFixed(1)}, radius ${f0.radius.toFixed(0)}, the flash scalar ${fl1.toFixed(2)})`);
  const pw = [f0.power]; let col = null;
  for (let t = 1; t < 70; t++) { g.x.br_t_play(1); const L = lights(g);
    const fl = L.list.find(l => Math.abs(l.x - tx) < 1 && Math.abs(l.z - tz) < 1); pw.push(fl ? fl.power : 0); if (t === 45 && fl) col = fl; }
  const falling = pw.every((p, i) => i === 0 || p <= pw[i - 1]);
  ok(falling && pw[16] < pw[0] * 0.6 && pw[62] > 0 && pw[63] === 0 && g.x.br_flash() === 0,
    `... and it decays: ${pw[0].toFixed(1)} -> ${pw[16].toFixed(2)} after a quarter second -> ${pw[62].toFixed(3)} -> gone on tick 63 (~1 s)`);
  ok(col && col.g > col.r && col.b > col.r && f0.r > 0.7 && f0.g > 0.85 && f0.r - col.r > 0.2,
    `... washed toward white at first (${[f0.r, f0.g, f0.b].map(v => v.toFixed(2))}), the scorer's teal as it dies (${[col.r, col.g, col.b].map(v => v.toFixed(2))})`);
  const d1 = g.x.br_drama(); g.x.br_t_play(300); const d2 = g.x.br_drama();
  ok(d1 > 0.45 && d2 < 0.05 && lights(g).all.every(l => l.power === 0),
    `the drama scalar holds up while the ball is dead (${d1.toFixed(2)}) and ebbs away after the restart (${d2.toFixed(3)}); the table is empty again`); }
// a burning car carrying the ball: a flickering orange-white light on the car, a light on the burning ball, the drama up
{ const g = await bench(); put(g, 0, 0, -200, 0, 1); g.x.br_t_step(5); g.x.br_t_give(0); g.x.br_t_ignite(0);
  const pw = []; let carL = null, ballL = null;
  for (let t = 0; t < 40; t++) { cmd(g, 0, 0, 1); g.x.br_t_step(1); const L = lights(g);
    carL = nearest(L, g.pos(0)); ballL = nearest(L, bpos(g)); pw.push(carL.l.power); }
  const late = pw.slice(20), spread = Math.max(...late) - Math.min(...late);
  ok(carL.d < 6 && carL.l.r === 1 && carL.l.g > carL.l.b && carL.l.g < 0.85 && spread > 0.2 && g.car(0, 'fire-fx') > 0.95,
    `a burning car is lit orange-white (${[carL.l.r, carL.l.g, carL.l.b].map(v => v.toFixed(2))}), flickering (power ${Math.min(...late).toFixed(2)}..${Math.max(...late).toFixed(2)}); fire-fx ${g.car(0, 'fire-fx').toFixed(2)}`);
  ok(ballL.d < 0.5 && ballL.l.power >= 2.2 && g.x.br_drama() > 0.7, `the burning ball carries its own light (power ${ballL.l.power.toFixed(2)}); the drama is up (${g.x.br_drama().toFixed(2)})`); }
// boost plumes: a cold blue-white light just behind each boosting car (a burning car's plume burns orange)
{ const g = await bench();
  for (const s of [0, 1]) { g.x.br_t_place(s, s * 200 - 100, 100, -200); orient(g, s, 0, 1, 0, 0, 0, 1); g.setCar(s, 'attached', 0); }
  g.x.br_t_ignite(1);
  for (let t = 0; t < 10; t++) { for (const s of [0, 1]) cmd(g, s, 0, 0, { boost: 1 }); g.x.br_t_step(1); }
  const L = lights(g), tail = (s) => sub(g.pos(s), g.fwd(s).map(v => v * 10));
  const p0 = nearest(L, tail(0)), p1 = L.list.filter(l => len(sub([l.x, l.y, l.z], tail(1))) < 1)[0];
  ok(p0.d < 1 && p0.l.b > p0.l.r && p0.l.radius === 70 && p1 && p1.r > p1.b,
    `boost plumes are lit: blue-white behind a cold car (${[p0.l.r, p0.l.g, p0.l.b].map(v => v.toFixed(2))}), orange behind a burning one`); }
// more candidates than slots: never more than 16 lights, and the goal flash and the burning ball always make it
{ const g = await quietMatch(7); g.x.br_t_score(0, -1, 1);
  for (let s = 0; s < 12; s++) { g.x.br_t_ignite(s); g.setCar(s, 'attached', 0); g.setCar(s, 'y', 120); g.x.br_t_cmd(s, 0, 0, 0, 0, 0, 0, 1, 0, 1); }
  g.x.br_t_give(0); g.x.br_t_step(30); const L = lights(g), [tx, tz] = towerXZ(1);
  ok(L.n === 16 && Math.abs(L.list[0].x - tx) < 1 && L.list[0].power > 0.5 && L.list.filter(l => l.r === 1).length >= 14,
    `twelve burning cars and a goal: the table is full at 16, the goal flash first, then the fires (${L.n} lights)`); }

// ---- the particles -----------------------------------------------------------------------------------------------------------------------
{ const g = await bench(); put(g, 0, 0, -200, 0, 1); g.x.br_t_step(2); const none = g.x.br_part_live() === 0; g.x.br_t_ignite(0);
  g.x.br_t_step(40); const P = parts(g), fire = P.filter(p => p.kind === 1);
  const close = fire.every(p => len(sub([p.x, p.y, p.z], g.pos(0))) < 40), aged = P.every(p => p.ttl <= p.life && p.ttl > 0);
  ok(none && fire.length > 30 && fire.length === g.x.br_part_live() && close && aged,
    `a burning car throws off flames: ${fire.length} live fire particles round it (none before it caught fire), each ageing through its life`);
  const rising = fire.filter(p => p.y > g.car(0, 'y') + 8).length;
  ok(rising > 5, `... and they rise off it (${rising} above the roof)`);
  put(g, 3, 200, 200, 1, 0, 1); g.x.br_t_score(3, -1, 0); g.x.br_t_setg(G['goal-t'], 0);   // another team scores: out
  const t0 = g.x.br_part_live(); let gone = -1;
  for (let t = 0; t < 400 && gone < 0; t++) { g.x.br_t_step(1); if (g.x.br_part_live() === 0) gone = t; }
  ok(!burning(g, 0) && t0 > 0 && gone > 20 && gone < 220,
    `once the fire is out the flames die down and the last particle dies ${(gone * DT).toFixed(2)} s later`); }
{ const g = await bench(); g.x.br_t_place(0, 0, 100, -200); orient(g, 0, 0, 1, 0, 0, 0, 1); g.setCar(0, 'attached', 0);
  for (let t = 0; t < 20; t++) { cmd(g, 0, 0, 0, { boost: 1 }); g.x.br_t_step(1); }
  const P = parts(g).filter(p => p.kind === 2), behind = P.filter(p => p.z < g.car(0, 'z') - 5).length;
  ok(P.length > 5 && behind === P.length, `a boosting car leaves a plume: ${P.length} plume particles, all behind it`); }
{ const g = await bench(); let rx = 0, rz = 0;   // somewhere on the dirt track
  for (let x = -400; x <= 400 && !rx; x += 20) for (let z = -400; z <= 400; z += 20) if (g.x.br_t_road(x, z) > 240 && g.x.br_t_road(x + 30, z) > 240) { rx = x; rz = z; break; }
  put(g, 0, rx, rz, 1, 0); g.x.br_t_step(2); g.setCar(0, 'vx', 120);
  for (let t = 0; t < 15; t++) { cmd(g, 0, 1, 0); g.x.br_t_step(1); }
  const P = parts(g).filter(p => p.kind === 3);
  ok(P.length > 0 && P.every(p => p.y < 20), `a car on the dirt track kicks up dust (${P.length} dust particles near the floor at ${rx},${rz})`); }
{ const g = await quietMatch(5); g.x.br_t_play(5); g.x.br_t_score(3, -1, 2); g.x.br_t_play(10);
  const P = parts(g).filter(p => p.kind === 4), [tx, tz] = towerXZ(2);
  const near = P.filter(p => Math.hypot(p.x - tx, p.z - tz) < 120 && p.y < 240).length;
  ok(P.length === 40 && near === 40 && P.filter(p => p.vy < 0).length > 30, `a goal sprays 40 sparks down out of the tower into the arena`); }
{ const g = await bench(); for (let s = 0; s < 12; s++) { g.x.br_t_place(s, (s % 4) * 100 - 150, 100, Math.floor(s / 4) * 100 - 100);
    orient(g, s, 0, 1, 0, 0, 0, 1); g.setCar(s, 'attached', 0); g.x.br_t_ignite(s); }
  let worst = 0; for (let t = 0; t < 120; t++) { for (let s = 0; s < 12; s++) cmd(g, s, 0, 0, { boost: 1 }); g.x.br_t_step(1); worst = Math.max(worst, g.x.br_part_live()); }
  ok(worst <= 256 && worst > 200 && parts(g).length === g.x.br_part_live(),
    `twelve burning, boosting cars: the 256-particle ring stays bounded (peak ${worst}) by recycling the oldest slots`); }

// ---- determinism ---------------------------------------------------------------------------------------------------------------------------
// a bot match where car 0 is lit (two goals by it, through the real on-goal!) and plays on burning. Run it twice in fresh
// modules: the match and the cosmetics both come out the same. Run it a third time with the fx dice reseeded mid-match: the
// cosmetics differ, the match does not -- the fx never feed back.
async function fireMatch(fxSeed) {
  const g = await boot(W); g.x.br_t_begin(11, 3, 0); g.x.br_t_play(300);
  g.x.br_t_score(0, -1, 1); g.x.br_t_play(400); g.x.br_t_score(0, 1, 1);
  if (fxSeed) g.x.br_t_setg(G.rng2, fxSeed);
  const lit = burning(g, 0); g.x.br_t_play(1500);
  let sim = 0; const mix = (v) => { sim = (sim * 31 + Math.round(v * 1000)) | 0; };
  for (let s = 0; s < 12; s++) for (const k of ['x', 'y', 'z', 'vx', 'vy', 'vz', 'air', 'fire-t', 'fire-streak', 'fires', 'touches', 'catches']) mix(g.car(s, k));
  for (const k of ['x', 'y', 'z', 'vx', 'vy', 'vz', 'holder', 'fire', 'fire-t']) mix(g.ball(k));
  for (let t = 0; t < 4; t++) mix(g.g(G.score + t)); mix(g.g(G.tick));
  const u8 = new Uint8Array(g.x.memory.buffer); let fx = 0;
  for (const [a, n] of [[g.x.br_parts_addr(), 256 * 64], [g.x.br_lights_addr(), 544]]) for (let i = 0; i < n; i++) fx = (fx * 31 + u8[a + i]) | 0;
  for (let s = 0; s < 12; s++) fx = (fx * 31 + Math.round(g.car(s, 'fire-fx') * 1e6)) | 0;
  return { lit, sim, fx, air: g.car(0, 'air'), parts: g.x.br_part_live() };
}
{ const a = await fireMatch(0), b = await fireMatch(0), c = await fireMatch(12345);
  ok(a.lit && a.sim === b.sim && a.fx === b.fx && a.parts > 0,
    `a bot match with a car on fire runs the same twice: the match (${a.sim}) and the cosmetics (${a.fx}) bit for bit`);
  ok(c.sim === a.sim && c.fx !== a.fx, `the fx dice reseeded mid-match: the cosmetics change (${c.fx}), the match does not (${c.sim})`); }

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
