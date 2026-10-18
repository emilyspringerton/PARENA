// tests/br_magnet_test.mjs -- BEDROCK RACERS: the magnet (the ball as a Quaffle: catch, carry, throw, pass, the re-catch lockout,
// tackles, the five-second clock, dunks, the resets, the command byte and the replay), measured on the compiled wasm.
//   node tests/br_magnet_test.mjs <bedrockracers.wasm>
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot, K } from './br_harness.mjs';
const W = process.argv[2];
if (!W) { console.error('usage: br_magnet_test.mjs <wasm>'); process.exit(2); }
let pass = 0, fail = 0;
const ok = (c, m) => { c ? pass++ : fail++; console.log((c ? 'PASS: ' : 'FAIL: ') + m); };
// the global word indices, by name, straight from the memory map
const memSrc = fs.readFileSync(path.join(path.dirname(fileURLToPath(import.meta.url)), '../stdlib/bedrockracers/web/br_mem.prn'), 'utf8');
const G = {}; for (const m of memSrc.matchAll(/\(defn g-([a-z0-9-]+) \[\] : I32 (\d+)\)/g)) G[m[1]] = +m[2];
const DT = 0.016;
const CRADLE_F = 17, CRADLE_U = 9;

const away = (g) => g.x.br_t_ball(0, 120, 480, 0, 0, 0);   // park the ball against the far wall, out of the way
function orient(g, s, ux, uy, uz, fx, fy, fz) {
  g.setCar(s, 'ux', ux); g.setCar(s, 'uy', uy); g.setCar(s, 'uz', uz); g.setCar(s, 'fx', fx); g.setCar(s, 'fy', fy); g.setCar(s, 'fz', fz);
  g.setCar(s, 'rx', fy * uz - fz * uy); g.setCar(s, 'ry', fz * ux - fx * uz); g.setCar(s, 'rz', fx * uy - fy * ux);
}
// a car on the floor at (x, z) facing (fx, fz), on team `team`
function put(g, s, x, z, fx, fz, team = 0) {
  const l = Math.hypot(fx, fz); g.x.br_t_place(s, x, 4, z); orient(g, s, 0, 1, 0, fx / l, 0, fz / l); g.setCar(s, 'team', team);
}
const cmd = (g, s, thr, magnet, o = {}) => g.x.br_t_cmd(s, thr, o.steer || 0, o.pitch || 0, o.roll || 0, 0, o.lift || 0, 0, 0, magnet);
const cradle = (g, s) => [0, 1, 2].map(i => g.pos(s)[i] + g.fwd(s)[i] * CRADLE_F + g.up(s)[i] * CRADLE_U);
const bpos = (g) => [g.ball('x'), g.ball('y'), g.ball('z')];
const bvel = (g) => [g.ball('vx'), g.ball('vy'), g.ball('vz')];
const sub = (a, b) => a.map((v, i) => v - b[i]);
const len = (a) => Math.hypot(...a);
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const relSpeed = (g, s) => len(sub(bvel(g), g.vel(s)));
const yaw = (v) => Math.atan2(v[0], v[2]) * 180 / Math.PI;
async function world() { const g = await boot(W); away(g); return g; }

// ---- catching ----------------------------------------------------------------------------------------------------------------
// a parked car, the ball lying still on the floor 25 ahead of its cradle
async function nearBall(magnet) {
  const g = await world(); put(g, 0, 0, -200, 0, 1); g.x.br_t_step(10);
  const c = cradle(g, 0); g.x.br_t_ball(c[0], 11, c[2] + 25, 0, 0, 0); const b0 = bpos(g);
  let caught = -1;
  for (let t = 0; t < 90 && caught < 0; t++) { cmd(g, 0, 0, magnet); g.x.br_t_step(1); if (g.ball('holder') === 0) caught = t; }
  return { g, caught, moved: len(sub(bpos(g), b0)) };
}
{ const { g, caught } = await nearBall(1);
  ok(caught >= 0 && caught * DT < 1.0, `the magnet reels in a still ball 25 from the cradle and catches it (${(caught * DT).toFixed(2)} s)`);
  ok(len(sub(bpos(g), cradle(g, 0))) < 0.5 && g.car(0, 'catches') === 1 && g.ball('touch') === 0,
    'the caught ball sits in the cradle; the catch is counted and is a touch'); }
{ const { g, caught, moved } = await nearBall(0);
  ok(caught < 0 && g.ball('holder') === -1 && moved < 1, `with the magnet off nothing is pulled or caught (the ball moved ${moved.toFixed(2)})`); }

