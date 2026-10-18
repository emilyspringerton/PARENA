// tests/br_bot_soak.mjs -- BEDROCK RACERS bot matches on the bench: goals, own goals, touches, how hard and how well the ball is
// struck, and how much of the floor has a goal-scoring shot at all. Prints one JSON object (the last line of output).
//   node tests/br_bot_soak.mjs <bedrockracers.wasm> [seeds=6] [level=3] [ticks=15000] [first-seed=1]
import { boot } from './br_harness.mjs';
const W = process.argv[2];
if (!W) { console.error('usage: br_bot_soak.mjs <wasm> [seeds] [level] [ticks]'); process.exit(2); }
const SEEDS = +(process.argv[3] || 6), LEVEL = +(process.argv[4] || 3), TICKS = +(process.argv[5] || 15000), FIRST = +(process.argv[6] || 1);
const G_PHASE = 16, G_SCORE = 32;
const out = { seeds: SEEDS, first_seed: FIRST, level: LEVEL, minutes: 0, goals: 0, own_goals: 0, touches: 0, mean_ball_speed: 0, mean_ball_height: 0,
  strikes: 0, strikes_on_target: 0, per_seed: [] };
let bs = 0, bh = 0, bn = 0;
for (let sd = FIRST; sd < FIRST + SEEDS; sd++) {
  const g = await boot(W); g.x.br_t_begin(sd, LEVEL, 0); let t = 0, lastTouch = -1, lastTick = -999;
  for (; t < TICKS; t++) {
    g.x.br_t_play(1); if (g.g(G_PHASE) !== 1) break;
    if (t % 10 === 0) { bs += Math.hypot(g.ball('vx'), g.ball('vy'), g.ball('vz')); bh += g.ball('y'); bn++; }
    // a strike: the toucher changed or re-touched after a gap; judge it a few ticks later by flying the ball on (what-if)
    const tt = g.ball('touch-t');
    if (tt !== lastTick && tt > lastTick + 6) { lastTick = tt; lastTouch = g.ball('touch'); }
    if (lastTick >= 0 && g.g(0) === lastTick + 4) {
      out.strikes++;
      const team = g.car(lastTouch, 'team');
      const sc = g.x.br_t_whatif(g.ball('x'), g.ball('y'), g.ball('z'), g.ball('vx'), g.ball('vy'), g.ball('vz'), team, -1);
      if (sc > 900) out.strikes_on_target++;
    }
  }
  const sc = [0, 1, 2, 3].map(k => g.g(G_SCORE + k)); let own = 0, touches = 0;
  for (let s = 0; s < 12; s++) { own += g.car(s, 'own-goals'); touches += g.car(s, 'touches'); }
  out.goals += sc.reduce((a, b) => a + b, 0); out.own_goals += own; out.touches += touches; out.minutes += t * 0.016 / 60;
  out.per_seed.push({ seed: sd, score: sc.join('/'), own_goals: own, touches, ticks: t });
}
out.mean_ball_speed = +(bs / bn).toFixed(1); out.mean_ball_height = +(bh / bn).toFixed(1); out.minutes = +out.minutes.toFixed(2);
out.goals_per_minute = +(out.goals / out.minutes).toFixed(3);
// how much of the floor has a goal at all: the shot planner over a grid of ball positions, at a typical strike speed
{ const g = await boot(W); let n = 0, ok = 0;
  for (let x = -440; x <= 440; x += 110) for (let z = -440; z <= 440; z += 110) for (const y of [11, 60]) {
    n++; if (g.x.br_t_plan(x, y, z, 0, 3, 300) > 900) ok++; }
  out.plan_coverage = +(ok / n).toFixed(3); }
console.log(JSON.stringify(out));
