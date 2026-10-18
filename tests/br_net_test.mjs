// tests/br_net_test.mjs -- BEDROCK RACERS online play, engine side: the wire protocol the wasm speaks (against hand-built frames, independent of any
// server) and the property the whole design stands on: LOCKSTEP. A server-side copy of the module (the referee, set up the way IDUNA's wazero copy is:
// br_init_sim) sequences every person's command bytes tick by tick; two real CLIENT engines (full br_init, drawing every frame, different seats) play the
// same stream from TICK frames; all three must end in the same state, through a person leaving mid-match (turned into a bot at an exact tick) and a
// server-forced end.
//   node tests/br_net_test.mjs <bedrockracers.wasm>
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const W = process.argv[2];
if (!W) { console.error('usage: br_net_test.mjs <wasm>'); process.exit(2); }
const here = path.dirname(fileURLToPath(import.meta.url));
const web = path.join(here, '../stdlib/bedrockracers/web');
let pass = 0, fail = 0;
const ok = (c, m) => { c ? pass++ : fail++; console.log((c ? 'PASS: ' : 'FAIL: ') + m); };

const G = {};
for (const f of fs.readdirSync(web)) {
  if (!/^br_.*\.prn$/.test(f) || f === 'br_fields.prn') continue;
  for (const m of fs.readFileSync(path.join(web, f), 'utf8').matchAll(/\(defn g-([a-z0-9-]+) \[\] : I32 (\d+)\)/g)) G[m[1]] = +m[2];
}
const KEY = { enter: 13, esc: 27, backspace: 8, w: 87, f: 70, b: 66, shift: 16 };
const bytes = fs.readFileSync(W);

async function engine(initSim = false) {
  const { instance } = await WebAssembly.instantiate(bytes, {});
  const x = instance.exports;
  initSim ? x.br_init_sim() : x.br_init();
  let now = 1000;
  const mem = () => new DataView(x.memory.buffer);
  const e = {
    x, mem,
    gi: (n) => mem().getInt32(135168 + G[n] * 4, true),
    gf: (n) => mem().getFloat32(135168 + G[n] * 4, true),
    frame(n = 1) { for (let i = 0; i < n; i++) { now += 16; x.br_frame(now); } },
    key(id, down) { x.br_key(id, down ? 1 : 0); },
    tap(id) { x.br_key(id, 1); e.frame(2); x.br_key(id, 0); e.frame(1); },
    // a frame in: copy to NETIN and call br_net_recv
    recv(arr) { new Uint8Array(x.memory.buffer, x.br_net_in_addr(), arr.length).set(arr); x.br_net_recv(arr.length); },
    // frames out: [u16 len][bytes]... from NETOUT, then clear
    out() {
      const n = x.br_net_out_len(), b = new Uint8Array(x.memory.buffer, x.br_net_out_addr(), n).slice(), fr = [];
      for (let o = 0; o + 2 <= n;) { const l = b[o] | (b[o + 1] << 8); fr.push(b.slice(o + 2, o + 2 + l)); o += 2 + l; }
      x.br_net_out_clear();
      return fr;
    },
    fb: () => new Uint8Array(x.memory.buffer, x.br_fb_addr(), 640 * 360 * 4),
  };
  return e;
}
const u32 = (v) => [v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255];
const u16 = (v) => [v & 255, (v >>> 8) & 255];
const str = (s) => [s.length, ...[...s].map(c => c.charCodeAt(0))];
const f32 = (v) => { const b = new Uint8Array(4); new DataView(b.buffer).setFloat32(0, v, true); return [...b]; };
const rdU32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;