// a ball sent straight at the cradle of a parked car with its magnet on
async function shot(v) {
  const g = await world(); put(g, 0, 0, -200, 0, 1); g.x.br_t_step(10);
  const c = cradle(g, 0); g.x.br_t_ball(c[0], c[1], c[2] + 70, 0, 0, -v);
  let caught = -1, fumbled = -1;
  for (let t = 0; t < 60; t++) { cmd(g, 0, 0, 1); g.x.br_t_step(1);
    if (caught < 0 && g.ball('holder') === 0) caught = t; if (fumbled < 0 && g.car(0, 'nocatch') > 0) fumbled = t; }
  return { g, caught, fumbled };
}
{ const { caught } = await shot(130); ok(caught >= 0, 'a ball thrown at the cradle at 130 is caught'); }
{ const { g, caught, fumbled } = await shot(300);
  ok(caught < 0 && fumbled >= 0 && g.car(0, 'touches') >= 1,
    `a ball at 300 is too hot: fumbled (tick ${fumbled}), never caught, and it hits the car as before`); }
// a ball that passes NEAR the cradle (not through it), from the side: a hot one goes straight past -- the field cannot hold a ball
// much faster than it could catch (review: it used to bend a 400 shot passing 14 off, or a 500 one passing 10 off, round, brake it
// and catch it on the way back, so a keeper with the button down caught anything that came close)
async function nearMiss(v, off) {
  const g = await world(); put(g, 0, 0, -200, 0, 1); g.x.br_t_step(10);
  const c = cradle(g, 0); g.x.br_t_ball(c[0] - 90, c[1], c[2] + off, v, 0, 0);
  let caught = -1, past = 0;
  for (let t = 0; t < 120 && caught < 0; t++) { cmd(g, 0, 0, 1); g.x.br_t_step(1); if (g.ball('holder') === 0) caught = t;
    if (!past && g.ball('x') > c[0] + 40) past = g.ball('vx'); }
  return { g, caught, past };
}
{ const r = [await nearMiss(260, 14), await nearMiss(400, 14), await nearMiss(500, 10)], slow = await nearMiss(150, 14);
  ok(r.every(q => q.caught < 0 && q.past > 200) && slow.caught >= 0,
    `hard shots passing 10-14 off the cradle fly on past it uncaught (260, 400, 500 -> ${r.map(q => q.past.toFixed(0))} beyond it); a slow one (150) is reeled in`); }

// ---- carrying -----------------------------------------------------------------------------------------------------------------
{ const g = await world(); put(g, 0, 0, -300, 0, 1); g.x.br_t_step(5); g.x.br_t_give(0); const z0 = g.car(0, 'z');
  let held = true, worst = 0, lowest = 99;
  for (let t = 0; t < 150; t++) { cmd(g, 0, 1, 1); g.x.br_t_step(1);
    held = held && g.ball('holder') === 0; worst = Math.max(worst, len(sub(bpos(g), cradle(g, 0)))); lowest = Math.min(lowest, g.ball('y')); }
  ok(held && worst < 0.5 && g.car(0, 'z') - z0 > 200,
    `carried on the floor: it rides the cradle (off by ${worst.toFixed(2)} at worst) over ${(g.car(0, 'z') - z0).toFixed(0)} units`);
  ok(lowest > 11.5, `... clear of the floor (centre never below ${lowest.toFixed(1)}, radius 11)`); }
{ const g = await world(); put(g, 0, 300, 0, 1, 0); g.x.br_t_step(5); g.setCar(0, 'vx', 100); g.x.br_t_give(0);
  let held = true, worst = 0, climbed = false;
  for (let t = 0; t < 300 && !climbed; t++) { cmd(g, 0, 1, 1); g.x.br_t_step(1);
    held = held && g.ball('holder') === 0; worst = Math.max(worst, len(sub(bpos(g), cradle(g, 0))));
    if (g.car(0, 'y') > 130 && g.car(0, 'uy') < 0.3 && g.car(0, 'attached') === 1) climbed = true; }
  ok(climbed && held && worst < 2, `carried up a wall: still held at the top of the wall (off the cradle by ${worst.toFixed(2)} at worst)`); }
{ const g = await world(); g.x.br_t_place(0, 0, 120, -200); orient(g, 0, 0, 1, 0, 0, 0, 1); g.setCar(0, 'attached', 0);
  g.setCar(0, 'vy', 30); g.setCar(0, 'vz', 80); g.x.br_t_give(0); let held = true, worst = 0, flew = true;
  for (let t = 0; t < 60; t++) { cmd(g, 0, 0, 1, { lift: 0.5, pitch: 0.6, roll: 0.3 }); g.x.br_t_step(1);
    held = held && g.ball('holder') === 0; worst = Math.max(worst, len(sub(bpos(g), cradle(g, 0)))); flew = flew && g.car(0, 'attached') === 0; }
  const turned = Math.acos(Math.max(-1, Math.min(1, g.car(0, 'fz')))) * 180 / Math.PI;
  ok(flew && held && worst < 0.5 && turned > 45,
    `carried in flight while the car pitches and rolls ${turned.toFixed(0)} deg: it stays in the cradle (off by ${worst.toFixed(2)})`); }

