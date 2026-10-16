// Behavioural test for the BRAWLPIT wasm engine (stdlib/brawlpit/bp_*.prn): module hygiene, the menu
// and match flow driven purely through key events, rendering, hats and portraits, 2P / FFA /
// practice rules, determinism, and the CPU brain. (The physics itself is covered bit-for-bit by
// brawlpit_parity_test.mjs; the page host by brawlpit_host_test.mjs.)
//
//   node brawlpit_engine_test.mjs <brawlpit.wasm>
import fs from 'node:fs';
import { load } from './brawlpit_shot.mjs';

const wasmPath = process.argv[2];
let pass = 0, fail = 0;
const ok = (c, m) => { c ? pass++ : fail++; console.log((c ? 'PASS: ' : 'FAIL: ') + m); };

const PL = 139264, G = 135168, TN = 143360, CTL = 131072;
const K = { ENTER: 13, ESC: 27, SPACE: 32, LEFT: 37, UP: 38, RIGHT: 39, DOWN: 40, A: 65, D: 68, W: 87, S: 83, J: 74, K: 75, P: 80, R: 82, L: 76, SLASH: 191, QUOTE: 222, RCTRL: 273, RSHIFT: 272 };
const view = (g) => new DataView(g.x.memory.buffer);
const gi = (g, k) => view(g).getInt32(G + k * 4, true);
const pi = (g, p, k) => view(g).getInt32(PL + p * 512 + k * 4, true);
const pf = (g, p, k) => view(g).getFloat32(PL + p * 512 + k * 4, true);
const P = { id: 0, char: 1, active: 2, bot: 3, x: 4, y: 5, vx: 6, vy: 7, ground: 8, facing: 9, inx: 10, iny: 11, state: 20, dmg: 21, stocks: 22, hat: 59, pad: 60, label: 69 };
const phase = (g) => g.x.bp_phase();

// ---- 1. module hygiene ------------------------------------------------------------------------------
const bytes = fs.readFileSync(wasmPath);
const mod = new WebAssembly.Module(bytes);
ok(WebAssembly.Module.imports(mod).length === 0, 'imports nothing: no env, no libc, no host callbacks');
const exportNames = WebAssembly.Module.exports(mod).map((e) => e.name);
ok(!exportNames.some((n) => /^(malloc|calloc|realloc|free|sbrk|brk|memalign)$/.test(n)), 'exports no allocator (no malloc/free/realloc/calloc)');
for (const n of ['bp_init', 'bp_frame', 'bp_key', 'bp_pad', 'bp_pad_gone', 'bp_pointer', 'bp_blur', 'bp_fb_addr', 'bp_hat_addr', 'bp_hat_set', 'bp_sprite_addr', 'bp_sprite_set', 'memory'])
  ok(exportNames.includes(n), `exports the host contract entry point ${n}`);
{
  const g = await load(wasmPath);
  ok(g.x.memory.buffer.byteLength === 8 * 1024 * 1024, 'linear memory is exactly 8 MB');
  let grew = true;
  try { g.x.memory.grow(1); } catch { grew = false; }
  ok(!grew, 'linear memory cannot grow (initial == maximum): the engine has no heap');
  ok(g.x.bp_fb_w() === 960 && g.x.bp_fb_h() === 540 && g.x.bp_fb_addr() === 0x100000, 'framebuffer is 960x540 RGBA at 0x100000');
  // every fixed region ends below the next one: the stack (64 KB, below 0x20000) never meets the data
  ok(g.x.__heap_base.value <= 0x20000 && g.x.__data_end.value > 0, `static data and the 64 KB stack end at ${g.x.__heap_base.value} (below the first fixed region at 0x20000); nothing uses a heap`);
  ok(g.x.bp_hat_addr(7) + 4096 <= g.x.bp_fb_addr() && g.x.bp_sprite_addr(9) + 65536 <= 8 * 1024 * 1024, 'hat atlas and sprite region sit inside the fixed map');
}

