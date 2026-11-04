// scripts/trapx_wasm_test.mjs -- the wasm city, checked against the JavaScript city it replaces.
//
//   node scripts/trapx_wasm_test.mjs build/trapx.wasm [WOTAN_DIR]
//
// Two jobs:
//   1. BEHAVIOUR: the numbers trapx_test.c (C) and trapx-rules-test.mjs (JS) both assert, asked of the
//      wasm module instead. 45/20 alertness, a 40/60/refused claim ladder, the School's 90 premium, the
//      portal keeping memory, 28 heat and -34 trust from a witnessed killing, K9 worths, the rogue
//      swarm, the 256-event ring.
//   2. EQUIVALENCE: the same scripted command sequence run through WOTAN/trapx/trapx-city.js and through
//      the wasm, comparing every district field, the Flow bank, the receipts and the ledger digest at
//      each step. If the two cities ever disagree, that is a real regression in one of them, not a
//      tolerance to widen.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const wasmPath = process.argv[2] || path.join(here, '..', 'build', 'trapx.wasm');
// Resolved against the CALLER's cwd, then handed to import() as a file URL: a bare relative specifier
// would resolve against this module's own directory instead, and silently look in PARENA/WOTAN.
const wotanDir = path.resolve(process.argv[3] || path.join(here, '..', '..', 'WOTAN'));

let pass = 0, fail = 0;
const ok = (c, m) => { if (c) pass++; else { fail++; console.log('FAIL: ' + m); } };
const eq = (got, want, m) => { if (got === want) pass++; else { fail++; console.log(`FAIL: ${m} (got ${got} want ${want})`); } };

if (!fs.existsSync(wasmPath)) {
  console.log(`FAIL: no module at ${wasmPath} (run scripts/build_trapx_wasm.sh build/trapx.wasm)`);
  process.exit(1);
}
const bytes = fs.readFileSync(wasmPath);
const mod = new WebAssembly.Module(bytes);
eq(WebAssembly.Module.imports(mod).length, 0, 'the module imports nothing');

const { exports: W } = await WebAssembly.instantiate(mod, {});

// field indexes, mirrored from stdlib/trapx/web/tp_mem.prn
// Keys are the JS city's own field names (trapx-city.js), indexes are this module's words: that way the
// equivalence loop can compare jd[name] with tp_di(scene, k) without a translation table to get wrong.
const D = { alertness: 1, trust: 2, heat: 3, enforcement: 4, fos_total: 5, fos_held: 6, attention: 7, integrity: 8, pressure: 9, k9: 10, kills: 11, clean_receipts: 12, dirty_receipts: 13, rogue: 14 };
const G = { tick: 0, flow: 1, scene: 2, receipts: 3, ledger: 4, seq: 5, head: 6, mods: 7 };
const d = (scene, k) => W.tp_di(scene, k);
const g = (k) => W.tp_g(k);

W.tp_init();
eq(g(G.flow), 100, 'a city starts with 100 Flow');
eq(g(G.scene), 200, 'and stands in the Apartment');
eq(g(G.mods), 3, 'VS0 ships three mods');
eq(d(200, D.fos_total), 2, 'the Apartment holds two Field Offices');
eq(d(201, D.fos_total), 1, 'the School holds one');

// watchers: +9 a tick in Jiangshi territory, +4 in the custody zone
W.tp_init();
for (let i = 0; i < 5; i++) W.tp_tick(0);
eq(d(200, D.alertness), 45, 'five Apartment ticks are +9 each');
eq(d(201, D.alertness), 20, 'the custody zone is +4 a tick');

W.tp_init();
W.tp_tick(1);
eq(d(200, D.alertness), 3, 'a quiet tick is 3');

// claiming: the ladder, the refusal, and the bank
W.tp_init();
eq(W.tp_claim(201), 2, 'you cannot claim where you are not');
eq(W.tp_claim(200), 0, 'the first Apartment office is claimable');
eq(g(G.flow), 60, 'and costs 40');
eq(W.tp_claim(200), 0, 'the second is claimable');
eq(g(G.flow), 0, 'at 40 then 60');
eq(W.tp_claim(200), 3, 'the third is refused: the scene holds two');

W.tp_init();
eq(W.tp_claim(201), 2, 'a fresh city is in the Apartment, not the School');
W.tp_portal(201);
eq(W.tp_claim(201), 0, 'the School office is claimable through the portal');
eq(g(G.flow), 10, 'at the custody premium: 90');
eq(W.tp_claim(201), 3, 'the School holds exactly one');

// the portal keeps one city's memory
W.tp_init();
for (let i = 0; i < 3; i++) W.tp_tick(0);
eq(d(200, D.alertness), 27, 'three ticks in the Apartment');
eq(W.tp_portal(201), 0, 'the portal to the School is open');
W.tp_tick(0);
eq(W.tp_portal(200), 0, 'and back');
eq(d(200, D.alertness), 36, 'the Apartment kept counting while you were away');
eq(W.tp_portal(200), 6, 'no portal from a scene to itself');
eq(W.tp_portal(204), 1, 'and none to a district VS0 does not have');