// ---- throwing -----------------------------------------------------------------------------------------------------------------
{ const g = await world(); put(g, 0, 0, -350, 0, 1); g.x.br_t_step(5); g.x.br_t_give(0);
  for (let t = 0; t < 40; t++) { cmd(g, 0, 1, 1); g.x.br_t_step(1); }
  const v0 = g.vel(0), f0 = g.fwd(0); cmd(g, 0, 1, 0); g.x.br_t_step(1);
  const want = v0.map((v, i) => v + f0[i] * 260), err = len(sub(bvel(g), want));
  ok(g.ball('holder') === -1 && err < 6 && g.car(0, 'throws') === 1,
    `letting go throws it along the nose: car ${len(v0).toFixed(0)} + 260 -> ball ${len(bvel(g)).toFixed(0)} (off by ${err.toFixed(1)})`); }
// the thrower presses again at once: the ball, put straight back into its cradle, is not caught until the lockout is over
{ const g = await world(); put(g, 0, 0, -200, 0, 1); g.x.br_t_step(5); g.x.br_t_give(0);
  cmd(g, 0, 0, 0); g.x.br_t_step(1); cmd(g, 0, 0, 1);
  const c = cradle(g, 0); g.x.br_t_ball(c[0], c[1], c[2], 0, 0, 0);
  let early = false; for (let t = 0; t < 30; t++) { g.x.br_t_step(1); early = early || g.ball('holder') === 0; }
  let later = -1; for (let t = 30; t < 90 && later < 0; t++) { g.x.br_t_step(1); if (g.ball('holder') === 0) later = t; }
  ok(!early && later >= 30 && later * DT < 0.85,
    `no instant re-catch: the ball back in the thrower's cradle is not caught for ${(later * DT).toFixed(2)} s (lockout 0.7 s)`); }

// ---- passing ------------------------------------------------------------------------------------------------------------------
// car 0 throws with team-mate car 1 crossing ahead of it at 80, 17 degrees off its nose
{ const g = await world(); put(g, 0, 0, -250, 0, 1); put(g, 1, -60, -30, 1, 0, 0); g.x.br_t_step(5); g.setCar(1, 'vx', 80);
  g.x.br_t_give(0);
  for (let t = 0; t < 3; t++) { cmd(g, 0, 0, 1); cmd(g, 1, 0.08, 1); g.x.br_t_step(1); }
  const bearing = yaw(sub(cradle(g, 1), bpos(g)));
  cmd(g, 0, 0, 0); g.x.br_t_step(1); cmd(g, 0, 0, 1);
  const out = yaw(sub(bvel(g), g.vel(0))), sp = relSpeed(g, 0);
  ok(out - bearing > 10 && Math.abs(sp - 260) < 4,
    `a pass to a team-mate in the nose cone is led: thrown ${(out - bearing).toFixed(1)} deg ahead of where it is, at the same 260`);
  let caught = -1; for (let t = 0; t < 90 && caught < 0; t++) { cmd(g, 1, 0.08, 1); g.x.br_t_step(1); if (g.ball('holder') === 1) caught = t; }
  ok(caught >= 0 && g.car(0, 'passes') === 1 && g.car(1, 'catches') === 1 && g.ball('touch2') === 0,
    `... and the team-mate catches it on the move (${(caught * DT).toFixed(2)} s): a pass for the passer, the passer is touch2`); }
// face to face, 80 apart: the throw comes in near 190 -- inside a team-mate's 260 pass window, over anyone else's 170
async function faceToFace(team1) {
  const g = await world(); put(g, 0, 0, -250, 0, 1); put(g, 1, 0, -136, 0, -1, team1); g.x.br_t_step(5); g.x.br_t_give(0);
  cmd(g, 1, 0, 1); cmd(g, 0, 0, 1); g.x.br_t_step(2); cmd(g, 0, 0, 0); g.x.br_t_step(1); cmd(g, 0, 0, 1);
  let caught = -1; for (let t = 0; t < 50 && caught < 0; t++) { g.x.br_t_step(1); if (g.ball('holder') === 1) caught = t; }
  return { g, caught };
}
{ const a = await faceToFace(0), b = await faceToFace(1);
  ok(a.caught >= 0 && a.g.car(0, 'passes') === 1 && b.caught < 0 && b.g.car(1, 'touches') >= 1,
    'a team-mate\'s pass is easier to catch: the same throw is caught by a team-mate and fumbled off an enemy\'s nose'); }
