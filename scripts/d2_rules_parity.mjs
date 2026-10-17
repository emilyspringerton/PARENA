// Checks the D2 web client's port of the ship rules (stdlib/d2/web/d2_rules.prn) against the real combat core: the C reference dump
// (DEADWEIGHT_2/tools/dw2_ref_dump.c) lists ships and what each did, tick by tick; the wasm must do the same for every one.
//   node scripts/d2_rules_parity.mjs <d2.wasm> <ref.txt>
import fs from 'node:fs';
const { instance } = await WebAssembly.instantiate(fs.readFileSync(process.argv[2]), {});
const e = instance.exports;
e.d2_init();
const lines = fs.readFileSync(process.argv[3], 'utf8').trim().split('\n');
let ships = 0, bad = 0, melt = 0, cuts = 0, shotsTotal = 0;
for (let i = 0; i < lines.length;) {
  const s = lines[i++].split(' ');
  if (s[0] !== 'S') throw new Error('bad line ' + lines[i - 1]);
  const idx = s[1], n = +s[2];
  e.d2_t_clear();
  for (let k = 0; k < n; k++) { const [it, r, c, rot, cut] = s[3 + k].split(',').map(Number); e.d2_t_add(it, r, c, rot, cut); cuts += cut; }
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
    const lo = e.d2_t_burn_lo() >>> 0, hi = e.d2_t_burn_hi() >>> 0;
    const mask = (BigInt(hi) << 32n) | BigInt(lo);
    if (shots !== +T[2]) fail(`tick ${t}: shots ${shots} != ${T[2]}`);
    if (e.d2_t_shat() !== +T[3]) fail(`tick ${t}: meltdowns ${e.d2_t_shat()} != ${T[3]}`);
    if (mask !== BigInt('0x' + T[4])) fail(`tick ${t}: burned ${mask.toString(16)} != ${T[4]}`);
    if (+T[3] > 0 && t === 30) melt++;
  }
  ships++;
}
console.log(`${ships} ships, ${shotsTotal} shots, ${melt} ships with a meltdown, ${cuts} cut items: ${bad ? bad + ' MISMATCHES' : 'every tick agrees with the C core'}`);
process.exit(bad ? 1 : 0);
