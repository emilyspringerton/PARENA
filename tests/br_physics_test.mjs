// tests/br_physics_test.mjs -- BEDROCK RACERS car and ball handling, measured on the compiled wasm: tyre grip and drift,
// steering weight, reversing, landing belly-down only, turtling and self-righting, wall-to-ceiling driving, the ball's roll and
// bounce, a head-on strike, and match determinism.
//   node tests/br_physics_test.mjs <bedrockracers.wasm>
import { boot } from './br_harness.mjs';
const W = process.argv[2];
if (!W) { console.error('usage: br_physics_test.mjs <wasm>'); process.exit(2); }
let pass = 0, fail = 0;
const ok = (c, m) => { c ? pass++ : fail++; console.log((c ? 'PASS: ' : 'FAIL: ') + m); };
const DT = 0.016;
const away = (g) => g.x.br_t_ball(0, 120, 480, 0, 0, 0);   // park the ball against the far wall, out of the way
const heading = (g) => Math.atan2(g.car(0, 'fx'), g.car(0, 'fz'));
const wrap = (a) => (a > Math.PI ? a - 2 * Math.PI : a < -Math.PI ? a + 2 * Math.PI : a);
const slipDeg = (g) => Math.abs(wrap(Math.atan2(g.car(0, 'vx'), g.car(0, 'vz')) - heading(g))) * 180 / Math.PI;
async function car(x, y, z) { const g = await boot(W); away(g); g.x.br_t_place(0, x, y, z); g.x.br_t_step(10); return g; }
function orient(g, ux, uy, uz, fx, fy, fz) {
  g.setCar(0, 'ux', ux); g.setCar(0, 'uy', uy); g.setCar(0, 'uz', uz); g.setCar(0, 'fx', fx); g.setCar(0, 'fy', fy); g.setCar(0, 'fz', fz);
  g.setCar(0, 'rx', fy * uz - fz * uy); g.setCar(0, 'ry', fz * ux - fx * uz); g.setCar(0, 'rz', fx * uy - fy * ux);
}
function launch(g, v) { g.setCar(0, 'vx', g.car(0, 'fx') * v); g.setCar(0, 'vz', g.car(0, 'fz') * v); }

// straight line
{ const g = await car(0, 4, -400); orient(g, 0, 1, 0, 0, 0, 1); let t100 = -1;
  for (let t = 0; t < 250; t++) { g.x.br_t_cmd(0, 1, 0, 0, 0, 0, 0, 0, 0); g.x.br_t_step(1); away(g); if (t100 < 0 && g.speed(0) >= 100) t100 = t * DT; }
  ok(t100 > 0 && t100 < 1.2, `0 to 100 in ${t100.toFixed(2)} s (< 1.2)`);
  ok(g.speed(0) > 150 && g.speed(0) < 195, `top speed on throttle alone ${g.speed(0).toFixed(0)} (150..195)`); }

// steering: weight, reverse, standstill
{ const g = await car(0, 4, 0); orient(g, 0, 1, 0, 0, 0, 1); launch(g, 120); const h0 = heading(g);
  g.x.br_t_cmd(0, 0.5, 1, 0, 0, 0, 0, 0, 0); g.x.br_t_step(1); const first = Math.abs(wrap(heading(g) - h0)) / DT;
  g.x.br_t_step(30); const a = heading(g); g.x.br_t_step(1); const steady = Math.abs(wrap(heading(g) - a)) / DT;
  ok(first < 0.6 * steady, `the yaw rate builds up: first tick ${first.toFixed(2)} vs steady ${steady.toFixed(2)} rad/s`);
  ok(wrap(heading(g) - h0) < 0, 'steer > 0 turns right going forward'); }
{ const g = await car(0, 4, 0); orient(g, 0, 1, 0, 0, 0, 1); launch(g, -60); const h0 = heading(g);
  for (let t = 0; t < 20; t++) { g.x.br_t_cmd(0, -0.5, 1, 0, 0, 0, 0, 0, 0); g.x.br_t_step(1); }
  ok(wrap(heading(g) - h0) > 0, 'steer > 0 while reversing swings the nose the other way, like a car'); }
{ const g = await car(0, 4, 0); orient(g, 0, 1, 0, 0, 0, 1); const h0 = heading(g);
  for (let t = 0; t < 30; t++) { g.x.br_t_cmd(0, 0, 1, 0, 0, 0, 0, 0, 0); g.x.br_t_step(1); }
  const w = Math.abs(wrap(heading(g) - h0)) / (30 * DT); ok(w < 0.8, `a parked car barely pivots: ${w.toFixed(2)} rad/s (< 0.8)`); }

