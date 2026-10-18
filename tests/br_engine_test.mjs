// tests/br_engine_test.mjs -- the BEDROCK RACERS wasm as a client: module hygiene, the page-host contract, screens and flow,
// input (keyboard, mouse, gamepad), the camera, the lighting, determinism (drawing only READS the sim) and the frame budget.
//   node tests/br_engine_test.mjs <bedrockracers.wasm>
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from './br_harness.mjs';
const W = process.argv[2];
if (!W) { console.error('usage: br_engine_test.mjs <wasm>'); process.exit(2); }
const here = path.dirname(fileURLToPath(import.meta.url));
const web = path.join(here, '../stdlib/bedrockracers/web');
let pass = 0, fail = 0;
const ok = (c, m) => { c ? pass++ : fail++; console.log((c ? 'PASS: ' : 'FAIL: ') + m); };

// the global word indices, by name, from every engine file (the renderer's files own words too)
const G = {};
for (const f of fs.readdirSync(web)) {
  if (!/^br_.*\.prn$/.test(f) || f === 'br_fields.prn') continue;
  for (const m of fs.readFileSync(path.join(web, f), 'utf8').matchAll(/\(defn g-([a-z0-9-]+) \[\] : I32 (\d+)\)/g)) G[m[1]] = +m[2];
}
const KEY = { enter: 13, esc: 27, space: 32, shift: 16, tab: 9, left: 37, up: 38, right: 39, down: 40,
  a: 65, b: 66, c: 67, d: 68, e: 69, f: 70, q: 81, s: 83, v: 86, w: 87, backspace: 8 };

// a booted game with a frame clock: one br_frame = 16 ms exactly, so one frame is one sim tick
async function game() {
  const g = await boot(W);
  let now = 1000;
  g.frame = (n = 1) => { for (let i = 0; i < n; i++) { now += 16; g.x.br_frame(now); } };
  g.tap = (id, frames = 2) => { g.x.br_key(id, 1); g.frame(frames); g.x.br_key(id, 0); g.frame(1); };
  g.hold = (id) => g.x.br_key(id, 1);
  g.release = (id) => g.x.br_key(id, 0);
  g.gi = (name) => g.g(G[name]);
  g.gfl = (name) => g.gf(G[name]);
  g.fb = () => new Uint8Array(g.x.memory.buffer, g.x.br_fb_addr(), 640 * 360 * 4);
  return g;
}
const distinct = (fb) => { const s = new Set(); for (let i = 0; i < fb.length; i += 4 * 97) s.add((fb[i] << 16) | (fb[i + 1] << 8) | fb[i + 2]); return s.size; };
const luma = (fb, x0, y0, x1, y1) => { let t = 0, n = 0; for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) { const i = (y * 640 + x) * 4; t += fb[i] * 0.3 + fb[i + 1] * 0.59 + fb[i + 2] * 0.11; n++; } return t / n; };
const same = (a, b) => Buffer.compare(a, b) === 0;
// the sim-owned bytes: all twelve car records and the ball record
const simState = (g) => Buffer.concat([Buffer.from(new Uint8Array(g.x.memory.buffer, g.x.br_car_addr(0), 12 * 512)),
  Buffer.from(new Uint8Array(g.x.memory.buffer, g.x.br_ball_addr(), 256))]);

