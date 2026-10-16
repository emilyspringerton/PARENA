// tests/shankpit460_engine_test.mjs -- the shankpit-460 wasm engine as a whole: module hygiene, screens, input, bot matches, the
// wire protocol (against hand-built frames, independent of any server) and determinism.
//   node tests/shankpit460_engine_test.mjs <shankpit460.wasm>
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const wasmPath = process.argv[2];
if (!wasmPath) { console.error('usage: shankpit460_engine_test.mjs <wasm>'); process.exit(2); }
const here = path.dirname(fileURLToPath(import.meta.url));
let pass = 0, fail = 0;
const ok = (c, m) => { c ? pass++ : fail++; console.log((c ? 'PASS: ' : 'FAIL: ') + m); };

const fields = {};
for (const m of fs.readFileSync(path.join(here, '../stdlib/shankpit460/sp_fields.prn'), 'utf8').matchAll(/\(defn k-([a-z0-9-]+) \[\] : I32 (\d+)\)/g)) fields[m[1]] = +m[2];
const G = { mode: 3, phase: 16, countdown: 11, killLimit: 6, me: 2, qstate: 65, winner: 7, endReason: 8, endN: 94, matchLeft: 4, locked: 20, paused: 21,
  scoreOpen: 27, wantQueue: 77, inMatch: 268, rosterN: 82, hitmark: 28, found: 87, want: 245, netState: 64, welcomed: 265, auth: 266, out: 73 };

async function boot() {
  const { instance } = await WebAssembly.instantiate(fs.readFileSync(wasmPath), {});
  const x = instance.exports;
  x.sp_init();
  let now = 1000;
  const dv = () => new DataView(x.memory.buffer);
  return {
    x, dv,
    frame(n = 1) { for (let i = 0; i < n; i++) { now += 1000 / 60; x.sp_frame(now); } },
    gi: (k) => dv().getInt32(135168 + k * 4, true),
    pf: (p, k) => dv().getFloat32(x.sp_player_addr(p) + fields[k] * 4, true),
    pi: (p, k) => dv().getInt32(x.sp_player_addr(p) + fields[k] * 4, true),
    setI: (p, k, v) => dv().setInt32(x.sp_player_addr(p) + fields[k] * 4, v, true),
    str: (p) => { const a = 172032 + p * 32; let s = ''; for (let i = 0; i < 31; i++) { const c = dv().getUint8(a + i); if (!c) break; s += String.fromCharCode(c); } return s; },
    fb: () => new Uint8Array(x.memory.buffer, x.sp_fb_addr(), 640 * 360 * 4),
    click(px, py) { x.sp_pointer(px, py); x.sp_button(0, 1); x.sp_button(0, 0); },
    key(id, down) { x.sp_key(id, down ? 1 : 0); },
    play() { this.frame(3); this.click(320, 204); x.sp_lock(1); },
  };
}
const distinctColors = (fb) => { const s = new Set(); for (let i = 0; i < fb.length; i += 4 * 211) s.add((fb[i] << 16) | (fb[i + 1] << 8) | fb[i + 2]); return s.size; };

// ---- module hygiene --------------------------------------------------------------------------------------------------
{
  const bytes = fs.readFileSync(wasmPath);
  const mod = new WebAssembly.Module(bytes);
  ok(WebAssembly.Module.imports(mod).length === 0, 'the module imports nothing (no allocator, no libc, no JS callbacks)');
  const g = await boot();
  const before = g.x.memory.buffer.byteLength;
  g.play(); g.frame(2000);
  ok(before === 8388608 && g.x.memory.buffer.byteLength === before, 'memory is a fixed 8 MB and did not grow through a long session');
}

// ---- no two globals share a slot (a collision here once made the weapon selection follow the camera pitch) ------------------
{
  const seen = new Map(); let dup = [];
  for (const f of fs.readdirSync(path.join(here, '../stdlib/shankpit460'))) {
    if (!f.endsWith('.prn') || /fields|map_data/.test(f)) continue;
    const src = fs.readFileSync(path.join(here, '../stdlib/shankpit460', f), 'utf8');
    for (const m of src.matchAll(/\(defn ((?:g|o|kat|want)-?[a-z0-9-]*) \[\] : I32 (\d+)\)/g)) {
      if (m[1] === 'g-base') continue;
      const k = +m[2];
      if (seen.has(k)) dup.push(`${m[1]}/${seen.get(k)}=${k}`); else seen.set(k, m[1]);
    }
  }
  ok(dup.length === 0, 'every engine global has its own word' + (dup.length ? ' -- collisions: ' + dup.join(', ') : ` (${seen.size} slots)`));
}