// ---- 2. rendering -----------------------------------------------------------------------------------
const colors = (g, step = 211) => { const a = g.fb(), s = new Set(); for (let i = 0; i < a.length; i += 4 * step) s.add(a[i] | a[i + 1] << 8 | a[i + 2] << 16); return s; };
const opaque = (g) => { const a = g.fb(); for (let i = 3; i < a.length; i += 4 * 101) if (a[i] !== 255) return false; return true; };
const has = (g, r, gg, b) => { const a = g.fb(); for (let i = 0; i < a.length; i += 4) if (a[i] === r && a[i + 1] === gg && a[i + 2] === b) return true; return false; };
const countRGB = (g, r, gg, b) => { const a = g.fb(); let n = 0; for (let i = 0; i < a.length; i += 4) if (a[i] === r && a[i + 1] === gg && a[i + 2] === b) n++; return n; };
{
  const g = await load(wasmPath);
  g.frame(5);
  ok(colors(g).size > 12 && opaque(g), 'title screen: a real, fully opaque image');
  ok(phase(g) === 0, 'boots on the title screen');
}

// ---- 3. menu and match flow, by keys only -------------------------------------------------------------
async function startMatch(mode = 0, extra = () => {}) {
  const g = await load(wasmPath);
  g.frame(5);
  g.tap(49 + mode);                 // title hotkey 1-4
  g.frame(2);
  extra(g);
  g.tap(K.ENTER);                   // lock in P1 (or P1 then P2 in mode 1)
  if (mode === 1) g.tap(K.ENTER);   // P2
  g.tap(K.ENTER);                   // stage -> FIGHT
  return g;
}
{
  const g = await load(wasmPath);
  g.frame(5);
  g.tap(K.DOWN); g.tap(K.DOWN);
  g.tap(K.ENTER);
  ok(phase(g) === 1 && gi(g, 50) === 2, 'title: Down, Down, Enter opens select with mode 2 (free for all)');
  g.tap(K.ESC);
  ok(phase(g) === 0, 'Esc on select goes back to the title');
  g.tap(53);                         // 5 = controls
  ok(phase(g) === 4, 'title hotkey 5 opens the controls screen');
  ok(colors(g).size > 8, 'controls screen renders');
  g.tap(K.ENTER);
  ok(phase(g) === 0, 'Enter leaves the controls screen');
}
{
  const g = await startMatch(0);
  ok(phase(g) === 2, 'VS CPU: lock in fighter + stage starts a match');
  ok(gi(g, 4) === 2 && pi(g, 0, P.active) === 1 && pi(g, 1, P.active) === 1 && pi(g, 1, P.bot) === 1, 'two fighters, the second is a CPU');
  ok(pi(g, 0, P.label) === 1 && pi(g, 1, P.label) === 3, 'name tags: P1 and CPU');
  ok(pf(g, 0, P.x) === -10 && pf(g, 1, P.x) === 10 && pi(g, 0, P.facing) === 1 && pi(g, 1, P.facing) === -1, 'fighters spawn apart, facing each other');
  const s0 = g.x.bp_sim_frames();
  g.frame(100);
  ok(g.x.bp_sim_frames() === s0, 'the world is frozen during READY / 3 / 2 / 1 (no sim ticks yet)');
  g.frame(100);
  ok(g.x.bp_sim_frames() > 40, 'then the simulation runs');
  ok(colors(g).size > 12 && opaque(g) && has(g, 0, 255, 255), 'the match view draws (cyan platform rim present)');
  // pause / resume / restart / quit
  g.tap(K.P);
  const frozen = g.x.bp_sim_frames();
  g.frame(60);
  ok(gi(g, 26) === 1 && g.x.bp_sim_frames() === frozen, 'P pauses: the simulation stops');
  g.tap(K.P);
  g.frame(10);
  ok(gi(g, 26) === 0 && g.x.bp_sim_frames() > frozen, 'P resumes');
  g.tap(K.ESC); g.tap(K.R);
  ok(phase(g) === 2 && gi(g, 52) > 100, 'pause menu: R restarts the match (new countdown)');
  g.tap(K.ESC); g.tap(81);
  ok(phase(g) === 0, 'pause menu: Q quits to the title');
}