// grip: concrete tracks, dirt lets the tail out
{ const g0 = await boot(W); let dirt = null, conc = null;
  for (let x = -300; x <= 300 && !(dirt && conc); x += 10) for (let z = -300; z <= 300; z += 10) {
    const r = g0.x.br_t_road(x, z); if (r > 250 && !dirt) dirt = [x, z]; if (r === 0 && !conc && Math.hypot(x, z) < 60) conc = [x, z]; }
  const slide = async (p) => { const g = await car(p[0], 4, p[1]); launch(g, 185); let m = 0;
    for (let t = 0; t < 40; t++) { g.x.br_t_cmd(0, 1, 1, 0, 0, 0, 0, 0, 0); g.x.br_t_step(1); away(g); m = Math.max(m, slipDeg(g)); } return [m, g.speed(0)]; };
  const [sc, vc] = await slide(conc), [sd, vd] = await slide(dirt);
  ok(sc < 5, `full lock at 185 on concrete tracks: slip angle ${sc.toFixed(1)} deg (< 5)`);
  ok(sd > 8, `full lock at 185 on dirt drifts: slip angle ${sd.toFixed(1)} deg (> 8)`);
  ok(vd < vc, `the drift scrubs speed: ${vd.toFixed(0)} on dirt vs ${vc.toFixed(0)} on concrete`); }

// landing: belly-down catches, roof-down does not
for (const tilt of [0, 45]) { const g = await car(0, 60, 0); const a = tilt * Math.PI / 180;
  orient(g, Math.sin(a), Math.cos(a), 0, 0, 0, 1); g.setCar(0, 'attached', 0); g.setCar(0, 'vy', -120); g.setCar(0, 'vz', 80); let att = -1;
  for (let t = 0; t < 60; t++) { g.x.br_t_cmd(0, 0, 0, 0, 0, 0, 0, 0, 0); g.x.br_t_step(1); if (att < 0 && g.car(0, 'attached') === 1) att = t; }
  ok(att > 0 && g.car(0, 'uy') > 0.98, `landing tilted ${tilt} deg: the fans catch it (tick ${att}) and it sits flat`); }
{ const g = await car(0, 60, 0); orient(g, 0, -1, 0, 0, 0, 1); g.setCar(0, 'attached', 0); g.setCar(0, 'vy', -120); g.setCar(0, 'vz', 80);
  let firstTouch = -1, attachedEarly = false, v1 = 0;
  for (let t = 0; t < 60; t++) { g.x.br_t_cmd(0, 0, 0, 0, 0, 0, 0, 0, 0); g.x.br_t_step(1);
    if (firstTouch < 0 && g.car(0, 'y') < 8) firstTouch = t; if (firstTouch >= 0 && t < firstTouch + 10 && g.car(0, 'attached') === 1) attachedEarly = true; if (t === firstTouch + 20) v1 = g.speed(0); }
  ok(!attachedEarly, 'landing on the roof: the fans do not snap it upright');
  ok(v1 < 70, `scraping along on the roof slows it (80 -> ${v1.toFixed(0)})`);
  let back = -1; for (let t = 0; t < 250; t++) { g.x.br_t_cmd(0, 0, 0, 0, 0, 0, 0, 0, 0); g.x.br_t_step(1); if (back < 0 && g.car(0, 'attached') === 1) back = t; }
  ok(back > 0 && g.car(0, 'uy') > 0.95, `a turtled car tips itself back onto its wheels (${((back + 60) * DT).toFixed(1)} s)`); }