// ---- title -> play -> end -> requeue -------------------------------------------------------------------------------------------
{
  const g = await boot();
  g.frame(3);
  ok(g.x.sp_phase() === 0 && distinctColors(g.fb()) > 25, 'the title screen is drawn (not blank): ' + distinctColors(g.fb()) + ' distinct colours');
  g.click(10, 10);
  ok(g.x.sp_phase() === 0, 'a click that misses PLAY does nothing');
  g.click(320, 204);
  ok(g.x.sp_phase() === 1 && g.gi(G.mode) === 0, 'clicking PLAY starts a bot match');
  ok(g.gi(G.wantQueue) === 1 && g.x.sp_net_wanted() === 1, 'and asks the page for a matchmaking socket (SEARCHING FOR MATCH)');
  g.x.sp_lock(1);
  ok([...Array(8).keys()].every((p) => g.pi(p, 'active') === 1 && g.str(p).length > 0), 'eight players, all named: ' + [...Array(8).keys()].map((p) => g.str(p)).join(', '));
  ok([...Array(8).keys()].filter((p) => g.pi(p, 'is-bot')).length === 7 && g.pi(0, 'is-bot') === 0, 'one human and seven bots');
  const pos = new Set([...Array(8).keys()].map((p) => g.pf(p, 'x').toFixed(0) + ',' + g.pf(p, 'z').toFixed(0)));
  ok(pos.size === 8, 'everyone starts on their own spawn');
  g.frame(100);
  ok(g.gi(G.countdown) > 0, 'a GET READY countdown runs first');
  g.frame(160);
  ok(g.gi(G.countdown) === 0 && distinctColors(g.fb()) > 60, 'then the match is live and the 3D view is drawn (' + distinctColors(g.fb()) + ' colours)');
  const killLimit = g.gi(G.killLimit);
  ok(killLimit === 13, 'a bot match is first to 13 kills');
  // run the clock out: the end screen with the standings and a requeue button
  for (let i = 0; i < 40 && g.x.sp_phase() === 1; i++) g.x.sp_t_play(1000), g.frame(1);
  ok(g.x.sp_phase() === 2, 'the match ends (kill limit or the five minute clock)');
  ok(g.gi(G.endN) === 8, 'the end screen has all eight on the board');
  const rows = [...Array(g.gi(G.endN)).keys()].map((i) => g.dv().getInt32(204800 + i * 16 + 4, true));
  ok(rows.every((k, i) => i === 0 || rows[i - 1] >= k), 'sorted best first: ' + rows.join(' '));
  g.frame(40);
  const endColors = distinctColors(g.fb());
  ok(endColors > 20, 'the end screen is drawn (' + endColors + ' colours)');
  g.click(320, 261);
  ok(g.x.sp_phase() === 1 && g.gi(G.mode) === 0, 'REQUEUE (the button) starts a new bot match');
  ok(g.pi(0, 'kills') === 0 && g.gi(G.countdown) > 0, 'with fresh scores and a countdown');
  g.x.sp_t_end_match(0, 1); g.frame(40);
  g.key(13, true); g.frame(2); g.key(13, false); g.frame(2);
  ok(g.x.sp_phase() === 1, 'Enter also requeues');
  g.x.sp_t_end_match(0, 1); g.frame(40);
  g.key(27, true); g.frame(2); g.key(27, false); g.frame(2);
  ok(g.x.sp_phase() === 0 && g.gi(G.wantQueue) === 0, 'Esc on the end screen goes to the title and stops searching');
}