// ---- 4. controls: P1 keys, both key sets for a lone human, pads --------------------------------------------
{
  const g = await startMatch(3);     // practice: nothing hurts, so movement is exactly the input
  g.frame(200);
  const x0 = pf(g, 0, P.x);
  g.hold(K.D, 25); g.frame(2);
  ok(pf(g, 0, P.x) > x0 + 4 && pi(g, 0, P.facing) === 1, 'D moves P1 right and faces right');
  g.hold(K.A, 25); g.frame(2);
  ok(pf(g, 0, P.vx) < 0 || pi(g, 0, P.facing) === -1, 'A turns P1 around');
  const j0 = pf(g, 0, P.y);
  g.key(K.SPACE, true); g.frame(8); g.key(K.SPACE, false);
  ok(pf(g, 0, P.y) > j0 + 1.5 || pi(g, 0, P.ground) === 0, 'Space jumps');
  g.frame(80);
  g.key(K.RIGHT, true); g.frame(12);
  ok(pf(g, 0, P.vx) > 0.5, 'arrow keys also drive a lone human');
  g.key(K.RIGHT, false);
  g.key(K.J, true); g.frame(2); g.key(K.J, false);
  g.frame(1);
  ok(pi(g, 0, 45) > 0 || pi(g, 0, P.state) === 3 || pi(g, 0, 44) > 0, 'J starts an attack');
}
{
  const g = await startMatch(1);     // local 2 player
  g.frame(200);
  ok(pi(g, 1, P.bot) === 0 && pi(g, 1, P.label) === 2, '2P: slot 1 is a second human, tagged P2');
  const a0 = pf(g, 0, P.x), b0 = pf(g, 1, P.x);
  g.key(K.RIGHT, true); g.frame(20); g.key(K.RIGHT, false);
  ok(pf(g, 1, P.x) < b0 - 4 || pf(g, 1, P.x) !== b0, 'arrow keys move P2 (they push toward / away from P1)');
  g.frame(60);
  const a1 = pf(g, 0, P.x);
  ok(Math.abs(a1 - a0) < 3, '... and do NOT move P1 in a 2P match');
  g.key(K.D, true); g.frame(20); g.key(K.D, false); g.frame(2);
  ok(pf(g, 0, P.x) > a1 + 3, 'WASD moves P1');
}
{
  // gamepad plumbing: a pad drives menus and player 1 in a 1P match; one pad plays P2 in 2P
  const g = await load(wasmPath);
  g.frame(5);
  const setPad = (slot, lx, ly, buttons) => g.x.bp_pad(slot, lx, ly, 0, 0, 0, 0, buttons);
  const padTap = (b) => { setPad(0, 0, 0, 1 << b); g.frame(3); setPad(0, 0, 0, 0); g.frame(3); };
  padTap(13);                        // d-pad down
  ok(gi(g, 17) === 1, 'a gamepad d-pad moves the title menu cursor');
  padTap(0);                         // A
  ok(phase(g) === 1 && gi(g, 50) === 1, 'A on the pad activates the menu item (local 2 player)');
  padTap(0); padTap(0); padTap(0);   // lock P1, lock P2, fight
  g.frame(200);
  ok(phase(g) === 2 && pi(g, 1, P.pad) === 0 && pi(g, 0, P.pad) === -1, 'one pad in a 2P match is assigned to player 2');
  const x1 = pf(g, 1, P.x);
  setPad(0, -1, 0, 0); g.frame(20);   // stick left
  ok(pf(g, 1, P.x) < x1 - 3, 'the pad stick moves player 2');
  setPad(0, 0, 0, 0);
  g.x.bp_pad_gone(0);
}

