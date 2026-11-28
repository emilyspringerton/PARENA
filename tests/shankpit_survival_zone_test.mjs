// tests/shankpit_survival_zone_test.mjs -- SURVIVAL's zones (sv_zone.prn): meadow scene per GFD room, edge exits, arrival side.
//   node tests/shankpit_survival_zone_test.mjs <survival.wasm>
import fs from 'node:fs';
let pass = 0, fail = 0;
const ok = (c, m) => { c ? pass++ : fail++; console.log((c ? 'PASS: ' : 'FAIL: ') + m); };
const { instance } = await WebAssembly.instantiate(fs.readFileSync(process.argv[2]), {});
const x = instance.exports; x.sp_init();
let now = 1000;
const dv = () => new DataView(x.memory.buffer);
const frame = (n = 1) => { for (let i = 0; i < n; i++) { now += 16; x.sp_frame(now); } };
const gi = (k) => dv().getInt32(135168 + k * 4, true);
const hx = () => dv().getFloat32(x.sp_player_addr(0) + 16, true), hz = () => dv().getFloat32(x.sp_player_addr(0) + 24, true);
const place = (px, pz) => x.sp_t_place(0, px, 6, pz, 0, 0);
const trees = () => gi(623);

x.sp_zone(111, 0, 1);                       // the host does this right after load: meadow
frame(3); x.sp_pointer(320, 204); x.sp_button(0, 1); x.sp_button(0, 0); x.sp_lock(1); frame(140);
ok(gi(664) === 1, 'biome is the meadow');
const t1 = trees();
ok(t1 > 0, `the meadow grows trees (${t1})`);
ok(x.sp_zone_exit() === 0, 'no exit while standing in the middle');

frame(130); place(405, 0); frame(2);
ok(x.sp_zone_exit() === 0, 'inside the margin: no exit yet');
place(412, 0); frame(2);
ok(x.sp_zone_exit() === 3, `walking off the east edge asks to go east (${x.sp_zone_exit()})`);
x.sp_zone_ack(); frame(2);
ok(x.sp_zone_exit() === 0 && hx() < 380, `refused: stepped back in (x ${hx().toFixed(0)}) and the exit clears`);
frame(130);
place(0, -412); frame(2);
ok(x.sp_zone_exit() === 2, 'south edge = 2');
x.sp_zone_ack(); frame(130);

// the room changed: new seed, came heading east -> the west edge, z kept
place(412, 55); frame(2);
x.sp_zone(222, 3, 1); frame(3);
ok(hx() < -300 && Math.abs(hz() - 55) < 5, `arrived on the west edge (${hx().toFixed(0)}, ${hz().toFixed(0)})`);
ok(x.sp_zone_exit() === 0, 'a fresh arrival does not bounce straight back out');
const t2 = trees();
ok(t2 > 0, `the new zone has its own trees (${t2})`);
x.sp_zone(111, 4, 1); frame(3);
ok(trees() === t1 && hx() > 300, `the same room (seed) regrows the same forest (${trees()} = ${t1}) and arrival is on the east edge`);
x.sp_zone(333, 0, 1); frame(3);
ok(Math.abs(hx()) < 1 && Math.abs(hz()) < 1, 'entry 0 = the centre');
console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