const WELCOME = (online = 7, auth = 1) => [0x81, 1, auth, ...u32(1), ...str('Pilot'), ...u16(online), ...str('pid-1')];
const STATUS = (qs, searching, online, elapsed, lobbyHumans = 0, lobbyMs = 0) => [0x82, qs, searching, ...u16(online), ...u16(elapsed), lobbyHumans, ...u16(lobbyMs)];
const MATCH = (room, slot, seed, level, humans, names) => [0x83, ...u32(room), slot, ...u32(seed), level, ...u16(humans), names.length, ...names.flatMap(([s, n]) => [s, ...str(n)])];
const TICK = (tick, cmds, drops = []) => [0x84, ...u32(tick), drops.length ? 1 : 0, ...(drops.length ? [drops.length, ...drops] : []), ...cmds.flat()];
const END = (reason, winner, scores, matchId, counted, before, after) => [0x87, reason, winner, ...scores, ...u32(matchId), counted, ...f32(before), ...f32(after)];
const cmdBytes = (thr, steer, flags = 0, lift = 0) => [thr & 255, steer & 255, 0, 0, 0, lift, flags, 0];

// ---- the client speaks the protocol --------------------------------------------------------------------------------------------------------------
{
  const c = await engine();
  c.frame(5);
  const name = [...'Pilot'].map(ch => ch.charCodeAt(0));
  new Uint8Array(c.x.memory.buffer, c.x.br_name_addr(), name.length).set(name); c.x.br_name_set(name.length);
  const tok = [...'tok-123'].map(ch => ch.charCodeAt(0));
  new Uint8Array(c.x.memory.buffer, c.x.br_token_addr(), tok.length).set(tok); c.x.br_token_set(tok.length);
  ok(c.x.br_net_wanted() === 0, 'at the title the engine wants no socket');
  c.tap(KEY.enter);
  ok(c.x.br_phase() === 1 && c.x.br_net_wanted() === 1, 'PLAY starts a bot match and asks the page for a socket (it wants to be matched)');
  ok(c.x.br_version() === 1, 'the module reports its sim version');
  c.x.br_net_socket(2);
  const hello = c.out();
  ok(hello.length >= 1 && hello[0][0] === 0x01 && hello[0][1] === 1 && rdU32(hello[0], 2) === 1 && (hello[0][6] | (hello[0][7] << 8)) === 7 &&
     String.fromCharCode(...hello[0].slice(8, 15)) === 'tok-123' && hello[0][15] === 5 && String.fromCharCode(...hello[0].slice(16, 21)) === 'Pilot',
    'on open it sends HELLO: version, sim version, the token, the display name');
  ok(hello.some(f => f[0] === 0x13), 'and a PING');
  ok(c.out().length === 0 && c.x.br_net_out_len() === 0, 'br_net_out_clear really clears');
  c.recv(WELCOME(7));
  const q = c.out();
  ok(c.x.br_net_welcomed() === 1 && c.x.br_net_auth() === 1 && c.x.br_net_online() === 7, 'WELCOME: signed in, 7 online');
  ok(q.length === 1 && q[0][0] === 0x02 && q[0][1] === 0 && q[0][2] === 2, 'a person who asked to play is queued at once: team 0 (amber), level 2');
  ok(c.x.br_net_status() === 1 && c.x.br_status() === 6, 'the status says searching, and the page label says so');
  c.recv(STATUS(1, 3, 9, 14));
  ok(c.x.br_net_online() === 9 && c.x.br_net_searching() === 3 && c.gi('q-elapsed') === 14, 'STATUS: 9 online, 3 searching, 14 s');
  c.recv(STATUS(2, 3, 9, 20, 3, 8000));
  c.frame(2);
  ok(c.x.br_net_status() === 2 && c.x.br_status() === 7 && c.gi('lobby-humans') === 3 && c.gi('lobby-ms') === 8000, 'STATUS: a lobby is counting down (8 s, 3 people): MATCH FOUND');
  const lobbyShot = Buffer.from(c.fb());
  c.recv(MATCH(77, 4, 424242, 2, (1 << 0) | (1 << 4) | (1 << 7), [[0, 'Ada1'], [4, 'Bea2'], [7, 'Cy3']]));
  c.frame(1);
  ok(c.x.br_phase() === 1 && c.gi('in-match') === 1 && c.gi('me') === 4 && c.gi('roster') === ((1 << 0) | (1 << 4) | (1 << 7)) && c.gi('seed') === 424242 && c.gi('bot-level') === 2,
    'MATCH: the local bot match is replaced by the online one: you are slot 4, three people, seed and level as announced');
  ok(c.x.br_status() === 5 && c.x.br_net_status() === 3, 'the label says online match');
  const nm = (s) => { const a = c.x.br_car_addr(s) - 0; return s; };   // names live at NAMES + slot * 32
  const nameOf = (s) => { const a = 172032 + s * 32; const u = new Uint8Array(c.x.memory.buffer, a, 32); let t = ''; for (const b of u) { if (!b) break; t += String.fromCharCode(b); } return t; };
  ok(nameOf(0) === 'Ada1' && nameOf(4) === 'Bea2' && nameOf(7) === 'Cy3' && nameOf(1) === 'Bram', 'the people carry their display names; the bots keep theirs');
  // nothing is stepped until the server sends ticks
  const t0 = c.gi('tick'), cd0 = c.gi('countdown');
  c.frame(30);
  ok(c.gi('tick') === t0 && c.gi('countdown') === cd0, 'with no ticks from the server the sim does not run (lockstep never runs ahead of the server)');
  // ticks arrive: 3 people, ascending slot order 0, 4, 7
  for (let t = 0; t < 10; t++) c.recv(TICK(t, [cmdBytes(0, 0), cmdBytes(127, 0, 0), cmdBytes(0, 0)]));
  c.frame(1);
  const done1 = cd0 - c.gi('countdown');
  c.frame(30);
  ok(cd0 - c.gi('countdown') === 10, `the engine plays exactly the 10 ticks it was given (the kickoff countdown fell ${cd0 - c.gi('countdown')})`);
  ok(c.gi('ntick-sim') === 10, 'and counts them');
  // a tick out of order is a broken stream: the engine leaves the room instead of playing a different match
  c.recv(TICK(99, [cmdBytes(0, 0), cmdBytes(0, 0), cmdBytes(0, 0)]));
  c.frame(2);
  ok(c.gi('net-err') === 90 && c.gi('in-match') === 0 && c.x.br_phase() === 2, 'a tick out of order ends the match here (net error 90) instead of drifting away from the others');
  const left = c.out();
  ok(left.some(f => f[0] === 0x03), 'and tells the server it is leaving');
}