// ---- module hygiene ----------------------------------------------------------------------------------------------------------
{
  const mod = new WebAssembly.Module(fs.readFileSync(W));
  ok(WebAssembly.Module.imports(mod).length === 0, 'the module imports nothing (no allocator, no libc, no JS callbacks)');
  const g = await game();
  const before = g.x.memory.buffer.byteLength;
  g.tap(KEY.b); g.frame(4000);
  ok(before === 8388608 && g.x.memory.buffer.byteLength === before, 'memory is a fixed 8 MB and did not grow through 4000 frames of a bot match');
  const exported = new Set(WebAssembly.Module.exports(mod).map(e => e.name));
  const host = fs.readFileSync(path.join(web, 'br_host.prn'), 'utf8');
  const used = [...new Set([...host.matchAll(/\(js\/call wasm "(br_[a-z0-9_]+)"/g)].map(m => m[1]))];
  const missing = used.filter(n => !exported.has(n));
  ok(used.length >= 12 && missing.length === 0, `every wasm export the page host calls exists (${used.length} checked${missing.length ? ', missing: ' + missing.join(' ') : ''})`);
}

// ---- no two globals share a slot -----------------------------------------------------------------------------------------------
{
  const seen = new Map(); const dup = [];
  for (const f of fs.readdirSync(web)) {
    if (!/^br_.*\.prn$/.test(f) || f === 'br_fields.prn') continue;
    for (const m of fs.readFileSync(path.join(web, f), 'utf8').matchAll(/\(defn (g-[a-z0-9-]+) \[\] : I32 (\d+)\)/g)) {
      if (m[1] === 'g-base') continue;
      const k = +m[2];
      if (seen.has(k)) dup.push(`${m[1]}/${seen.get(k)}=${k}`); else seen.set(k, m[1]);
    }
  }
  ok(dup.length === 0, `no two G words share a slot (${seen.size} claimed${dup.length ? '; clashes: ' + dup.join(' ') : ''})`);
}

// ---- screens and flow ------------------------------------------------------------------------------------------------------------
{
  const g = await game(); g.frame(30);
  ok(g.x.br_phase() === 0 && g.x.br_status() === 0 && g.gi('cam-mode') === 3, 'the title is up, over the orbiting empty arena');
  ok(distinct(g.fb()) > 40, `the title frame is a picture, not a blank (${distinct(g.fb())} sampled colours)`);
  g.tap(KEY.enter);
  ok(g.x.br_phase() === 1 && g.x.br_status() === 1 && g.gi('roster') === 1, 'Enter starts a match with the person in slot 0');
  ok(g.gi('cam-mode') === 0 && g.gi('countdown') > 0, 'the chase camera is on and the kickoff countdown runs');
  const c0 = g.gi('countdown'); g.frame(60);
  ok(c0 - g.gi('countdown') === 60 && g.gi('tick') === 0, 'one display frame at 16 ms is one sim tick: the kickoff countdown falls by 60 in 60 frames');
  g.frame(g.gi('countdown') + 2);
  const t0 = g.gi('tick'); g.frame(60);
  ok(g.gi('countdown') === 0 && g.gi('tick') - t0 === 60, 'and once the whistle has gone the match clock gains 60 ticks in 60 frames');
  g.tap(KEY.esc);
  ok(g.x.br_status() === 3, 'Esc pauses a person\'s match');
  const tp = g.gi('tick'); g.frame(30);
  ok(g.gi('tick') === tp, 'the sim does not advance while paused');
  ok(distinct(g.fb()) > 40, 'the paused frame still draws');
  { const a = Buffer.from(g.fb()); g.frame(10); ok(Buffer.compare(a, Buffer.from(g.fb())) === 0, 'and a paused frame is perfectly still: not one pixel changes in 10 frames (the ball does not keep rolling)'); }
  g.tap(KEY.esc);
  ok(g.x.br_status() === 1, 'Esc again resumes');
  g.tap(KEY.v); ok(g.gi('cam-mode') === 1, 'V switches to the ball cam'); g.tap(KEY.v); ok(g.gi('cam-mode') === 0, 'V switches back to the chase cam');
  g.x.br_pause(); ok(g.x.br_status() === 3, 'losing focus (br_pause) pauses a person\'s match');
  g.x.br_pause(); g.tap(KEY.esc); ok(g.x.br_status() === 1, 'and Esc brings it back');
  g.x.br_t_end();
  g.frame(3); ok(g.x.br_phase() === 2 && g.x.br_status() === 4, 'a finished match shows the end screen');
  ok(distinct(g.fb()) > 30, 'the end screen draws');
  g.frame(40); g.tap(KEY.enter);
  ok(g.x.br_phase() === 0, 'Enter on the end screen goes back to the title');
}
{
  const g = await game(); g.frame(10); g.tap(KEY.b);
  ok(g.x.br_phase() === 1 && g.x.br_status() === 2 && g.gi('roster') === 0, 'B at the title watches twelve bots');
  g.frame(400);
  ok(g.gi('cam-mode') === 2, 'a bot-only match uses the spectator camera');
  ok(g.gi('tick') > 150 && g.gi('countdown') === 0, 'and the sim runs without a person in it (the match is under way)');
  g.x.br_pause(); ok(g.x.br_status() === 2, 'a bot match does not pause when focus is lost (nobody to wait for)');
  ok(distinct(g.fb()) > 60, `the spectator view draws (${distinct(g.fb())} sampled colours)`);
}

// ---- pausing a car that is moving fast holds the view exactly where it was (the chase camera lags a fast car and used to glide for 3 s) ----
{
  const g = await game(); g.frame(5); g.tap(KEY.enter); g.frame(260);
  g.hold(KEY.w); g.hold(KEY.shift); g.frame(90); g.release(KEY.w); g.release(KEY.shift);
  g.tap(KEY.esc, 1);
  const sp = g.x.br_status() === 3 ? Math.hypot(...g.vel(0)) : -1;
  const a = Buffer.from(g.fb()); g.frame(60);
  ok(sp > 150 && Buffer.compare(a, Buffer.from(g.fb())) === 0, `a match paused at ${sp.toFixed(0)} units/s is frozen at once: not one pixel changes in the next 60 frames`);
}

// ---- a paused HUD is still too: a low tank blinks and the magnet ring pulses, and neither may while paused ------------------------------------------
{
  const g = await game(); g.frame(5); g.tap(KEY.enter); g.frame(260);
  g.setCar(0, 'air', 5); g.hold(KEY.f); g.frame(40);                 // a nearly empty tank (blinking) and the magnet held (pulsing)
  g.tap(KEY.esc, 1); const a = Buffer.from(g.fb()); g.frame(50);
  ok(g.x.br_status() === 3 && Buffer.compare(a, Buffer.from(g.fb())) === 0, 'with a blinking air bar and a pulsing magnet ring on screen, pausing still freezes every pixel');
  g.release(KEY.f);
}

// ---- input: keyboard, mouse, gamepad ------------------------------------------------------------------------------------------------------
{
  const g = await game(); g.frame(5); g.tap(KEY.enter); g.frame(3);
  const cmd = () => ({ thr: g.gfl('cmd-thr'), steer: g.gfl('cmd-steer'), pitch: g.gfl('cmd-pitch'), roll: g.gfl('cmd-roll'), strafe: g.gfl('cmd-strafe'),
    lift: g.gfl('cmd-lift'), boost: g.gi('cmd-boost'), magnet: g.gi('cmd-magnet'), fans: g.gi('cmd-fans-off'), tab: g.gi('score-open') });
  const keys = (list, frames = 1) => { list.forEach(k => g.hold(k)); g.frame(frames); const c = cmd(); list.forEach(k => g.release(k)); g.frame(2); return c; };
  ok(keys([KEY.w]).thr === 1 && keys([KEY.s]).thr === -1, 'W is throttle, S is brake');
  ok(keys([KEY.d]).steer === 1 && keys([KEY.a]).steer === -1, 'D steers right, A steers left');
  ok(keys([KEY.down]).pitch === 1 && keys([KEY.up]).pitch === -1, 'the down arrow is pitch +1 (a flight stick pulled back), the up arrow pitch -1');
  ok(keys([KEY.right]).roll === 1 && keys([KEY.left]).roll === -1, 'the right arrow is roll +1, the left arrow roll -1');
  ok(keys([KEY.e]).strafe === 1 && keys([KEY.q]).strafe === -1, 'E and Q strafe');
  ok(keys([KEY.space], 12).lift > 0.5, 'Space ramps the lift up while held');
  ok(keys([KEY.shift]).boost === 1 && keys([KEY.c]).fans === 1 && keys([KEY.tab]).tab === 1, 'Shift boosts, C turns the fans off, Tab opens the scores');
  ok(keys([KEY.f]).magnet === 1, 'F holds the magnet');
  g.x.br_button(2, 1); g.frame(1); const mg = cmd().magnet; g.x.br_button(2, 0); g.frame(2);
  ok(mg === 1 && cmd().magnet === 0, 'the right mouse button holds the magnet, and letting go lets go');
  const idle = cmd();
  ok(idle.thr === 0 && idle.steer === 0 && idle.magnet === 0 && idle.boost === 0, 'with nothing held every command is zero');
  g.hold(KEY.w); g.hold(KEY.d); g.frame(2); g.x.br_blur(); g.frame(2);
  ok(cmd().thr === 0 && cmd().steer === 0, 'br_blur lets go of every held key');
  // a gamepad: LS steer / pitch, RT throttle, LT brake, A lift, B magnet, RB boost, LB fans off, RS roll
  const pad = (lx, ly, rx, ry, lt, rt, mask) => { g.x.br_pad(0, lx, ly, rx, ry, lt, rt, mask); g.frame(2); const c = cmd(); g.x.br_pad_gone(0); g.frame(2); return c; };
  ok(pad(0, 0, 0, 0, 0, 1, 0).thr === 1 && pad(0, 0, 0, 0, 1, 0, 0).thr === -1, 'on a pad RT is throttle and LT is brake');
  ok(pad(1, 0, 0, 0, 0, 0, 0).steer > 0.9 && pad(0, 1, 0, 0, 0, 0, 0).pitch > 0.9, 'the left stick steers and pitches');
  ok(pad(0, 0, 1, 0, 0, 0, 0).roll > 0.9, 'the right stick rolls');
  ok(pad(0, 0, 0, 0, 0, 0, 1 << 1).magnet === 1 && pad(0, 0, 0, 0, 0, 0, 1 << 5).boost === 1 && pad(0, 0, 0, 0, 0, 0, 1 << 4).fans === 1,
    'B is the magnet, RB boosts, LB turns the fans off');
  ok(pad(0.1, 0, 0, 0, 0, 0, 0).steer === 0, 'a stick inside the dead zone reads zero');
  g.x.br_pad(0, 0, 0, 0, 0, 0, 0, 1 << 9); g.frame(2); g.x.br_pad(0, 0, 0, 0, 0, 0, 0, 0); g.frame(2);
  ok(g.x.br_status() === 3, 'pad Start pauses');
  g.x.br_pad_gone(0);
}

// ---- the chase camera through a wall, the ceiling and a kickoff ----------------------------------------------------------------------------------
{
  const g = await game(); g.frame(5); g.tap(KEY.enter); g.frame(260);
  const basis = (s, u, f) => { const r = [f[1] * u[2] - f[2] * u[1], f[2] * u[0] - f[0] * u[2], f[0] * u[1] - f[1] * u[0]];
    for (const [k, v] of Object.entries({ ux: u[0], uy: u[1], uz: u[2], fx: f[0], fy: f[1], fz: f[2], rx: r[0], ry: r[1], rz: r[2] })) g.setCar(s, k, v); };
  const eye = () => ['x', 'y', 'z'].map(k => g.gfl('cam-' + k)), camUp = () => ['ux', 'uy', 'uz'].map(k => g.gfl('cam-' + k));
  const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  g.x.br_t_ball(0, 40, 380, 0, 0, 0);
  // a car dropped onto the ceiling (a half-turn from where the camera was): the camera snaps onto it, inside the room
  g.x.br_t_place(0, 0, 236, -150); basis(0, [0, -1, 0], [0, 0, 1]); g.frame(3);
  ok(dot(camUp(), [0, -1, 0]) > 0.9 && g.x.br_t_free(...eye()) >= 4.9, `a car put on the ceiling is chased from inside the room with the world turned over (camera up . car up ${dot(camUp(), [0, -1, 0]).toFixed(2)}, eye ${g.x.br_t_free(...eye()).toFixed(1)} from a surface)`);
  // a real climb: up the wall and over onto the ceiling, W held
  g.x.br_t_place(0, -516, 40, 0); basis(0, [1, 0, 0], [0, 1, 0]); g.frame(5); g.hold(KEY.w);
  let minFree = 1e9, minDot = 1, ceiling = false;
  for (let i = 0; i < 260; i++) { g.frame(1); minFree = Math.min(minFree, g.x.br_t_free(...eye())); if (i > 5) minDot = Math.min(minDot, dot(camUp(), g.up(0))); if (g.up(0)[1] < -0.8) ceiling = true; }
  g.release(KEY.w);
  ok(ceiling, 'the car really drove from the wall onto the ceiling');
  ok(minFree >= 4.9, `through the whole climb the eye never gets closer than 5 units to a surface (${minFree.toFixed(1)})`);
  ok(minDot > 0.8, `and the camera's up follows the car's up the whole way (worst camera up . car up ${minDot.toFixed(2)})`);
  // carrying the ball: it sits dead ahead of the nose, so the camera rises and backs off to see over it
  g.x.br_t_place(0, 0, 4, -200); basis(0, [0, 1, 0], [0, 0, 1]); g.x.br_t_ball(0, 300, 380, 0, 0, 0); g.frame(60);
  const lift = () => { const e = eye(), p = g.pos(0), u = g.up(0); return (e[0] - p[0]) * u[0] + (e[1] - p[1]) * u[1] + (e[2] - p[2]) * u[2]; };
  const free = lift();
  const c = [g.pos(0)[0] + g.fwd(0)[0] * 17, 13, g.pos(0)[2] + g.fwd(0)[2] * 17]; g.x.br_t_ball(c[0], c[1], c[2], 0, 0, 0);
  g.hold(KEY.f); g.frame(90); const carrying = lift(); const held = g.ball('holder');
  g.release(KEY.f); g.x.br_t_ball(0, 300, 380, 0, 0, 0); g.frame(90); const after = lift();
  ok(held === 0 && carrying > free + 8, `a carried ball raises the chase camera to see over it (${free.toFixed(1)} -> ${carrying.toFixed(1)} above the car's roof line) and it settles back when the ball is gone (${after.toFixed(1)})`);
  // a goal scored while the person is on the ceiling: the kickoff stands the car up and the camera must come with it
  g.x.br_t_place(0, 0, 236, -150); basis(0, [0, -1, 0], [0, 0, 1]); g.frame(40);
  g.x.br_t_score(1, -1, 3); g.frame(400);
  ok(g.up(0)[1] > 0.9 && camUp()[1] > 0.9 && g.x.br_t_free(...eye()) >= 4.9, 'after a goal from the ceiling the kickoff puts the car upright and the camera is upright with it (it used to stay upside down)');
}

// ---- the title's picks and clicks, the pause menu, the results screen -----------------------------------------------------------------------------
{
  const g = await game(); g.frame(30);
  const click = (x, y) => { g.x.br_pointer(x, y); g.x.br_button(0, 1); g.frame(2); g.x.br_button(0, 0); g.frame(2); };
  ok(g.gi('pick-team') === 0 && g.gi('pick-level') === 2, 'the title starts on the amber team and NORMAL bots');
  g.tap(KEY.right); g.tap(KEY.right); ok(g.gi('pick-team') === 2, 'Right twice picks the third team');
  g.tap(KEY.left); g.tap(KEY.a); ok(g.gi('pick-team') === 0, 'Left and A step back');
  g.tap(KEY.left); ok(g.gi('pick-team') === 3, 'Left from the first team wraps to the last');
  g.tap(KEY.d); ok(g.gi('pick-team') === 0, 'and D from the last wraps to the first');
  g.tap(KEY.up); g.tap(KEY.up); ok(g.gi('pick-level') === 3, 'Up raises the bots\' skill to HARD and stops there');
  g.tap(KEY.down); g.tap(KEY.s); g.tap(KEY.s); ok(g.gi('pick-level') === 1, 'Down and S lower it to EASY and stop there');
  click(400, 190); ok(g.gi('pick-team') === 3, 'a click on the coral swatch picks coral');
  click(400, 246); ok(g.gi('pick-level') === 3, 'a click on HARD picks HARD');
  ok(g.x.br_phase() === 0, 'picking starts nothing');
  click(250, 284);
  ok(g.x.br_phase() === 1 && g.gi('roster') === (1 << 9) && g.gi('me') === 9 && g.gi('view-slot') === 9 && g.gi('bot-level') === 3,
    'a click on PLAY starts a match with the person in coral\'s first car (slot 9) against HARD bots');
  ok(g.car(9, 'is-bot') === 0 && g.car(0, 'is-bot') === 1 && g.car(11, 'is-bot') === 1, 'only slot 9 is a person');
  ok(g.car(9, 'team') === 3 && g.gi('cam-mode') === 0, 'slot 9 is on team 3 and the chase camera follows it');
  g.frame(g.gi('countdown') + 5); g.hold(KEY.w); g.frame(12);
  const driven = g.car(9, 'in-thr'), sp9 = Math.hypot(...g.vel(9));
  g.release(KEY.w); g.frame(4);
  ok(driven > 0.9 && sp9 > 5 && g.car(9, 'in-thr') === 0, `the keys drive slot 9: W gives it full throttle (speed ${sp9.toFixed(0)}) and letting go gives none`);
  g.tap(KEY.esc); ok(g.x.br_status() === 3, 'Esc pauses');
  click(370, 180); ok(g.x.br_phase() === 0, 'LEAVE on the pause card goes back to the title');
  g.tap(KEY.enter); g.tap(KEY.esc); click(260, 180);
  ok(g.x.br_status() === 1, 'RESUME on the pause card plays on');
  g.tap(KEY.esc); g.tap(KEY.backspace); ok(g.x.br_phase() === 0, 'Backspace on the pause card leaves the match');
  g.tap(KEY.enter); g.frame(100); g.x.br_t_score(9, -1, 0); g.frame(3); g.x.br_t_end();
  g.frame(10); g.tap(82); ok(g.x.br_phase() === 2, 'R is ignored for the first half second of the results (a stray key does not skip them)');
  g.frame(40);
  click(350, 312);
  ok(g.x.br_phase() === 1 && g.gi('roster') === (1 << 9) && g.gi('countdown') > 0 && g.gi('score') === 0, 'REMATCH starts the same teams again with the scores back at zero');
  g.x.br_t_end(); g.frame(60); click(260, 312);
  ok(g.x.br_phase() === 0, 'TITLE on the results screen goes back to the title');
  g.tap(66);
  g.hold(KEY.tab); g.frame(2); ok(g.gi('score-open') === 1, 'Tab opens the scoreboard for a spectator too');
  g.release(KEY.tab); g.frame(2); ok(g.gi('score-open') === 0, 'and letting go closes it');
}

// ---- the whistle: GO is up for under a second once the kickoff count runs out ------------------------------------------------------------------------------
{
  const g = await game(); g.frame(5); g.tap(KEY.enter); g.frame(g.gi('countdown') + 3);
  const amberIn = () => { const a = g.fb(); let n = 0; for (let y = 125; y < 200; y++) for (let x = 280; x < 360; x++) { const i = (y * 640 + x) * 4; if (a[i] > 190 && a[i + 1] > 130 && a[i + 1] < 215 && a[i + 2] < 150) n++; } return n; };
  const during = amberIn(); g.frame(120); const after = amberIn();
  ok(during > 100 && after < 20, `GO is drawn in the team's colour as the count runs out (${during} px) and is gone two seconds later (${after} px)`);
}

// ---- the HUD draws what the sim says ----------------------------------------------------------------------------------------------------------------
{
  const g = await game(); g.frame(5); g.tap(KEY.enter); g.frame(300);
  const count = (x0, y0, x1, y1, pred) => { const a = g.fb(); let n = 0; for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) { const i = (y * 640 + x) * 4; if (pred(a[i], a[i + 1], a[i + 2])) n++; } return n; };
  const amber = (r, gg, b) => r > 190 && gg > 130 && gg < 215 && b < 150;
  const red = (r, gg, b) => r > 200 && gg < 140 && b < 130;
  const teal = (r, gg, b) => r < 130 && gg > 150 && b > 140;
  const flameC = (r, gg, b) => r > 220 && gg > 110 && gg < 200 && b < 110;
  for (const [k, v] of Object.entries({ ux: 0, uy: 1, uz: 0, fx: 0, fy: 0, fz: 1, rx: -1, ry: 0, rz: 0 })) g.setCar(0, k, v);
  g.x.br_t_place(0, 0, 4, -200); for (const [k, v] of Object.entries({ ux: 0, uy: 1, uz: 0, fx: 0, fy: 0, fz: 1, rx: -1, ry: 0, rz: 0 })) g.setCar(0, k, v);
  g.x.br_t_ball(0, 300, 380, 0, 0, 0); g.frame(10);
  ok(count(592, 304, 632, 344, amber) === 0, 'the magnet ring is dim and grey while the magnet is off');
  g.hold(KEY.f); g.frame(4); const pulling = count(592, 304, 632, 344, amber);
  ok(pulling > 40, `it lights in the team colour while pulling (${pulling} px)`);
  g.release(KEY.f); g.frame(2);
  const cx = g.pos(0)[0] + g.fwd(0)[0] * 17, cz = g.pos(0)[2] + g.fwd(0)[2] * 17; g.x.br_t_ball(cx, 13, cz, 0, 0, 0);
  g.hold(KEY.f); g.frame(40); const holding = count(592, 304, 632, 344, amber);
  ok(g.ball('holder') === 0 && holding > pulling, `and fills with the carry clock when the ball is held (${holding} px)`);
  g.release(KEY.f); g.frame(2);
  ok(count(42, 343, 138, 348, teal) > 400, 'a full tank draws a full teal air bar');
  g.setCar(0, 'air', 10); let seen = 0; for (let i = 0; i < 30 && !seen; i++) { g.frame(1); seen = count(42, 343, 138, 348, red); }
  ok(seen > 15 && seen < 120, `a tank at 10% draws a short red bar (${seen} px)`);
  g.setCar(0, 'air', 100); g.frame(2);
  g.x.br_t_ignite(0); g.frame(60);
  ok(count(538, 300, 628, 310, flameC) > 200 && count(100, 60, 540, 90, flameC) > 200, 'ON FIRE draws the flame bar and the banner');
  g.x.br_t_score(1, -1, 3); g.frame(30);
  ok(count(220, 110, 420, 160, amber) > 400, 'a goal draws the big GOAL banner in the scorer\'s colour');
  g.frame(400);                                       // the goal's kickoff has put the cars back; stage the car again
  g.x.br_t_place(0, 0, 4, -200); for (const [k, v] of Object.entries({ ux: 0, uy: 1, uz: 0, fx: 0, fy: 0, fz: 1, rx: -1, ry: 0, rz: 0 })) g.setCar(0, k, v);
  g.x.br_t_ball(-300, 40, -380, 0, 0, 0); g.frame(30);
  const arrow = count(560, 80, 640, 280, (r, gg, b) => r > 200 && gg > 195 && b > 180);
  ok(arrow > 8, `a ball behind the car gets an arrow at the screen edge (${arrow} px)`);
}