// a team-mate in the cone that is NOT holding its magnet (not calling for it -- every bot, for now) could never catch a pass, so
// the throw is not bent toward it: it goes where the thrower aimed, along the nose (review: any team-mate in the cone took the
// throw, so a shot up into a tower with a bot team-mate floating ahead was passed to the bot instead)
async function coneMate(mag) {
  const g = await world(); put(g, 0, 0, -250, 0, 1); put(g, 1, -40, -100, 0, 1, 0); g.x.br_t_step(5); g.x.br_t_give(0);
  cmd(g, 0, 0, 1); cmd(g, 1, 0, mag); g.x.br_t_step(2); const v0 = g.vel(0);
  cmd(g, 0, 0, 0); cmd(g, 1, 0, mag); g.x.br_t_step(1);
  return yaw(sub(bvel(g), v0));
}
{ const off = await coneMate(0), on = await coneMate(1), mate = yaw([-40, 0, 150]);
  ok(Math.abs(off) < 1 && Math.abs(on - mate) < 2,
    `a team-mate 15 deg off the nose gets the pass only while it holds its magnet (${on.toFixed(1)} deg); not calling for it, the throw goes on along the nose (${off.toFixed(1)} deg)`); }
// a HAND-OFF: team-mates nose to nose 40 apart, so the pass is caught a tick after it is thrown. A catch is always a new touch, so
// the passer is the assist (review: inside the three-tick "same touch" window the catch left touch2 at whoever had the ball
// before the passer -- car 2 here would have been credited with the assist -- and the catcher's touch went uncounted)
{ const g = await world(); put(g, 0, 0, -250, 0, 1); put(g, 1, 0, -210, 0, -1, 0); put(g, 2, -200, 0, 0, 1, 0); g.x.br_t_step(3);
  g.x.br_t_give(2); cmd(g, 2, 0, 0); g.x.br_t_step(10);            // car 2 had it first (its button is up: it throws at once)
  g.x.br_t_give(0); cmd(g, 0, 0, 1); cmd(g, 1, 0, 1); g.x.br_t_step(6);
  const before = g.ball('touch2'); cmd(g, 0, 0, 0); g.x.br_t_step(1); cmd(g, 0, 0, 1); const t0 = g.g(0);
  let caught = -1; for (let t = 0; t < 30 && caught < 0; t++) { g.x.br_t_step(1); if (g.ball('holder') === 1) caught = g.g(0) - t0; }
  ok(before === 2 && caught >= 0 && caught <= 3 && g.ball('touch') === 1 && g.ball('touch2') === 0 && g.car(1, 'touches') === 1 && g.car(0, 'passes') === 1,
    `a hand-off caught ${caught} tick(s) after the throw is a pass with the passer as touch2 (the assist) and a touch for the catcher`); }
// a pass broken up: an enemy parked across the line of a pass deflects it. It is no longer a pass: the window closes (review: it
// stayed open, so a team-mate who picked up the deflection got the easy 260 limit and the passer a completed pass)
{ const g = await world(); put(g, 0, 0, -250, 0, 1); put(g, 1, 0, -60, 0, -1, 0); put(g, 3, 0, -180, 1, 0, 1); g.x.br_t_step(5);
  g.x.br_t_give(0); cmd(g, 0, 0, 1); cmd(g, 1, 0, 1); g.x.br_t_step(2); cmd(g, 0, 0, 0); g.x.br_t_step(1); cmd(g, 0, 0, 1);
  const opened = g.ball('pass-from') === 0 && g.ball('pass-t') > 0;
  let hit = -1; for (let t = 0; t < 30 && hit < 0; t++) { g.x.br_t_step(1); if (g.ball('touch') === 3) hit = t; }
  ok(opened && hit >= 0 && g.ball('pass-from') === -1 && g.ball('pass-t') === 0,
    `a pass deflected by an enemy (tick ${hit}) is no longer a pass: the pass window closes`); }