// ---- commands go out when they change ----------------------------------------------------------------------------------------------------------------
{
  const c = await engine(); c.frame(3);
  c.tap(KEY.enter); c.x.br_net_socket(2); c.out(); c.recv(WELCOME()); c.out();
  c.recv(MATCH(1, 0, 99, 2, (1 << 0) | (1 << 3), [[0, 'Me'], [3, 'You']])); c.out();
  c.frame(2);
  const first = c.out().filter(f => f[0] === 0x10);
  ok(first.length === 1 && first[0].length === 9 && first[0].slice(1).every(b => b === 0), 'the first frame of an online match sends the (empty) command once');
  c.key(KEY.w, true); c.frame(2);
  const w = c.out().filter(f => f[0] === 0x10);
  ok(w.length >= 1 && w[w.length - 1][1] === 127, 'holding W sends throttle +127');
  c.key(KEY.f, true); c.frame(2);
  const f = c.out().filter(f => f[0] === 0x10);
  ok(f.length >= 1 && (f[f.length - 1][7] & 4) === 4, 'holding F sets the magnet flag');
  c.frame(5);
  ok(c.out().filter(f => f[0] === 0x10).length === 0, 'a held, unchanged command is not re-sent every frame');
  c.frame(25);
  ok(c.out().filter(f => f[0] === 0x10).length >= 1, 'but is repeated now and then so the server never doubts a quiet player');
  c.key(KEY.w, false); c.key(KEY.f, false);
  // the leave card: an online match cannot be paused
  c.tap(KEY.esc);
  ok(c.gi('leave-menu') === 1 && c.gi('paused') === 0, 'Esc in an online match opens the leave card and does NOT pause (the others are playing)');
  c.x.br_pause();
  ok(c.gi('paused') === 0 && c.x.br_status() === 5, 'losing focus does not pause an online match either');
  c.tap(KEY.esc);
  ok(c.gi('leave-menu') === 0, 'Esc again keeps playing');
  c.tap(KEY.esc); c.tap(KEY.backspace);
  ok(c.x.br_phase() === 0 && c.out().some(f => f[0] === 0x03) && c.x.br_net_wanted() === 0, 'Backspace on the leave card leaves: LEAVE is sent and the title wants no socket');
}