// ---- what pitch and roll do to a car in the air (the sign the keys and the stick feed) ---------------------------------------------------------------
{
  const aloft = async (pitch, roll) => {
    const g = await boot(W); g.x.br_t_ball(0, 120, 480, 0, 0, 0); g.x.br_t_place(0, 0, 150, 0);
    for (const [k, v] of Object.entries({ ux: 0, uy: 1, uz: 0, fx: 0, fy: 0, fz: 1, rx: -1, ry: 0, rz: 0 })) g.setCar(0, k, v);
    for (let t = 0; t < 25; t++) { g.x.br_t_cmd(0, 0, 0, pitch, roll, 0, 0, 0, 0, 0); g.x.br_t_step(1); }
    return { f: g.fwd(0), u: g.up(0) };
  };
  const up = await aloft(1, 0), down = await aloft(-1, 0), rr = await aloft(0, 1), rl = await aloft(0, -1);
  ok(up.f[1] > 0.5 && down.f[1] < -0.5, 'pitch +1 raises the nose and pitch -1 drops it (so the down arrow, or the stick pulled back, pulls the nose up)');
  ok(rr.u[0] < -0.5 && rl.u[0] > 0.5, 'roll +1 tips the roof toward the car\'s right (x < 0 for a car facing +z) and roll -1 toward its left');
}