// ---- tackles ------------------------------------------------------------------------------------------------------------------
// car 0 (team 0) carrying, parked; car k (team `team`) drives into its tail at 150
async function bump(team) {
  const g = await world(); put(g, 0, 0, 0, 0, 1); put(g, 3, 0, -40, 0, 1, team); g.x.br_t_step(5); g.x.br_t_give(0);
  g.setCar(3, 'vz', 150); let loose = -1;
  for (let t = 0; t < 30; t++) { cmd(g, 0, 0, 1); cmd(g, 3, 1, 0); g.x.br_t_step(1); if (loose < 0 && g.ball('holder') !== 0) loose = t; }
  return { g, loose };
}
{ const e = await bump(1), m = await bump(0);
  ok(e.loose >= 0 && e.g.car(3, 'steals') === 1 && e.g.car(0, 'nocatch') > 0,
    `an enemy bumping the carrier knocks the ball loose (tick ${e.loose}), a steal, and the carrier is locked out`);
  ok(m.loose < 0 && m.g.car(3, 'steals') === 0 && m.g.car(0, 'z') > 10, 'a team-mate bumping the carrier just shoves it: the ball stays held'); }
// car k drives nose-first into the carried ball itself
async function ram(team) {
  const g = await world(); put(g, 0, 0, 0, 0, 1); put(g, 3, 0, 70, 0, -1, team); g.x.br_t_step(5); g.x.br_t_give(0);
  g.setCar(3, 'vz', -100); let loose = -1;
  for (let t = 0; t < 30; t++) { cmd(g, 0, 0, 1); cmd(g, 3, 1, 0); g.x.br_t_step(1); if (loose < 0 && g.ball('holder') !== 0) loose = t; }
  return { g, loose };
}
{ const e = await ram(1), m = await ram(0);
  ok(e.loose >= 0 && e.g.car(3, 'steals') === 1 && e.g.car(3, 'touches') >= 1,
    `an enemy's body hitting the carried ball knocks it loose (tick ${e.loose}) and touches it; a team-mate's does not (${m.loose < 0 ? 'held' : 'LOST'})`);
  ok(m.loose < 0, 'a team-mate driving into the carried ball passes through it'); }
// a steal a few ticks after the catch (inside the three-tick "same touch" window) is a new touch too: an enemy crossing in front
// of the carrier knocks the ball out of the cradle; the carrier becomes touch2 and the stealer's touch is counted
{ const g = await world(); put(g, 0, 0, 0, 0, 1); put(g, 1, -200, 0, 0, 1, 0); put(g, 3, -22, 17, 1, 0, 1); g.x.br_t_step(5);
  g.x.br_t_give(1); cmd(g, 1, 0, 0); g.x.br_t_step(10);            // car 1 had it before car 0
  g.x.br_t_give(0); g.setCar(3, 'vx', 100); const caughtT = g.ball('touch-t'); let loose = false;
  for (let t = 0; t < 10 && !loose; t++) { cmd(g, 0, 0, 1); cmd(g, 3, 1, 0); g.x.br_t_step(1); loose = g.ball('holder') !== 0; }
  const gap = g.ball('touch-t') - caughtT;
  ok(loose && gap <= 3 && g.ball('touch') === 3 && g.ball('touch2') === 0 && g.car(3, 'touches') === 1 && g.car(3, 'steals') === 1,
    `stolen ${gap} ticks after the catch: a new touch (touch ${g.ball('touch')}, touch2 ${g.ball('touch2')}, the stealer's touches ${g.car(3, 'touches')})`); }

// ---- the costs of carrying and the five-second clock ---------------------------------------------------------------------------------
{ const g = await world(); put(g, 0, 0, -200, 0, 1); g.x.br_t_step(5); g.x.br_t_give(0); g.setCar(0, 'air', 50);
  for (let t = 0; t < 60; t++) { cmd(g, 0, 0, 1); g.x.br_t_step(1); }
  const a1 = g.car(0, 'air'); cmd(g, 0, 0, 0); g.x.br_t_step(1);
  for (let t = 0; t < 60; t++) { cmd(g, 0, 0, 0); g.x.br_t_step(1); }
  ok(a1 === 50 && g.car(0, 'air') > 60, `the carrier's tank does not refill (50 -> ${a1}); after the throw it does (-> ${g.car(0, 'air').toFixed(0)})`); }
{ const g = await world(); for (const s of [0, 1]) { g.x.br_t_place(s, s * 100 - 50, 120, -200); orient(g, s, 0, 1, 0, 0, 0, 1); g.setCar(s, 'attached', 0); }
  g.x.br_t_give(0); for (const s of [0, 1]) { g.setCar(s, 'vz', 500); cmd(g, s, 0, 1); } g.x.br_t_step(1);
  ok(g.speed(0) <= 378.5 && g.speed(1) > 400, `the carrier's hard speed cap is 90%: ${g.speed(0).toFixed(0)} carrying vs ${g.speed(1).toFixed(0)}`); }
{ const g = await world(); put(g, 0, 0, -200, 0, 1); g.x.br_t_step(5); g.x.br_t_give(0); let thrown = -1, sp = 0;
  for (let t = 0; t < 340 && thrown < 0; t++) { cmd(g, 0, 0, 1); g.x.br_t_step(1); if (g.ball('holder') === -1) { thrown = t + 1; sp = relSpeed(g, 0); } }
  ok(Math.abs(thrown * DT - 5) < 0.05 && Math.abs(sp - 130) < 5 && g.car(0, 'throws') === 1,
    `held with the button down, the ball is thrown for you after ${(thrown * DT).toFixed(2)} s, at half pace (${sp.toFixed(0)})`); }

