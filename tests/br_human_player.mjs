// tests/br_human_player.mjs -- can a PERSON score against EASY bots with the default controls?
//
// A keyboard-only player for the first car of the amber team. It uses only what a person has: the key events the page forwards
// (W, A, D, S, Shift and F through br_key) and what they can see on screen (positions of the cars and the ball, read from memory).
// No test hooks, no what-if oracle, no command words. The sim is deterministic and the match seed comes from the frame clock the
// script feeds it, so a given seed always plays out the same way: a seed that scores is a reproducible proof that it can be done.
//
// The player: chase the ball, hold F near it to catch and carry, run at whichever opponent corner its throw already points closest
// to, and let go (the throw is the car's velocity + 260 along the nose) once the throw is aimed within a few degrees of the corner's
// apex and is fast enough. The scoring window it plays to (measured with the bots' own what-if): a flat throw along the floor
// diagonal goes in from up to ~450 units out at 350+ units/s, and the aim has to be within about +-4 degrees at 300 units and
// +-2 degrees at 600 -- speed above ~450 hardly matters, aim does.
//
//   node tests/br_human_player.mjs <bedrockracers.wasm> [seed ...]        (about 3 minutes per seed; the default seed is 1)
//   env: TICKS (15000), ALIGN (4 degrees), REL_MIN (100), REL_MAX (320), MIN_THROW (450), VERBOSE=1 (every carry)
//
// Exits 0 when the person scored at least once in at least one of the seeds, 1 otherwise. NOT part of `make test-bedrockracers`
// (it takes minutes); it is how "a human can score against EASY bots" was shown, and what to rerun after touching the bots or the
// magnet. Honest measure on seeds 1-4 at these settings: a person goal in 1 of 4 four-minute matches; the bots intercept most throws.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from './br_harness.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const G = {};
for (const f of fs.readdirSync(path.join(here, '../stdlib/bedrockracers/web'))) {
  if (!/^br_.*\.prn$/.test(f) || f === 'br_fields.prn') continue;
  for (const m of fs.readFileSync(path.join(here, '../stdlib/bedrockracers/web', f), 'utf8').matchAll(/\(defn g-([a-z0-9-]+) \[\] : I32 (\d+)\)/g)) G[m[1]] = +m[2];
}
const KEY = { w: 87, a: 65, d: 68, s: 83, f: 70, shift: 16, enter: 13, down: 40, right: 39 };
const env = (k, d) => (process.env[k] !== undefined ? +process.env[k] : d);
const APEX = (t) => [(t & 1 ? 1 : -1) * 450, (t & 2 ? 1 : -1) * 450];

