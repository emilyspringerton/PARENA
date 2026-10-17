// Checks the D2 web client's port of the ship rules (stdlib/d2/web/d2_rules.prn) against the real combat core: the C reference dump
// (DEADWEIGHT_2/tools/dw2_ref_dump.c) lists ships and what each did, tick by tick; the wasm must do the same for every one.
//   node scripts/d2_rules_parity.mjs <d2.wasm> <ref.txt>
import fs from 'node:fs';
const { instance } = await WebAssembly.instantiate(fs.readFileSync(process.argv[2]), {});
const e = instance.exports;
e.d2_init();
const lines = fs.readFileSync(process.argv[3], 'utf8').trim().split('\n');
// the catalog, the examples and the font are written once at init: no fight may land on them (the ship simulations are 4 KB each, and a layout
// change once put a ship's stored charge on top of the examples)
const REGIONS = { items: [191488, 19 * 128], examples: [194048, 6 * 128], font: [196608, 255 * 16] };
const sums0 = Object.fromEntries(Object.entries(REGIONS).map(([k, [a, n]]) => [k, e.d2_t_sum(a, n)]));
let ships = 0, bad = 0, melt = 0, cuts = 0, shotsTotal = 0, dmgTotal = 0, mended = 0, items = new Set();
for (let i = 0; i < lines.length;) {
  const s = lines[i++].split(' ');
  if (s[0] !== 'S') throw new Error('bad line ' + lines[i - 1]);
  const idx = s[1], n = +s[2];
  e.d2_t_clear();
  for (let k = 0; k < n; k++) { const [it, r, c, rot, cut] = s[3 + k].split(',').map(Number); e.d2_t_add(it, r, c, rot, cut); cuts += cut; items.add(it); }
  const problems = e.d2_t_rebuild();
  const a = lines[i++].split(' ');
  const fail = (m) => { bad++; if (bad < 8) console.log(`ship ${idx}: ${m}`); };
  if (problems !== 0) fail(`wasm found ${problems} illegal placements in a ship the server accepted`);
  if (e.d2_t_armor() !== +a[1]) fail(`armor ${e.d2_t_armor()} != ${a[1]}`);
  if (e.d2_t_cargo() !== +a[2]) fail(`cargo ${e.d2_t_cargo()} != ${a[2]}`);
  for (let t = 1; t <= 30; t++) {
    const T = lines[i++].split(' ');
    const shots = e.d2_t_tick();
    shotsTotal += shots;
    const dmg = e.d2_t_dmg(); dmgTotal += dmg;
    const lo = e.d2_t_burn_lo() >>> 0, hi = e.d2_t_burn_hi() >>> 0;
    const mask = (BigInt(hi) << 32n) | BigInt(lo);
    if (dmg !== +T[2]) fail(`tick ${t}: damage dealt ${dmg} != ${T[2]}`);
    if (e.d2_t_shat() !== +T[3]) fail(`tick ${t}: meltdowns ${e.d2_t_shat()} != ${T[3]}`);
    if (mask !== BigInt('0x' + T[4])) fail(`tick ${t}: burned ${mask.toString(16)} != ${T[4]}`);
    if (e.d2_t_hull() !== +T[5]) fail(`tick ${t}: hull ${e.d2_t_hull()} != ${T[5]}`);
    if (e.d2_t_sarmor() !== +T[6]) fail(`tick ${t}: armor ${e.d2_t_sarmor()} != ${T[6]}`);
    if (e.d2_t_qsum() !== +T[7]) fail(`tick ${t}: stored charge ${e.d2_t_qsum()} != ${T[7]}`);
    if (e.d2_t_gates() !== +T[8]) fail(`tick ${t}: open gates ${e.d2_t_gates()} != ${T[8]}`);
    if (+T[3] > 0 && t === 30) melt++;
    if (t === 30 && +T[5] > 40) mended++;
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
    e.d2_t_clear();
    for (let q = 0; q < +s[2]; q++) { const [it, r, c, rot, cut] = s[3 + q].split(',').map(Number); e.d2_t_add(it, r, c, rot, cut); }
    e.d2_t_rebuild();
    for (let t = 0; t < 30; t++) e.d2_t_tick();
    k++;
  }
  e.d2_t_set(132, 0);
}
for (const [k, [a, n]] of Object.entries(REGIONS)) {
  const now = e.d2_t_sum(a, n);
  if (now !== sums0[k]) { bad++; console.log(`MEMORY: the ${k} region was overwritten while ships ran (a fight wrote over it)`); }
}
console.log(`${ships} ships, ${shotsTotal} shots, ${dmgTotal} damage, ${melt} ships with a meltdown, ${mended} that mended, ${cuts} cut items, ${items.size} of 19 items used: ${bad ? bad + ' MISMATCHES' : 'every tick agrees with the C core'}`);
process.exit(bad ? 1 : 0);