// ---- 5. rules: practice, free-for-all, results ----------------------------------------------------------------
{
  const g = await startMatch(3);
  g.frame(200 + 60 * 20);
  ok(pi(g, 0, P.stocks) === 4 && pi(g, 1, P.stocks) === 4 && pf(g, 0, P.dmg) === 0 && pf(g, 1, P.dmg) === 0, 'practice: 20 s of play, nobody takes damage or loses a stock');
}
{
  const g = await startMatch(2);     // free for all: P1 + 3 CPU
  ok(gi(g, 4) === 4 && [0, 1, 2, 3].every((p) => pi(g, p, P.active) === 1) && [1, 2, 3].every((p) => pi(g, p, P.bot) === 1), 'free for all: four fighters, three CPUs');
  ok([pi(g, 1, P.label), pi(g, 2, P.label), pi(g, 3, P.label)].join() === '3,4,5', 'CPU tags are numbered (CPU, CPU2, CPU3)');
  const xs = [0, 1, 2, 3].map((p) => pf(g, p, P.x));
  ok(new Set(xs).size === 4, 'four different spawn positions');
  g.frame(250);
  // knock two fighters out by hand: the match must carry on with two left
  const setI = (p, k, v) => view(g).setInt32(PL + p * 512 + k * 4, v, true);
  setI(1, P.stocks, 0); setI(1, P.state, 6);
  setI(2, P.stocks, 0); setI(2, P.state, 6);
  g.frame(30);
  ok(gi(g, 2) === 0 && phase(g) === 2, 'free for all: the match continues while two fighters remain');
  setI(3, P.stocks, 0); setI(3, P.state, 6);
  g.frame(140);
  ok(phase(g) === 3 && gi(g, 13) === 0, 'last one standing wins: results screen, winner is P1');
  ok(colors(g).size > 12, 'results screen renders');
  g.frame(60);
  g.tap(K.R);
  ok(phase(g) === 2, 'results: R starts a rematch');
}
{
  const g = await startMatch(0);
  g.frame(210);
  const setI = (p, k, v) => view(g).setInt32(PL + p * 512 + k * 4, v, true);
  setI(0, P.stocks, 0); setI(0, P.state, 6);
  g.frame(160);
  ok(phase(g) === 3 && gi(g, 13) === 1, 'when P1 is out, the CPU wins');
  g.frame(60);
  g.tap(67);
  ok(phase(g) === 1, 'results: C returns to fighter select');
}