// ---- input -------------------------------------------------------------------------------------------------------------------------
{
  const g = await boot();
  g.play(); g.frame(260);
  for (let p = 1; p < 8; p++) g.setI(p, 'active', 0);   // nobody shoots the player while the controls are tested
  const x0 = g.pf(0, 'x'), z0 = g.pf(0, 'z');
  g.key(87, true); g.frame(90); g.key(87, false);
  ok(Math.hypot(g.pf(0, 'x') - x0, g.pf(0, 'z') - z0) > 20, 'W moves the player forward');
  g.x.sp_mouse(100, 0); g.frame(3);
  const camYaw = new DataView(g.x.memory.buffer).getFloat32(135168 + 48 * 4, true);
  g.x.sp_mouse(100, 0); g.frame(3);
  const camYaw2 = new DataView(g.x.memory.buffer).getFloat32(135168 + 48 * 4, true);
  let d = camYaw2 - camYaw; if (d > 180) d -= 360; if (d < -180) d += 360;
  ok(Math.abs(d + 15) < 0.01, `100 px of mouse is 15 degrees (0.15 per pixel, turning right lowers yaw): ${d.toFixed(3)}`);
  const camP = () => new DataView(g.x.memory.buffer).getFloat32(135168 + 49 * 4, true);
  const p0 = camP(); g.x.sp_mouse(0, -100); g.frame(3);
  ok(Math.abs(camP() - p0 - 15) < 0.01, 'mouse up looks up');
  g.x.sp_mouse(0, 100000); g.frame(3);
  ok(camP() >= -89.001, 'pitch is clamped at -89');
  for (let w = 0; w < 6; w++) { g.key(49 + w, true); g.frame(3); g.key(49 + w, false); g.frame(2); ok(g.pi(0, 'weapon') === w, `key ${w + 1} selects weapon ${w}`); }
  g.x.sp_wheel(1); g.frame(3);
  ok(g.pi(0, 'weapon') === 0, 'the wheel cycles forward (6 wraps to 1)');
  g.key(50, true); g.frame(3); g.key(50, false); g.frame(2);
  const ammo0 = g.pi(0, 'ammo1');
  g.x.sp_button(0, 1); g.frame(4); g.x.sp_button(0, 0); g.frame(2);
  ok(g.pi(0, 'ammo1') < ammo0, 'left mouse fires (a magnum round is spent)');
  g.key(82, true); g.frame(3); g.key(82, false); g.frame(80);
  ok(g.pi(0, 'ammo1') === 6, 'R reloads');
  g.key(9, true); g.frame(2);
  ok(g.gi(G.scoreOpen) === 1, 'Tab holds the scoreboard open');
  g.key(9, false); g.frame(2);
  ok(g.gi(G.scoreOpen) === 0, 'and releasing it closes it');
  // not locked: the sim waits (a bot match) and fire is ignored
  g.x.sp_lock(0); g.frame(2);
  const t0 = g.x.sp_ticks(); g.frame(30);
  ok(g.x.sp_ticks() === t0, 'unlocking the pointer pauses a bot match');
  g.x.sp_lock(1); g.frame(30);
  ok(g.x.sp_ticks() > t0, 'and locking it again resumes');
}