// the fans: up the wall, over the curl, along the ceiling
{ const g = await car(300, 4, 0); orient(g, 0, 1, 0, 1, 0, 0); launch(g, 100); let inv = false;
  for (let t = 0; t < 400; t++) { g.x.br_t_cmd(0, 1, 0, 0, 0, 0, 0, 0, 0); g.x.br_t_step(1); away(g); if (g.car(0, 'uy') < -0.9 && g.car(0, 'attached') === 1) inv = true; }
  ok(inv, 'driving into the wall at 100 climbs it and carries on upside down along the ceiling'); }

// air: rate-commanded attitude
{ const g = await car(0, 120, 0); g.setCar(0, 'attached', 0); const r = [];
  for (let t = 0; t < 40; t++) { g.x.br_t_cmd(0, 0, 0, t < 20 ? 1 : 0, 0, 0, 0.36, 0, 0); g.x.br_t_step(1); g.setCar(0, 'air', 100); r.push(g.car(0, 'wp')); }
  ok(r[10] > 3.3 && Math.abs(r[30]) < 0.05, `pitch rate reaches ${r[10].toFixed(2)} and stops after release (${r[30].toFixed(3)})`); }

// the ball
{ const g = await car(0, 4, 400); g.x.br_t_ball(0, 150, 0, 0, 0, 0); const peaks = []; let pv = 0;
  for (let t = 0; t < 400; t++) { g.x.br_t_step(1); const vy = g.ball('vy'); if (pv > 0 && vy <= 0) peaks.push(g.ball('y')); pv = vy; }
  const e = (peaks[0] - 11) / (150 - 11); ok(e > 0.3 && e < 0.6, `dropped from 150 it bounces back to ${peaks[0].toFixed(0)}`); }
{ const g = await car(0, 4, 400); g.x.br_t_ball(-300, 11, 0, 150, 0, 0); g.x.br_t_step(125);
  const v = Math.hypot(g.ball('vx'), g.ball('vz')); ok(v > 60 && v < 120, `a ball rolled at 150 still rolls at ${v.toFixed(0)} after 2 s`); }
{ const g = await car(0, 4, 400); g.x.br_t_ball(0, 11, 0, 250, 0, 0); let my = 0;
  for (let t = 0; t < 200; t++) { g.x.br_t_step(1); my = Math.max(my, g.ball('y')); }
  ok(my > 50, `a ball rolled at 250 into a wall climbs it (to ${my.toFixed(0)})`); }
{ const g = await car(0, 4, -100); orient(g, 0, 1, 0, 0, 0, 1); g.x.br_t_ball(0, 11, -60, 0, 0, 0); launch(g, 190); let b = 0;
  for (let t = 0; t < 40; t++) { g.x.br_t_cmd(0, 1, 0, 0, 0, 0, 0, 0, 0); g.x.br_t_step(1); b = Math.max(b, Math.hypot(g.ball('vx'), g.ball('vy'), g.ball('vz'))); }
  ok(b > 230, `a head-on hit at 190 sends the ball off at ${b.toFixed(0)}`); }

// determinism: a whole bot match is a pure function of its seed
{ const run = async () => { const g = await boot(W); g.x.br_t_begin(7, 3, 0); g.x.br_t_play(3000);
    let h = 0; for (let s = 0; s < 12; s++) for (const k of ['x', 'y', 'z', 'vx', 'vy', 'vz', 'air']) h = (h * 31 + Math.round(g.car(s, k) * 1000)) | 0;
    return h ^ Math.round(g.ball('x') * 1000); };
  const a = await run(), b = await run(); ok(a === b, `two runs of the same seed agree after 3000 ticks (${a})`); }

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