// ---- 6. portraits and hats are drawn by the wasm ---------------------------------------------------------------
// The equipped Hat Store hat is worn BY DEFAULT: the page decodes it (a network round trip after boot) and
// calls bp_hat_set(0, 1); nothing needs to be picked on the select screen. (Founder, 2026-10-16: "the hat
// store hat thats equipped should be worn by the player in brawlpit" -- it used to be opt-in via up / down.)
const uploadHat = (g, r = 250, gr = 5, b = 5) => {          // a 32x32 solid colour, opaque, into hat slot 0
  const hat = new Uint8Array(g.x.memory.buffer, g.x.bp_hat_addr(0), 4096);
  for (let i = 0; i < 1024; i++) { hat[i * 4] = r; hat[i * 4 + 1] = gr; hat[i * 4 + 2] = b; hat[i * 4 + 3] = 255; }
  g.x.bp_hat_set(0, 1);
};
const toSelect = async (hatFirst) => {                       // boot, optionally with the store hat already in, open practice's select
  const g = await load(wasmPath);
  g.frame(5);
  if (hatFirst) uploadHat(g);
  g.tap(49 + 3);                                             // practice (hotkey 4)
  g.frame(2);
  return g;
};
const fight = (g) => { g.tap(K.ENTER); g.tap(K.ENTER); g.frame(220); };   // lock in the fighter, stage -> FIGHT, past the countdown
let redNoHat = 0;
{
  const g = await toSelect(false);
  ok(gi(g, 48) === -1, 'no store hat loaded: player 1 defaults to NO HAT');
  fight(g);
  ok(pi(g, 0, P.hat) === -1, 'no store hat loaded: the fighter wears nothing');
  redNoHat = countRGB(g, 250, 5, 5);
}
{
  const g = await toSelect(true);
  ok(gi(g, 48) === 0 && gi(g, 59) === 0, 'a loaded store hat is player 1\'s default hat, with no key pressed');
  fight(g);                                                  // no UP / DOWN anywhere
  ok(pi(g, 0, P.hat) === 0, 'the equipped store hat is worn by player 1 without being picked');
  ok(pi(g, 1, P.hat) === -1, 'the CPU opponent does not wear the player\'s store hat');
  ok(countRGB(g, 250, 5, 5) > redNoHat + 200, 'the hat\'s pixels are blitted onto the fighter by the wasm');
}
{
  const g = await toSelect(false);                           // the hat lands while the select screen is open
  ok(gi(g, 48) === -1, 'before the hat arrives: NO HAT');
  uploadHat(g);
  ok(gi(g, 48) === 0, 'a store hat that arrives late (select screen open) becomes the default');
  fight(g);
  ok(pi(g, 0, P.hat) === 0, '...and is worn in the match');
}
{
  const g = await toSelect(false);                           // the hat lands after the fight has begun
  fight(g);
  ok(pi(g, 0, P.hat) === -1 && g.x.bp_phase() === 2, 'a match that started before the hat arrived: bare-headed');
  uploadHat(g);
  ok(pi(g, 0, P.hat) === 0, 'the hat arrives mid-match: player 1 puts it on at once');
  g.frame(4);
  ok(countRGB(g, 250, 5, 5) > redNoHat + 200, '...and it is drawn on the next frame');
  g.x.bp_hat_set(0, 0);
  ok(pi(g, 0, P.hat) === -1, 'the hat going away takes it off the fighter');
}
{
  const g = await toSelect(true);                            // the player deliberately goes bare-headed
  g.tap(K.DOWN);                                             // WOTAN HAT -> NO HAT
  ok(gi(g, 48) === -1 && gi(g, 59) === 1, 'down on the select screen: NO HAT, and the choice is marked as made by hand');
  uploadHat(g);                                              // the page announces the hat again
  ok(gi(g, 48) === -1, 'a hat announced after a deliberate NO HAT does not override the choice');
  fight(g);
  ok(pi(g, 0, P.hat) === -1 && countRGB(g, 250, 5, 5) <= redNoHat + 5, 'a deliberate NO HAT is respected in the match');
}
{
  const g = await toSelect(true);                            // the colour hats are still there to pick
  g.tap(K.UP);                                               // WOTAN HAT -> BLUE HAT
  ok(gi(g, 48) === 8, 'up from the store hat: BLUE HAT');
  g.tap(K.UP); g.tap(K.UP);
  ok(gi(g, 48) === 10, 'RED, then GREEN');
  g.tap(K.UP);
  ok(gi(g, 48) === -1, 'then round to NO HAT');
  g.tap(K.UP);
  ok(gi(g, 48) === 0, 'and back to the store hat');
}
{
  const g = await load(wasmPath);                            // local 2-player: the store hat is the signed-in player's, not P2's
  g.frame(5);
  uploadHat(g);
  g.tap(49 + 1); g.frame(2);                                 // 2 PLAYERS
  g.tap(K.ENTER); g.tap(K.ENTER); g.tap(K.ENTER); g.frame(220);
  ok(pi(g, 0, P.hat) === 0 && pi(g, 1, P.hat) === -1 && gi(g, 49) === -1, 'local 2P: player 1 wears the store hat, player 2 does not');
}
{
  const g = await load(wasmPath);
  g.frame(3);
  // portrait: slot for character 5 filled with pure green; select that fighter and look for it
  const sp = new Uint8Array(g.x.memory.buffer, g.x.bp_sprite_addr(5), 65536);
  for (let i = 0; i < 16384; i++) { sp[i * 4] = 3; sp[i * 4 + 1] = 250; sp[i * 4 + 2] = 3; sp[i * 4 + 3] = 255; }
  g.x.bp_sprite_set(5, 1);
  g.tap(49); g.frame(2);
  for (let i = 0; i < 2; i++) g.tap(K.RIGHT);       // rosie(3) -> sequel duck(5)
  g.frame(2);
  ok(countRGB(g, 3, 250, 3) > 5000, 'a character portrait uploaded by the page is drawn large on the select screen');
}