// ---- LOCKSTEP: a server copy and two clients end in the same state ------------------------------------------------------------------------------------------
{
  const SEED = 20261018, LEVEL = 2, HUMANS = (1 << 0) | (1 << 4) | (1 << 7), TICKS = 7000, DROP_AT = 2400, DROP_SLOT = 4, END_AT = 6500;
  const S = await engine(true);
  const A = await engine(), B = await engine();
  S.x.br_net_begin(SEED, LEVEL, HUMANS, 0);
  const welcome = (c) => { c.tap(KEY.enter); c.x.br_net_socket(2); c.out(); c.recv(WELCOME()); c.out(); };
  welcome(A); welcome(B);
  const names = [[0, 'Ada1'], [4, 'Bea2'], [7, 'Cy3']];
  A.recv(MATCH(5, 0, SEED, LEVEL, HUMANS, names)); B.recv(MATCH(5, 4, SEED, LEVEL, HUMANS, names));
  A.frame(1); B.frame(1);
  ok(S.x.br_net_roster() === HUMANS && A.gi('roster') === HUMANS && B.gi('roster') === HUMANS, 'all three copies start with the same people');
  // a scripted command stream: each person changes their mind every ~11-17 ticks (a tiny LCG, so the test is reproducible)
  let rng = 12345; const rnd = (n) => { rng = (rng * 1103515245 + 12345) >>> 0; return (rng >>> 16) % n; };
  const state = { 0: cmdBytes(0, 0), 4: cmdBytes(0, 0), 7: cmdBytes(0, 0) }; const until = { 0: 0, 4: 0, 7: 0 };
  let humans = HUMANS, bAlive = true, mismatches = 0, checks = 0;
  const pendAddr = S.x.br_net_pend_addr();
  const hashes = [];
  for (let t = 0; t < TICKS; t++) {
    const drops = [];
    if (t === DROP_AT) { S.x.br_net_drop(DROP_SLOT); humans &= ~(1 << DROP_SLOT); drops.push(DROP_SLOT); bAlive = false; }
    const cmds = [];
    for (const s of [0, 4, 7]) {
      if (!(humans >> s & 1)) continue;
      if (t >= until[s]) { until[s] = t + 11 + rnd(7); const thr = [127, 127, 0, -127][rnd(4)], steer = [-127, 0, 127, 60][rnd(4)];
        state[s] = [thr & 255, steer & 255, ([0, 0, 90, -90][rnd(4)]) & 255, ([0, 0, 40, -40][rnd(4)]) & 255, 0, [0, 0, 255, 128][rnd(4)], [0, 1, 4, 5, 4][rnd(5)], 0]; }
      cmds.push(state[s]);
      new Uint8Array(S.x.memory.buffer, pendAddr + s * 8, 8).set(state[s]);
    }
    S.x.br_net_tick();
    const fr = TICK(t, cmds, drops);
    A.recv(fr); if (bAlive) B.recv(fr);
    // the clients play what they were sent (frames also draw)
    while (A.gi('ntick-sim') <= t) A.frame(1);
    while (bAlive && B.gi('ntick-sim') <= t) B.frame(1);
    if (t % 500 === 499 || t === DROP_AT || t === DROP_AT + 1) {
      checks++;
      const hs = S.x.br_net_hash(), ha = A.x.br_net_hash(), hb = B.x.br_net_hash();
      hashes.push(hs);
      if (hs !== ha || (bAlive && hs !== hb)) { mismatches++; console.log(`  tick ${t}: hash S ${hs >>> 0} A ${ha >>> 0} B ${hb >>> 0}`); }
    }
  }
  ok(mismatches === 0 && checks >= 14, `the server copy and both clients agree on the whole match state at every one of ${checks} checkpoints (7000 ticks, one person dropped to a bot at tick ${DROP_AT})`);
  ok(S.x.br_net_roster() === (HUMANS & ~(1 << DROP_SLOT)) && A.gi('roster') === S.x.br_net_roster(), 'the drop turned the slot into a bot in the server copy and in the client at the same tick');
  ok(A.gi('tick') === S.x.br_net_ticks() && A.gi('tick') > 6000, `all three are at match tick ${S.x.br_net_ticks()}`);
  ok(new Set(hashes).size > 10, 'the hashes differ from checkpoint to checkpoint: the match really was moving');
  // the server ends the match and says so; the client that has played every tick ends with it
  S.x.br_net_end_now();
  const addr = S.x.br_net_result(); const R = new Uint8Array(S.x.memory.buffer, addr, 16 + 12 * 16).slice();
  const winner = R[0], reason = R[1];
  ok(S.x.br_net_phase() === 2 && reason === 4, `the server forces an end (reason 4), winner ${winner === 255 ? 'none' : 'team ' + winner}, scores ${[R[8], R[9], R[10], R[11]]}`);
  A.recv(END(reason, winner, [R[8], R[9], R[10], R[11]], 4242, 1, 1500.0, 1512.5));
  A.frame(2);
  ok(A.x.br_phase() === 2 && A.gi('end-reason') === 4 && A.gi('winner') === (winner === 255 ? -1 : winner), 'the client ends with the same winner and reason');
  ok(S.x.br_net_hash() === A.x.br_net_hash(), 'and its final state equals the server copy\'s, bit for bit');
  ok(A.gi('end-got') === 1 && A.gi('end-match-id') === 4242 && A.gi('end-counted') === 1 && Math.abs(A.gf('end-after') - 1512.5) < 1e-3 && A.x.br_net_match_id() === 4242,
    'the rating the server sent (1500 -> 1512.5, match 4242) is on the client for the results screen');
  const sumGoals = Array.from({ length: 12 }, (_, s) => R[16 + s * 16]).reduce((p, q) => p + q, 0);
  const sumScores = R[8] + R[9] + R[10] + R[11];
  ok(sumGoals >= sumScores, `the result block is coherent: ${sumScores} goals on the board, ${sumGoals} credited to cars (own goals take a point off)`);
  // the results screen draws the rating
  A.frame(5);
  ok(A.fb().some((v, i) => i % 4 === 0 && v > 100), 'the results screen draws');
}

// ---- the server copy initialised sim-only behaves exactly like a fully initialised one -------------------------------------------------------------------
{
  const S1 = await engine(true), S2 = await engine(false);
  for (const s of [S1, S2]) s.x.br_net_begin(777, 3, 1 << 1, 0);
  for (let t = 0; t < 1500; t++) {
    for (const s of [S1, S2]) { new Uint8Array(s.x.memory.buffer, s.x.br_net_pend_addr() + 8, 8).set(cmdBytes(t % 40 < 25 ? 127 : 0, t % 90 < 30 ? 80 : -80, t % 7 === 0 ? 5 : 1)); s.x.br_net_tick(); }
  }
  ok(S1.x.br_net_hash() === S2.x.br_net_hash() && S1.x.br_net_ticks() === S2.x.br_net_ticks() && S1.x.br_net_ticks() > 1200, 'br_init_sim (no meshes, no font) and br_init lead to the same match: the server can skip the drawing set-up');
}

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