// ---- drawing only READS the sim -----------------------------------------------------------------------------------------------------------
{
  const N = 700;
  const a = await game(); a.frame(3); a.x.br_t_start(0, 5); for (let i = 0; i < N; i++) a.frame(1);    // drawn every tick
  const b = await game(); b.frame(3); b.x.br_t_start(0, 5); b.x.br_t_play(N);                          // never drawn
  ok(a.gi('tick') === b.gi('tick') && a.gi('tick') > 400 && a.gi('countdown') === 0, `both runs are at match tick ${a.gi('tick')} after ${N} sim ticks`);
  ok(same(simState(a), simState(b)), 'a match that is drawn every tick ends in the same car and ball bytes as one that is never drawn');
  const before = simState(a); for (let i = 0; i < 6; i++) a.x.br_t_render(i / 6);
  ok(same(before, simState(a)), 'rendering a frame does not touch a car or the ball');
}

// ---- light ----------------------------------------------------------------------------------------------------------------------------------
{
  const g = await game(); g.frame(5); g.tap(KEY.enter); g.frame(260);
  const calm = Uint8Array.from(g.fb()); const lumCalm = luma(calm, 200, 200, 440, 330);
  g.x.br_t_ignite(0); g.x.br_t_fx(40); g.x.br_t_render(1);
  const hot = Uint8Array.from(g.fb()); const lumHot = luma(hot, 200, 200, 440, 330);
  ok(g.x.br_light_count() > 0 && g.x.br_drama() > 0.2, `a car on fire puts lights in the table (${g.x.br_light_count()}) and raises the drama (${g.x.br_drama().toFixed(2)})`);
  ok(Math.abs(lumHot - lumCalm) > 3, `and the frame is lit differently: mean luma ${lumCalm.toFixed(1)} -> ${lumHot.toFixed(1)}`);
}

// ---- the frame budget -----------------------------------------------------------------------------------------------------------------------
{
  const g = await game(); g.frame(5); g.tap(KEY.enter); g.frame(400);
  const t = []; for (let i = 0; i < 300; i++) { const s = performance.now(); g.frame(1); t.push(performance.now() - s); }
  t.sort((p, q) => p - q);
  const med = t[150], p99 = t[296];
  console.log(`  frame time over 300 frames of a match: median ${med.toFixed(2)} ms, p99 ${p99.toFixed(2)} ms, worst ${t[299].toFixed(2)} ms`);
  ok(med < 12, `the median frame (sim + 11 bots + a 640x360 software render) is under 12 ms (${med.toFixed(2)})`);
}

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