// ---- dunks, own goals and the resets (a match on the rules) -----------------------------------------------------------------------------
// every car a human (so nobody moves but car 0, which reads the local command words); car 0 carries the ball up a tower's shaft
async function dunk(tower) {
  const g = await boot(W); g.x.br_t_begin(3, 3, 0xfff); g.x.br_t_setg(G['cmd-magnet'], 1);
  g.x.br_t_give(1); g.x.br_t_play(6);              // team-mate car 1 has it first (its button is up: it lets go at once)
  const sx = tower & 1 ? 1 : -1, sz = tower & 2 ? 1 : -1;
  g.x.br_t_place(0, sx * 450 + sx * 9, 200, sz * 450); orient(g, 0, -sx, 0, 0, 0, 1, 0); g.setCar(0, 'attached', 0); g.setCar(0, 'vy', 120);
  g.x.br_t_give(0); let goalAt = -1;
  for (let t = 0; t < 90 && goalAt < 0; t++) { g.x.br_t_play(1); if (g.g(G['goal-t']) > 0) goalAt = t; }
  return { g, goalAt };
}
{ const { g, goalAt } = await dunk(1);
  ok(goalAt >= 0 && g.g(G.score) === 1 && g.g(G.conceded + 1) === 1 && g.car(0, 'goals') === 1 && g.g(G['goal-dunk']) === 1 && g.g(G['banner-id']) === 1,
    `a dunk: carried up into tower 1's shaft it scores for the carrier's team (${(goalAt * DT).toFixed(2)} s)`);
  ok(g.car(1, 'assists') === 1 && g.ball('holder') === -1, '... with the team-mate who had it before as the assist, and the ball is free (dead) after'); }
{ const { g, goalAt } = await dunk(0);
  ok(goalAt >= 0 && g.g(G.score) === 0 && g.car(0, 'own-goals') === 1 && g.car(0, 'goals') === 0 && g.g(G['banner-id']) === 2,
    'a dunk into your own tower is an own goal'); }
{ const g = await boot(W); g.x.br_t_begin(3, 3, 0xfff); g.x.br_t_setg(G['cmd-magnet'], 1); g.x.br_t_play(5); g.x.br_t_give(0); g.x.br_t_play(5);
  const held = g.ball('holder') === 0; g.x.br_t_kickoff();
  ok(held && g.ball('holder') === -1 && g.ball('y') === 70 && g.ball('pass-from') === -1, 'a kickoff frees the ball (back on the spot, nobody holding it)');
  g.x.br_t_give(0); g.x.br_t_end(); const e = g.ball('holder');
  g.x.br_t_give(0); g.x.br_t_place(0, 0, 4, -100); const p = g.ball('holder');
  g.x.br_t_give(0); g.setCar(0, 'active', 0); g.x.br_t_step(1);
  ok(e === -1 && p === -1 && g.ball('holder') === -1, 'the end of the match, a car put back on the floor, and a car leaving the match all free the ball'); }
// a keeper (car 9, team 3) hovering in the mouth of its own shaft with its magnet on and its cradle above the goal line; team 0
// lobs the ball up the shaft past it. A cradle over the line holds nothing: the ball goes in and it is team 0's goal (review: the
// keeper caught it just under the line, the catch snapped it up into the cradle over the line, and team 0's goal became the
// keeper's own goal)
{ const g = await boot(W); g.x.br_t_begin(3, 3, 0xfff); g.x.br_t_play(3);
  for (let s = 1; s < 12; s++) if (s !== 9) g.setCar(s, 'active', 0);
  g.x.br_t_place(9, 450, 232, 441); orient(g, 9, 0, 0, 1, 0, 1, 0); g.setCar(9, 'attached', 0);
  g.x.br_t_give(0); g.x.br_t_play(2);                               // car 0 (team 0) plays it: its button is up, it lets go
  g.x.br_t_ball(450, 150, 457, 0, 170, 0); g.x.br_t_setg(G.me, 9); g.x.br_t_setg(G['cmd-magnet'], 1);
  const above = cradle(g, 9)[1]; let goal = -1;
  for (let t = 0; t < 80 && goal < 0; t++) { for (const k of ['vx', 'vy', 'vz']) g.setCar(9, k, 0); g.x.br_t_play(1); if (g.g(G['goal-t']) > 0) goal = t; }
  ok(above > 244 && goal >= 0 && g.g(G.score) === 1 && g.car(9, 'own-goals') === 0 && g.car(9, 'catches') === 0,
    `a ball over the goal line cannot be caught by a keeper whose cradle is up in the shaft (${above.toFixed(0)}): it is team 0's goal`); }