// ---- the wire protocol, against hand-built frames ------------------------------------------------------------------------------------
{
  const g = await boot();
  const w = (...parts) => { const b = []; for (const p of parts) { if (typeof p === 'number') b.push(p); else b.push(...p); } return Uint8Array.from(b); };
  const u16 = (v) => [v & 255, (v >> 8) & 255];
  const u32 = (v) => [v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >>> 24) & 255];
  const f32 = (v) => [...new Uint8Array(new Float32Array([v]).buffer)];
  const str = (s) => [s.length, ...Buffer.from(s)];
  const feed = (bytes) => { new Uint8Array(g.x.memory.buffer, g.x.sp_net_in_addr(), bytes.length).set(bytes); g.x.sp_net_recv(bytes.length); };
  const frames = () => {
    const base = g.x.sp_net_out_addr(), total = g.x.sp_net_out_len(), out = []; const dv = g.dv();
    for (let off = 0; off + 2 <= total;) { const len = dv.getUint8(base + off) + 256 * dv.getUint8(base + off + 1); out.push(new Uint8Array(g.x.memory.buffer.slice(base + off + 2, base + off + 2 + len))); off += 2 + len; }
    g.x.sp_net_out_clear(); return out;
  };
  g.play(); g.frame(5);
  ok(g.x.sp_net_wanted() === 1, 'the engine wants a socket while searching');
  // the page opens it: token + name in memory, then the open event
  const token = 'abc.def.ghi';
  new Uint8Array(g.x.memory.buffer, g.x.sp_token_addr(), 64).set(Buffer.from(token));
  g.x.sp_token_set(token.length);
  new Uint8Array(g.x.memory.buffer, g.x.sp_name_addr(), 8).set(Buffer.from('Wren'));
  g.x.sp_name_set(4);
  g.x.sp_net_socket(2);
  let out = frames();
  const hello = out[0];
  ok(hello && hello[0] === 1 && hello[1] === 1 && hello[2] + 256 * hello[3] === token.length && Buffer.from(hello.slice(4, 4 + token.length)).toString() === token, 'HELLO carries the version, the token and ...');
  ok(hello[4 + token.length] === 4 && Buffer.from(hello.slice(5 + token.length, 9 + token.length)).toString() === 'Wren', '... the display name');
  ok(out.some((f) => f[0] === 19), 'and a PING follows');
  ok(g.pi(0, 'active') === 1 && g.str(0) === 'Wren', 'a signed-in name replaces "You" in the bot match');
  feed(w(0x81, 1, 1, str('Wren'), u16(3)));
  ok(g.gi(G.welcomed) === 1 && g.gi(G.auth) === 1, 'WELCOME: signed in');
  out = frames();
  ok(out.length === 1 && out[0][0] === 2, 'once welcomed the engine sends QUEUE');
  feed(w(0x82, 1, 2, u16(9), u16(12)));
  ok(g.gi(G.qstate) === 1 && g.gi(68) === 9, 'STATUS: searching, 9 online');

  // MATCH: slot 2 of 3, kill limit 16, 360 s, countdown phase, 3 s to start, roster of 3 with spawn points
  const roster = [0, 1, 2].map((s) => [s, s + 3, ...str(['Ada', 'Bram', 'Wren'][s]), ...f32(-300 + s * 20), ...f32(6), ...f32(-330), ...f32(180)]);
  feed(w(0x83, u32(7), 2, 16, u16(360), 1, u16(3000), u32(360000), 3, ...roster));
  ok(g.gi(G.mode) === 1 && g.gi(G.inMatch) === 1 && g.gi(G.me) === 2, 'MATCH: a human match, local slot 2');
  ok(g.gi(G.killLimit) === 16 && g.gi(G.rosterN) === 3, 'the kill limit and roster size come from the server');
  ok(g.str(0) === 'Ada' && g.str(1) === 'Bram' && g.str(2) === 'Wren', 'roster names land in their slots');
  ok(g.pi(0, 'net-slot') === 1 && g.pi(2, 'net-slot') === 0, 'other humans are remote; the local player simulates');
  ok(g.pi(4, 'active') === 0, 'bots are gone');
  ok(Math.abs(g.gi(G.countdown) - 187) <= 1, 'a 3 s countdown (' + g.gi(G.countdown) + ' ticks)');
  ok(g.gi(G.found) > 0, 'with a MATCH FOUND interstitial');
  feed(w(0x85, 5, u32(360000)));
  ok(g.gi(G.countdown) === 0, 'LIVE clears the countdown');
  feed(w(0x85, 3, 2, f32(-250), f32(6), f32(-330), f32(180)));
  ok(Math.abs(g.pf(2, 'x') + 250) < 0.01, 'RESPAWN places the local player where the server says');

  // SNAP: slot 0 at (10, 6, -100) moving, health 70 / shield 0, 3 kills
  const snapEntry = (slot, x, z, hp, sh, k, d, st) => [slot, ...f32(x), ...f32(6), ...f32(z), ...f32(0), ...f32(0), 2, 0, hp, sh, k, d, st];
  feed(w(0x84, u32(300000), 3, snapEntry(0, 10, -100, 70, 0, 3, 1, 0), snapEntry(1, 30, -90, 100, 100, 0, 0, 0), snapEntry(2, -250, -330, 55, 20, 1, 2, 0)));
  ok(g.pi(0, 'health') === 70 && g.pi(0, 'shield') === 0 && g.pi(0, 'kills') === 3, 'SNAP: server health, shield and kills');
  ok(g.pi(2, 'health') === 55 && g.pi(2, 'shield') === 20 && g.pi(2, 'deaths') === 2, 'and the local player\'s own vitals come from the server too');
  ok(Math.abs(g.gi(G.matchLeft) - 18750) <= 2, 'the match clock follows the server (' + g.gi(G.matchLeft) + ' ticks left)');
  g.frame(30);
  ok(Math.abs(g.pf(0, 'x') - 10) < 0.5 && Math.abs(g.pf(0, 'z') + 100) < 0.5, 'a remote player is drawn at its snapshot position');
  frames();
  g.frame(5);
  out = frames();
  const st = out.filter((f) => f[0] === 16);
  ok(st.length >= 1 && st[0].length === 25, 'the local player\'s STATE goes out (25 bytes): ' + st.length + ' in 5 frames');

  // EVENTS: damage to the local player, a kill, a remote shot
  feed(w(0x85, 1, 2, 0, 30, 25, 0, 0, 2));
  ok(g.pi(2, 'health') === 25 && g.pi(2, 'flash') > 0, 'DAMAGE updates the victim and flashes');
  feed(w(0x85, 2, 0, 2, 2));
  ok(g.pi(2, 'state') === 1, 'KILL: the local player is down');
  ok(g.pi(0, 'state') === 0, 'and the killer is not');
  feed(w(0x85, 4, 0, 2, f32(10), f32(8.6), f32(-100), f32(0), f32(0), f32(1)));
  ok(g.pi(0, 'is-shooting') > 0, 'FIRE: a remote shot raises the shooter\'s muzzle');
  // END: slot 0 wins 16-9, local player 9-16
  feed(w(0x87, 1, 0, 3, 0, 16, 9, 1, 3, 4, 2, 9, 16));
  ok(g.x.sp_phase() === 2 && g.gi(G.winner) === 0 && g.gi(G.endReason) === 1, 'END: the end screen, slot 0 the winner, reason kills');
  ok(g.pi(0, 'kills') === 16 && g.pi(2, 'deaths') === 16 && g.gi(G.endN) === 3, 'final standings are the server\'s');
  ok(g.gi(G.inMatch) === 0, 'the engine is no longer in a match');
  // socket drop mid-match = a closed match, not a hang
  const g2 = await boot();
  g2.play(); g2.frame(5);
  new Uint8Array(g2.x.memory.buffer, g2.x.sp_net_in_addr(), 1).set([0]);
  g2.x.sp_net_socket(2);
  const send2 = (bytes) => { new Uint8Array(g2.x.memory.buffer, g2.x.sp_net_in_addr(), bytes.length).set(bytes); g2.x.sp_net_recv(bytes.length); };
  send2(w(0x81, 1, 0, str('x'), u16(1)));
  send2(w(0x83, u32(1), 0, 16, u16(360), 2, u16(0), u32(360000), 2, [0, 0, ...str('a'), ...f32(0), ...f32(6), ...f32(0), ...f32(0)], [1, 1, ...str('b'), ...f32(20), ...f32(6), ...f32(0), ...f32(0)]));
  ok(g2.gi(G.mode) === 1 && g2.gi(G.countdown) === 0, 'a late arrival joins a live match with no countdown');
  g2.x.sp_net_socket(3);
  ok(g2.x.sp_phase() === 2 && g2.gi(G.endReason) === 3, 'losing the socket mid-match ends it (reason: closed) rather than hanging');
  g2.frame(40);
  g2.click(320, 261);
  ok(g2.x.sp_phase() === 1 && g2.gi(G.mode) === 0, 'and REQUEUE goes back to a bot match');
  // malformed input is ignored
  const g3 = await boot(); g3.play(); g3.frame(5);
  for (const bad of [[], [0x83], [0x83, 1, 2], [0x84, 5], [0x85, 3], [0x87, 1], [0xee], [0x81]]) { try { new Uint8Array(g3.x.memory.buffer, g3.x.sp_net_in_addr(), bad.length).set(bad); g3.x.sp_net_recv(bad.length); } catch (e) { ok(false, 'a malformed frame trapped: ' + e); } }
  ok(g3.x.sp_phase() === 1 && g3.gi(G.mode) === 0, 'truncated and unknown frames are ignored');
}

// ---- determinism ------------------------------------------------------------------------------------------------------------------------
{
  const run = async () => {
    const g = await boot(); g.x.sp_t_bot_arena(2); g.x.sp_t_play(3000);
    return [...Array(8).keys()].map((p) => [g.pf(p, 'x'), g.pf(p, 'z'), g.pi(p, 'kills'), g.pi(p, 'deaths'), g.pi(p, 'health')].join(':')).join('|');
  };
  const a = await run(), b = await run();
  ok(a === b, 'the same seed plays out the same bot match, tick for tick');
}

// ---- cosmetics never touch the simulation's dice ---------------------------------------------------------------------------------------------
{
  const run = async (poke) => {
    const g = await boot(); g.x.sp_t_bot_arena(2);
    if (poke) for (let i = 0; i < 40; i++) { g.x.sp_t_poke(134, i * 7919); g.x.sp_t_play(50); } else g.x.sp_t_play(2000);
    return g.gi(1);
  };
  ok(await run(true) === await run(false), 'reseeding the cosmetic generator leaves the simulation\'s generator untouched');
}

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