export async function play(wasm, { seed = 1, team = 0, ticks = env('TICKS', 15000), verbose = false } = {}) {
  const ALIGN = Math.cos(env('ALIGN', 4) * Math.PI / 180), REL_MIN = env('REL_MIN', 100), REL_MAX = env('REL_MAX', 320), MIN_THROW = env('MIN_THROW', 450);
  const g = await boot(wasm);
  let now = 1000;
  const frame = () => { now += 16; g.x.br_frame(now); };
  const held = new Set();
  const key = (k, on) => { if (on && !held.has(k)) { held.add(k); g.x.br_key(KEY[k], 1); } else if (!on && held.has(k)) { held.delete(k); g.x.br_key(KEY[k], 0); } };
  const tap = (k) => { g.x.br_key(KEY[k], 1); frame(); frame(); g.x.br_key(KEY[k], 0); frame(); };
  for (let i = 0; i < 5; i++) frame();
  for (let i = 0; i < 3; i++) tap('down');                  // the title: bots down to EASY
  for (let t = 0; t < team; t++) tap('right');              // and your team
  now = seed * 100003 + 1000;                               // the match seed comes from the clock: one clock per seed
  g.x.br_key(KEY.enter, 1); frame(); g.x.br_key(KEY.enter, 0);
  const me = team * 3;
  let stuck = 0, back = 0, lastHolder = -1, carryStart = 0, lastTarget = -1;
  const carries = [];
  for (let i = 0; i < ticks; i++) {
    const p = g.pos(me), f = g.fwd(me), r = [g.car(me, 'rx'), g.car(me, 'ry'), g.car(me, 'rz')], v = g.vel(me);
    const b = [g.ball('x'), g.ball('y'), g.ball('z')], bv = [g.ball('vx'), g.ball('vy'), g.ball('vz')];
    const carrying = g.ball('holder') === me, tick = g.g(G.tick), speed = Math.hypot(v[0], v[2]);
    if (carrying && lastHolder !== me) carryStart = tick;
    // where I am pointing a throw right now: the car's velocity plus 260 along the nose
    const thx = v[0] + 260 * f[0], thz = v[2] + 260 * f[2], thl = Math.hypot(thx, thz) || 1;
    let tx, tz;
    if (carrying) {
      let bestT = -1, bestA = -2;
      for (let t = 0; t < 4; t++) {                         // the opponent corner the throw already points nearest to (a little stickiness)
        if (t === team) continue;
        const [x, z] = APEX(t), dd = Math.hypot(x - p[0], z - p[2]) || 1;
        const a = (thx * (x - p[0]) + thz * (z - p[2])) / (thl * dd) + (t === lastTarget ? 0.15 : 0);
        if (a > bestA) { bestA = a; bestT = t; }
      }
      lastTarget = bestT; [tx, tz] = APEX(bestT);
    } else {
      const d0 = Math.hypot(b[0] - p[0], b[2] - p[2]), lead = Math.min(d0 / (speed + 220), 0.7);
      tx = b[0] + bv[0] * lead; tz = b[2] + bv[2] * lead;   // a free ball: go where it is going
    }
    const dx = tx - p[0], dz = tz - p[2], dist = Math.hypot(dx, dz), ux = dx / dist, uz = dz / dist;
    const side = ux * r[0] + uz * r[2], ahead = ux * f[0] + uz * f[2];     // side > 0: the target is to my right
    const throwAlign = (thx * ux + thz * uz) / thl;
    let w = true, s = false, aKey = false, dKey = false, boost = false, fKey = false, firing = false;
    if (back > 0) { back--; w = false; s = true; if (side > 0) aKey = true; else dKey = true; }
    else {
      if (side > 0.04) dKey = true; else if (side < -0.04) aKey = true;
      if (ahead < 0.2) w = speed < 60;
      if (!carrying) {
        fKey = dist < 70;                                   // magnet on near the ball
        if (dist < 70 && Math.hypot(v[0] - bv[0], v[2] - bv[2]) > 150) { w = false; s = true; }   // too hot to catch: brake
        boost = dist > 240 && ahead > 0.9;
      } else {
        fKey = true; boost = ahead > 0.9;
        firing = thl >= MIN_THROW && throwAlign >= ALIGN && dist >= REL_MIN && dist <= REL_MAX;
        if (firing || tick - carryStart > 290) fKey = false;   // let go: that is the throw
      }
    }
    if (speed < 4 && back === 0) { stuck++; if (stuck > 70) { back = 55; stuck = 0; } } else stuck = 0;
    key('w', w); key('s', s); key('a', aKey); key('d', dKey); key('shift', boost); key('f', fKey);
    if (carrying) var last = { speed: Math.round(speed), throwSpeed: Math.round(thl), aimErr: Math.round(Math.acos(Math.min(1, throwAlign)) * 180 / Math.PI), dist: Math.round(dist), fired: firing };
    if (!carrying && lastHolder === me) carries.push({ tick, held: tick - carryStart, ...last, goalsBefore: g.car(me, 'goals'), scored: false });
    lastHolder = g.ball('holder');
    frame();
    const open = carries[carries.length - 1];
    if (open && !open.scored && g.car(me, 'goals') > open.goalsBefore) open.scored = true;
    if (g.x.br_phase() !== 1) break;
  }
  if (verbose) for (const c of carries) console.log(`   t${c.tick} held ${c.held} speed ${c.speed} throw ${c.throwSpeed} aim ${c.aimErr}deg dist ${c.dist} ${c.fired ? 'RELEASED' : 'lost/auto'}${c.scored ? ' GOAL' : ''}`);
  return { goals: g.car(me, 'goals'), assists: g.car(me, 'assists'), ownGoals: g.car(me, 'own-goals'), ticks: g.g(G.tick), carries: carries.length, released: carries.filter(c => c.fired).length, teamScores: [0, 1, 2, 3].map(t => g.g(G.score + t)) };
}

if (process.argv[1] && process.argv[1].endsWith('br_human_player.mjs')) {
  const [wasm, ...seeds] = process.argv.slice(2);
  if (!wasm) { console.error('usage: br_human_player.mjs <wasm> [seed ...]'); process.exit(2); }
  let scored = 0; const list = seeds.length ? seeds.map(Number) : [1];
  for (const seed of list) {
    const r = await play(wasm, { seed, verbose: !!process.env.VERBOSE });
    if (r.goals > 0) scored++;
    console.log(`seed ${seed}: the person scored ${r.goals} (assists ${r.assists}, own goals ${r.ownGoals}) in ${(r.ticks / 62.5 / 60).toFixed(1)} min | ${r.carries} carries, ${r.released} released at a corner | team scores amber/teal/violet/coral ${r.teamScores}`);
  }
  console.log(`${scored} of ${list.length} matches had a person goal against EASY bots`);
  process.exit(scored > 0 ? 0 : 1);
}