// a new match starts clean whatever the last one left: nobody holds the ball, no pass window, lockout, fire or dunk flag
{ const g = await boot(W); g.x.br_t_begin(3, 3, 0xfff); g.x.br_t_play(5);
  g.x.br_t_give(4); g.x.br_t_ignite(4); g.x.br_t_play(3);           // car 4 burning, throws (pass window, lockout, ball alight)
  g.x.br_t_give(5); g.x.br_t_setg(G['goal-dunk'], 1);               // car 5 holding it, the last goal a dunk
  g.x.br_t_begin(4, 3, 0xfff);
  const clean = [...Array(12).keys()].every(s => ['nocatch', 'in-magnet', 'fire-t', 'fire-streak', 'catches', 'throws'].every(k => g.car(s, k) === 0));
  ok(clean && g.ball('holder') === -1 && g.ball('hold-t') === 0 && g.ball('pass-from') === -1 && g.ball('pass-t') === 0 &&
     g.ball('fire') === 0 && g.ball('fire-t') === 0 && g.g(G['goal-dunk']) === 0 && g.x.br_light_count() === 0 && g.x.br_part_live() === 0,
    `begin-match! leaves nothing behind (holder ${g.ball('holder')}, pass-from ${g.ball('pass-from')}, goal-dunk ${g.g(G['goal-dunk'])})`); }

// ---- the command byte and the replay ------------------------------------------------------------------------------------------------------
{ const g = await boot(W); const fl = [];
  for (const [m, b, f] of [[1, 1, 0], [1, 0, 1], [0, 1, 1], [0, 0, 0]]) {
    g.x.br_t_setg(G['cmd-magnet'], m); g.x.br_t_setg(G['cmd-boost'], b); g.x.br_t_setg(G['cmd-fans-off'], f);
    g.x.br_t_pend_from_cmd(5); g.x.br_t_apply_pend(5);
    fl.push(g.x.br_t_pend_byte(5, 6) === m * 4 + b + f * 2 && g.car(5, 'in-magnet') === m && g.car(5, 'in-boost') === b && g.car(5, 'in-fans-off') === f); }
  ok(fl.every(Boolean), 'the magnet button rides bit 2 of the flags byte, beside boost and fans-off, and comes back out of it'); }
// a match where the human holds the ball for 40 ticks and lets go; then a FRESH module is handed only that match's command log
// (as IDUNA's referee would be) and replays it
const LOG_BASE = +memSrc.match(/\(defn log-base \[\] : I32 (\d+)\)/)[1];
async function replayRun(log) {
  const g = await boot(W); g.x.br_t_begin(9, 3, 0xfff);
  const live = !log;
  if (live) { g.x.br_t_setgf(G['cmd-thr'], 0.6); g.x.br_t_setg(G['cmd-magnet'], 1); }
  else { new Uint8Array(g.x.memory.buffer).set(log, LOG_BASE);
    g.x.br_t_setg(G['log-n'], log.length / 8); g.x.br_t_setg(G.replay, 1); g.x.br_t_setg(G['replay-pos'], 0); }
  g.x.br_t_play(100); g.x.br_t_give(0);
  g.x.br_t_play(40); if (live) g.x.br_t_setg(G['cmd-magnet'], 0);
  g.x.br_t_play(1); if (live) g.x.br_t_setg(G['cmd-magnet'], 1);
  g.x.br_t_play(150);
  let h = 0; for (let s = 0; s < 12; s++) for (const k of ['x', 'y', 'z', 'vx', 'vz']) h = (h * 31 + Math.round(g.car(s, k) * 1000)) | 0;
  for (const k of ['x', 'y', 'z', 'vx', 'vy', 'vz']) h = (h * 31 + Math.round(g.ball(k) * 1000)) | 0;
  return { h, throwT: g.ball('touch-t'), throws: g.car(0, 'throws'), catches: g.car(0, 'catches'),
    log: new Uint8Array(g.x.memory.buffer).slice(LOG_BASE, LOG_BASE + g.g(G['log-n']) * 8) };
}
{ const a = await replayRun(null), b = await replayRun(a.log);
  ok(a.throws === 1 && a.throwT === 140 && b.throwT === a.throwT && b.h === a.h,
    `replayed from its command log alone the carrier lets go on the same tick (${b.throwT}) and the match lands on the same state (${b.h})`); }