// ---- 7. determinism ------------------------------------------------------------------------------------------
{
  const run = async () => {
    const g = await startMatch(0);
    g.frame(200);
    for (let i = 0; i < 400; i++) {
      if (i % 40 === 0) g.key(K.D, true); if (i % 40 === 20) g.key(K.D, false);
      if (i % 53 === 0) g.key(K.J, true); if (i % 53 === 3) g.key(K.J, false);
      g.frame(1);
    }
    const st = []; for (let p = 0; p < 2; p++) for (const k of [4, 5, 6, 7, 20, 21, 22]) st.push(pi(g, p, k));
    let h = 0; const fb = g.fb(); for (let i = 0; i < fb.length; i += 7) h = (h * 31 + fb[i]) | 0;
    return { st: st.join(), h };
  };
  const a = await run(), b = await run();
  // bp_seed uses the page clock, which differs between runs; the stored state words depend on it only
  // through CPU choices, so determinism is asserted on the engine with a fixed seed below.
  ok(typeof a.h === 'number' && typeof b.h === 'number', 'two scripted sessions run to completion');
}
{
  const sim = async () => {
    const g = await load(wasmPath);
    g.x.init_fighters(); g.x.bp_seed_bang(4242);
    g.x.set_slot_char_bang(0, 6); g.x.set_slot_char_bang(1, 9);
    g.x.sim_init_match(2, 0, 1);
    const trace = [];
    for (let t = 0; t < 1500; t++) {
      g.x.sim_set_input_bang(0, (t % 90 < 45) ? 1 : -1, 0, (t % 70) === 0 ? 1 : 0, (t % 31) < 3 ? 1 : 0, 0, (t % 200) === 100 ? 1 : 0);
      g.x.sim_step();
      if (t % 25 === 0) for (const k of [4, 5, 21, 22, 20]) trace.push(pi(g, 0, k), pi(g, 1, k));
    }
    return trace.join();
  };
  ok((await sim()) === (await sim()), 'the same seed and inputs reproduce the same match, bit for bit (fixed-step, no hidden state)');
}

// ---- 8. the CPU brain stays on the stage and fights --------------------------------------------------------------
{
  const g = await load(wasmPath);
  const runAI = (level, stage, seed) => {
    g.x.init_fighters(); g.x.bp_seed_bang(seed);
    view(g).setInt32(G + 57 * 4, level, true);
    g.x.set_slot_char_bang(0, (seed * 3) % 10); g.x.set_slot_char_bang(1, (seed * 7 + 2) % 10);
    g.x.sim_init_match(2, 0, stage);
    let hits = 0, selfKO = 0, kos = 0, prevS = [4, 4], prevD = [0, 0], t = 0;
    for (; t < 7200; t++) {
      g.x.bot_think_web(0);
      g.x.sim_step();
      for (let p = 0; p < 2; p++) {
        const st = pi(g, p, P.stocks), d = pf(g, p, P.dmg);
        if (st < prevS[p]) { kos++; if (prevD[p] < 35) selfKO++; }
        if (d > prevD[p] + 0.5) hits++;
        prevS[p] = st; prevD[p] = d;
      }
      if (gi(g, 2)) break;
    }
    return { hits, selfKO, kos, t };
  };
  let hits = 0, selfKO = 0, kos = 0, n = 0, decided = 0;
  for (let s = 1; s <= 10; s++) { const r = runAI(2, [0, 1, 4][s % 3], s * 977); hits += r.hits; selfKO += r.selfKO; kos += r.kos; n++; if (r.kos >= 4) decided++; }
  ok(hits / n >= 8, `CPU vs CPU (normal): they actually fight, ${(hits / n).toFixed(1)} hits per match`);
  ok(selfKO / n <= 1.2, `CPU vs CPU (normal): few self-KOs at low damage (${(selfKO / n).toFixed(2)} per match; the C bot walks off the edge constantly)`);
  ok(decided >= 8, `CPU vs CPU: matches finish (${decided}/10 reached a decision within two minutes)`);
}

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
