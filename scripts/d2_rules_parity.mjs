// Checks the D2 web client's port of the ship rules (stdlib/d2/web/d2_rules.prn) against the real combat core: the C reference dump
// (DEADWEIGHT_2/tools/dw2_ref_dump.c) lists ships and what each did, tick by tick; the wasm must do the same for every one.
//   node scripts/d2_rules_parity.mjs <d2.wasm> <ref.txt>
//
// Per ship it checks what the pack screen shows before a fight (cost, weight, structure, dodge chance, which cells are cooled and how hot every cell is, each gun's
// boost, which lasers have a clear lane, how many shots it takes to open every lane with each weapon, the pieces a Beam or an Ore splits into), and then, tick by
// tick for 30 seconds, the shots the circuit fires, the Back-EMF meltdowns, the burnt and destroyed cells, the charge held in stores and the open transistor gates --
// with the destroyed cells the dump schedules ("K tick cell") applied exactly as a server EVENTS frame would apply them.
import fs from 'node:fs';
const { instance } = await WebAssembly.instantiate(fs.readFileSync(process.argv[2]), {});
const e = instance.exports;
e.d2_init();
const lines = fs.readFileSync(process.argv[3], 'utf8').trim().split('\n');
// the catalog, the examples and the font are written once at init: no fight may land on them
const REGIONS = { items: [262144, 24 * 128], examples: [266240, 12 * 160], font: [196608, 255 * 16] };
const sums0 = Object.fromEntries(Object.entries(REGIONS).map(([k, [a, n]]) => [k, e.d2_t_sum(a, n)]));
let ships = 0, bad = 0, melt = 0, shotsTotal = 0, killsTotal = 0, items = new Set(), laneChecks = 0, splitChecks = 0, heatCells = 0, boosted = 0;
const mask = (lo, hi) => (BigInt(hi >>> 0) << 32n) | BigInt(lo >>> 0);
function load(s) {
  e.d2_t_clear();
  const n = +s[2];
  for (let k = 0; k < n; k++) { const [it, r, c, rot] = s[3 + k].split(',').map(Number); e.d2_t_add(it, r, c, rot); items.add(it); }
  return e.d2_t_rebuild();
}
for (let i = 0; i < lines.length;) {
  const s = lines[i++].split(' ');
  if (s[0] !== 'S') throw new Error('bad line ' + lines[i - 1]);
  const idx = s[1], n = +s[2];
  const problems = load(s);
  const fail = (m) => { bad++; if (bad < 400) console.log(`ship ${idx}: ${m}`); };
  if (problems !== 0) fail(`wasm found ${problems} illegal placements in a ship the server accepted`);
  const a = lines[i++].split(' ');
  // A cost weight structure evade10 cooled-hex Hdddd.. B boosts.. L clear.. O lane shots..
  const hIdx = a.findIndex((t) => t.startsWith('H') && t.length === 37);
  const bIdx = a.indexOf('B'), lIdx = a.indexOf('L'), oIdx = a.indexOf('O');
  if (e.d2_t_cost() !== +a[1]) fail(`cost ${e.d2_t_cost()} != ${a[1]}`);
  if (e.d2_t_weight() !== +a[2]) fail(`weight ${e.d2_t_weight()} != ${a[2]}`);
  if (e.d2_t_structure() !== +a[3]) fail(`structure ${e.d2_t_structure()} != ${a[3]}`);
  if (e.d2_t_evade10() !== +a[4]) fail(`dodge ${e.d2_t_evade10()} != ${a[4]}`);
  if (mask(e.d2_t_cooled_lo(), e.d2_t_cooled_hi()) !== BigInt('0x' + a[5])) fail(`cooled cells ${mask(e.d2_t_cooled_lo(), e.d2_t_cooled_hi()).toString(16)} != ${a[5]}`);
  const heat = a[hIdx].slice(1);
  for (let c = 0; c < 36; c++) { if (e.d2_t_heat(c) !== +heat[c]) { fail(`heat at cell ${c}: ${e.d2_t_heat(c)} != ${heat[c]}`); break; } if (+heat[c] > 0) heatCells++; }
  for (let p = 0; p < n; p++) {
    const want = +a[bIdx + 1 + p];
    if (e.d2_t_boost(p) !== want) { fail(`boost of placement ${p}: ${e.d2_t_boost(p)} != ${want}`); break; }
    if (want > 100) boosted++;
    const lc = a[lIdx + 1 + p];
    if (lc !== '-' && e.d2_t_lane_clear(p) !== +lc) { fail(`laser ${p} clear lane: ${e.d2_t_lane_clear(p)} != ${lc}`); break; }
  }
  for (let k = 0; k < 18; k++) {
    const got = e.d2_t_shots_to_open(k % 6, 1 + Math.floor(k / 6)), want = +a[oIdx + 1 + k];
    laneChecks++;
    if (got !== want) { fail(`shots to open lane ${k % 6} with weapon ${1 + Math.floor(k / 6)}: ${got} != ${want}`); break; }
  }
  // pieces of a split
  while (lines[i].startsWith('P ')) {
    const q = lines[i++].split(' ');
    const p = +q[1], np = +q[2];
    const got = e.d2_t_split(p);
    splitChecks++;
    if (got !== np) { fail(`split of placement ${p}: ${got} pieces != ${np}`); continue; }
    for (let k = 0; k < np; k++) {
      const w = q[3 + k].split(',').map(Number);
      for (let f = 0; f < 4; f++) if (e.d2_t_split_get(k, f) !== w[f]) { fail(`split of placement ${p}, piece ${k}, field ${f}: ${e.d2_t_split_get(k, f)} != ${w[f]}`); break; }
    }
  }
  const kills = new Map();
  while (lines[i].startsWith('K ')) { const q = lines[i++].split(' '); if (!kills.has(+q[1])) kills.set(+q[1], []); kills.get(+q[1]).push(+q[2]); }
  load(s);   // a fresh fight (the split test overwrote scratch; rebuild also resets the circuit)
  for (let t = 1; t <= 30; t++) {
    const T = lines[i++].split(' ');
    if (kills.has(t)) for (const c of kills.get(t)) { e.d2_t_kill(c); killsTotal++; }
    const shots = e.d2_t_tick();
    shotsTotal += shots;
    if (shots !== +T[2]) fail(`tick ${t}: shots ${shots} != ${T[2]}`);
    if (e.d2_t_shat() !== +T[3]) fail(`tick ${t}: meltdowns ${e.d2_t_shat()} != ${T[3]}`);
    // parts the second's EVENTS destroyed (heat): the client is told, it does not work them out -- apply them as the page would
    if (T[8] !== '-') for (const c of T[8].split(',')) e.d2_t_kill(+c);
    if (mask(e.d2_t_burn_lo(), e.d2_t_burn_hi()) !== BigInt('0x' + T[4])) fail(`tick ${t}: burned ${mask(e.d2_t_burn_lo(), e.d2_t_burn_hi()).toString(16)} != ${T[4]}`);
    if (mask(e.d2_t_dead_lo(), e.d2_t_dead_hi()) !== BigInt('0x' + T[5])) fail(`tick ${t}: destroyed ${mask(e.d2_t_dead_lo(), e.d2_t_dead_hi()).toString(16)} != ${T[5]}`);
    if (e.d2_t_qsum() !== +T[6]) fail(`tick ${t}: stored charge ${e.d2_t_qsum()} != ${T[6]}`);
    if (e.d2_t_gates() !== +T[7]) fail(`tick ${t}: open gates ${e.d2_t_gates()} != ${T[7]}`);
    if (+T[3] > 0 && t === 30) melt++;
  }
  ships++;
}
// the opponent's ship has its own simulation memory: run the first few hundred ships through it as well
{
  e.d2_t_set(132, 1);
  let k = 0;
  for (let i = 0; i < lines.length && k < 300;) {
    const s = lines[i++].split(' ');
    if (s[0] !== 'S') continue;
    load(s);
    for (let t = 0; t < 30; t++) e.d2_t_tick();
    k++;
  }
  e.d2_t_set(132, 0);
}
for (const [k, [a, n]] of Object.entries(REGIONS)) {
  const now = e.d2_t_sum(a, n);
  if (now !== sums0[k]) { bad++; console.log(`MEMORY: the ${k} region was overwritten while ships ran (a fight wrote over it)`); }
}
console.log(`${ships} ships, ${shotsTotal} shots, ${killsTotal} destroyed cells applied, ${melt} ships with a meltdown, ${items.size} of 24 items used, ${laneChecks} lane numbers, ${splitChecks} splits, ${heatCells} hot cells, ${boosted} boosted guns: ${bad ? bad + ' MISMATCHES' : 'every number agrees with the C core'}`);
process.exit(bad ? 1 : 0);