// BIG_O's half
W.tp_init();
const trust0 = d(200, D.trust);
eq(W.tp_kill(200, 5), 0, 'a killing with five witnesses');
eq(d(200, D.heat), 28, 'adds 8 + 4 a witness');
eq(d(200, D.trust), trust0 - 34, 'and costs 4 + 6 a witness of trust');
ok(d(200, D.enforcement) >= 1, 'the city reacted before the next tick');
eq(W.tp_kill(200, -1), 7, 'a negative witness count is refused');

// K9 and Attention
W.tp_init();
W.tp_claim(200);
eq(W.tp_k9(200, 2), 0, 'two dogs on the street');
eq(d(200, D.attention), 14, 'raise Attention by 7 a unit');
eq(W.tp_k9(200, 6), 7, 'six units is out of range');

// a beaten office swarms, once
W.tp_init();
W.tp_claim(200);
W.tp_tick(0);
// push the district to the trigger through memory the module owns (the same fields the C test sets)
const mem = () => new DataView(W.memory.buffer);
const distBase = 135168;
const setF = (scene, k, v) => mem().setInt32((scene === 200 ? distBase : distBase + 64) + k * 4, v, true);
setF(200, D.integrity, 15); setF(200, D.attention, 85); setF(200, D.pressure, 70);
W.tp_tick(0);
eq(d(200, D.rogue), 1, 'a beaten office under hard attention and pressure swarms');

// the ring wraps, the sequence does not. Pushed directly, like the JS test's pushEvent loop -- a tick
// only announces on a band or level change, so 300 ticks is NOT 300 events.
W.tp_init();
for (let i = 0; i < 300; i++) W.tp_push(0, 200, i, 0);
eq(W.tp_ev_count(), 256, 'the ring holds 256 and no more');
eq(g(G.seq), 300, 'but the sequence keeps counting');
eq(W.tp_ev(0, 3), 299, 'newest first');

// MODS FIRST: only the ledger wants a receipt
W.tp_init();
W.tp_claim(200);
const newest = W.tp_ev(0, 0);
eq(newest, 6, 'a claim lands a RECEIPT (kind 6) as the newest event');
eq(W.tp_deliver(6, 0), 0, 'the ledger mod (slot 0) is delivered a receipt');
eq(W.tp_deliver(6, 1), -1, 'and it is the only one');
eq(W.tp_deliver(0, 0), -1, 'nobody subscribed to ticks');

// ---- equivalence: the same script through the JavaScript city -------------------------------
if (fs.existsSync(path.join(wotanDir, 'trapx', 'trapx-city.js'))) {
  const C = await import(pathToFileURL(path.join(wotanDir, 'trapx', 'trapx-city.js')).href);
  const js = C.initCity(C.APARTMENT);
  W.tp_init();
  const script = [
    ['tick', 0], ['tick', 0], ['tick', 0],
    ['claim', C.APARTMENT], ['claim', C.APARTMENT], ['claim', C.APARTMENT],
    ['k9', 3], ['contest', C.APARTMENT], ['kill', 4],
    ['portal', C.SCHOOL], ['claim', C.SCHOOL], ['tick', 0], ['tick', 0],
    ['k9school', 1], ['kill', 2],
    ['portal', C.APARTMENT], ['tick', 0], ['tick', 1], ['tick', 1],
  ];
  const applyJs = (op, arg) => {
    if (op === 'tick') C.tick(js, arg);
    else if (op === 'claim') C.claim(js, arg);
    else if (op === 'k9') C.deployK9(js, C.APARTMENT, arg);
    else if (op === 'contest') C.contest(js, arg);
    else if (op === 'kill') C.publicKill(js, js.scene, arg);
    else if (op === 'portal') C.portal(js, arg);
    else if (op === 'k9school') C.deployK9(js, C.SCHOOL, arg);
  };
  const applyWasm = (op, arg) => {
    if (op === 'tick') W.tp_tick(arg);
    else if (op === 'claim') W.tp_claim(arg);
    else if (op === 'k9') W.tp_k9(200, arg);
    else if (op === 'contest') W.tp_contest(arg);
    else if (op === 'kill') W.tp_kill(g(G.scene), arg);
    else if (op === 'portal') W.tp_portal(arg);
    else if (op === 'k9school') W.tp_k9(201, arg);
  };
  for (const [i, [op, arg]] of script.entries()) {
    applyJs(op, arg); applyWasm(op, arg);
    for (const scene of [200, 201]) {
      const jd = C.district(js, scene);
      for (const [name, k] of Object.entries(D)) {
        const jv = jd[name];
        const wv = d(scene, k);
        eq(wv, jv, `step ${i} (${op}) district ${scene} ${name}`);
      }
    }
    eq(g(G.flow), js.flow, `step ${i} (${op}) flow`);
    eq(g(G.receipts), js.receipts, `step ${i} (${op}) receipts`);
    eq(g(G.ledger), js.ledger % 1000000007, `step ${i} (${op}) ledger`);
    eq(g(G.tick), js.tick, `step ${i} (${op}) tick`);
    eq(g(G.scene), js.scene, `step ${i} (${op}) scene`);
  }
} else {
  console.log('note: no WOTAN checkout beside PARENA -- equivalence block skipped');
}

if (fail > 0) {
  console.log(`trapx-wasm-test: ${fail} of ${pass + fail} FAILED`);
  process.exit(1);
}
console.log(`trapx-wasm-test OK (${pass} checks, ${(bytes.length / 1024).toFixed(1)} KB, 0 imports)`);
