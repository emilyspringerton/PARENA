// tests/shankpit460_bot_soak.mjs -- eight bots in the arena, no human: do they find each other, fight, and keep moving?
//   node tests/shankpit460_bot_soak.mjs <shankpit460.wasm> [ticks] [level]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const [wasmPath, ticksArg = '20000', levelArg] = process.argv.slice(2);
const here = path.dirname(fileURLToPath(import.meta.url));
const fields = {};
for (const m of fs.readFileSync(path.join(here, '../stdlib/shankpit460/sp_fields.prn'), 'utf8').matchAll(/\(defn k-([a-z0-9-]+) \[\] : I32 (\d+)\)/g)) fields[m[1]] = +m[2];
const { instance } = await WebAssembly.instantiate(fs.readFileSync(wasmPath), {});
const ex = instance.exports;
const dv = () => new DataView(ex.memory.buffer);
const f = (p, k) => dv().getFloat32(ex.sp_player_addr(p) + fields[k] * 4, true);
const i = (p, k) => dv().getInt32(ex.sp_player_addr(p) + fields[k] * 4, true);
ex.sp_init();
const ticks = +ticksArg;
const levels = levelArg ? [+levelArg] : [1, 2, 3];
const report = [];
for (const level of levels) {
  ex.sp_t_bot_arena(level);
  for (let w = 0; w < 8; w++) ex.sp_t_poke(300 + w, 0);
  const cells = Array.from({ length: 8 }, () => new Set());
  const still = new Array(8).fill(0), aliveTicks = new Array(8).fill(0);
  const last = Array.from({ length: 8 }, (_, p) => [f(p, 'x'), f(p, 'z')]);
  let stuckRun = new Array(8).fill(0), worstStuck = new Array(8).fill(0);
  const chunk = 30;
  for (let t = 0; t < ticks; t += chunk) {
    ex.sp_t_play(chunk);
    for (let p = 0; p < 8; p++) {
      if (i(p, 'state') !== 0) { stuckRun[p] = 0; continue; }
      aliveTicks[p] += chunk;
      const x = f(p, 'x'), z = f(p, 'z');
      cells[p].add(Math.floor(x / 60) + ',' + Math.floor(z / 60));
      const moved = Math.hypot(x - last[p][0], z - last[p][1]);
      if (moved < 1.5) { still[p] += chunk; stuckRun[p] += chunk; worstStuck[p] = Math.max(worstStuck[p], stuckRun[p]); } else stuckRun[p] = 0;
      last[p] = [x, z];
    }
  }
  const kills = [...Array(8).keys()].map((p) => i(p, 'kills')), deaths = [...Array(8).keys()].map((p) => i(p, 'deaths'));
  const totalK = kills.reduce((a, b) => a + b, 0);
  const secs = ticks / 62.5;
  console.log(`level ${level}: ${secs.toFixed(0)}s  kills ${kills.join(' ')}  deaths ${deaths.join(' ')}  total ${totalK} (${(totalK / (secs / 60)).toFixed(1)}/min)`);
  console.log(`   explored cells ${cells.map((c) => c.size).join(' ')}   idle% ${still.map((s, p) => Math.round(100 * s / Math.max(1, aliveTicks[p]))).join(' ')}   worst idle run ${worstStuck.map((s) => (s / 62.5).toFixed(0) + 's').join(' ')}`);
  console.log('   kills by weapon: ' + ['knife','magnum','ar','shotgun','sniper','katana','storm','dash'].map((n, w) => n + ' ' + ex.sp_g(300 + w)).join('  '));
  report.push({ level, totalK, perMin: totalK / (secs / 60), idle: still.reduce((a, b) => a + b, 0) / Math.max(1, aliveTicks.reduce((a, b) => a + b, 0)) });
}
const bad = report.filter((r) => r.perMin < 2 || r.idle > 0.35);
if (bad.length) { console.log('SOAK FAIL', JSON.stringify(bad)); process.exit(1); }
console.log('soak ok');