// a replay FUZZ: car 0 (the local player, through the command words) and car 4 (an online human, through its pending bytes as
// the room would write them) press random buttons -- the magnet among them, held and let go in random stretches -- against ten
// bots for 6000 ticks, car 0 starting with the ball. A fresh module handed only the command log (and the same opening hand-over)
// must land on the same car records, ball record and rules words.
const PEND_BASE = +fs.readFileSync(path.join(path.dirname(fileURLToPath(import.meta.url)), '../stdlib/bedrockracers/web/br_sim.prn'), 'utf8')
  .match(/\(defn pend-base \[\] : I32 (\d+)\)/)[1];
async function fuzzRun(log) {
  const g = await boot(W); g.x.br_t_begin(5, 3, 0b10001);
  if (log) { new Uint8Array(g.x.memory.buffer).set(log, LOG_BASE);
    g.x.br_t_setg(G['log-n'], log.length / 8); g.x.br_t_setg(G.replay, 1); g.x.br_t_setg(G['replay-pos'], 0); }
  g.x.br_t_give(0);
  let s = 12345 >>> 0; const r = () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296; };
  let m0 = 1, m4 = 0;
  for (let t = 0; t < 6000 && g.g(G.phase) === 1; t++) {
    if (!log) {
      if (r() < 0.04) m0 = 1 - m0; if (r() < 0.04) m4 = 1 - m4;
      g.x.br_t_setgf(G['cmd-thr'], r() * 2 - 1); g.x.br_t_setgf(G['cmd-steer'], r() * 2 - 1); g.x.br_t_setgf(G['cmd-pitch'], r() * 2 - 1);
      g.x.br_t_setgf(G['cmd-lift'], r() < 0.3 ? r() : 0); g.x.br_t_setg(G['cmd-boost'], r() < 0.2 ? 1 : 0); g.x.br_t_setg(G['cmd-magnet'], m0);
      const u8 = new Uint8Array(g.x.memory.buffer);
      for (let k = 0; k < 6; k++) u8[PEND_BASE + 32 + k] = Math.floor(r() * 256);
      u8[PEND_BASE + 38] = (r() < 0.2 ? 1 : 0) + 4 * m4; u8[PEND_BASE + 39] = 0;
    }
    g.x.br_t_play(1);
  }
  const u32 = new Uint32Array(g.x.memory.buffer), c0 = g.x.br_car_addr(0) / 4, b0 = g.x.br_ball_addr() / 4, fx = K['fire-fx'];
  let h = 0; const mix = (v) => { h = (Math.imul(h, 31) + v) | 0; };
  for (let k = 0; k < 12 * 128; k++) if (k % 128 !== fx) mix(u32[c0 + k]);   // every car word but the cosmetic fire-fx
  for (let k = 0; k < 64; k++) mix(u32[b0 + k]);
  for (let k = 0; k < 110; k++) if (k < 99 || k > 101) mix(g.g(k));          // the rules words, less the log / replay cursors
  let act = 0; for (let q = 0; q < 12; q++) act += g.car(q, 'catches') + g.car(q, 'throws');
  return { h, act, tick: g.g(0), log: new Uint8Array(g.x.memory.buffer).slice(LOG_BASE, LOG_BASE + g.g(G['log-n']) * 8) };
}
{ const a = await fuzzRun(null), b = await fuzzRun(a.log);
  ok(a.act >= 2 && a.h === b.h && a.tick === b.tick,
    `a fuzzed match (two humans, random buttons and magnet, ${a.act} catches + throws) replays from its command log to the same state (${b.h})`); }

// ---- determinism --------------------------------------------------------------------------------------------------------------------------
{ const run = async () => { const g = await boot(W); g.x.br_t_begin(7, 3, 0); g.x.br_t_play(3000);
    let h = 0; for (let s = 0; s < 12; s++) for (const k of ['x', 'y', 'z', 'vx', 'vy', 'vz', 'air', 'nocatch', 'catches', 'throws', 'steals'])
      h = (h * 31 + Math.round(g.car(s, k) * 1000)) | 0;
    return h ^ Math.round(g.ball('x') * 1000) ^ (g.ball('holder') << 20); };
  const a = await run(), b = await run(); ok(a === b, `a whole bot match is still a pure function of its seed (${a})`); }

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
