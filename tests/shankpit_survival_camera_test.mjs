// tests/shankpit_survival_camera_test.mjs -- SURVIVAL's wheel zoom, third-person camera and click to move (sv_camera.prn).
//   node tests/shankpit_survival_camera_test.mjs <survival.wasm>
import fs from 'node:fs';
const wasmPath = process.argv[2];
let pass = 0, fail = 0;
const ok = (c, m) => { c ? pass++ : fail++; console.log((c ? 'PASS: ' : 'FAIL: ') + m); };
const { instance } = await WebAssembly.instantiate(fs.readFileSync(wasmPath), {});
const x = instance.exports; x.sp_init();
let now = 1000;
const dv = () => new DataView(x.memory.buffer);
const frame = (n = 1) => { for (let i = 0; i < n; i++) { now += 16; x.sp_frame(now); } };
const gi = (k) => dv().getInt32(135168 + k * 4, true), gf = (k) => dv().getFloat32(135168 + k * 4, true);
const poke = (k, v) => x.sp_t_poke(k, v);
const G = { camX: 54, camY: 55, camZ: 56, sy: 57, cy: 58, sp: 59, cp: 60, scale: 61, pitch: 49, yaw: 48, want: 245, arm: 650, target: 651, fp: 652, dest: 653, dx: 654, dz: 655, hyaw: 657 };
const hero = () => [x.sp_player_addr(0)].map((a) => [dv().getFloat32(a + 16, true), dv().getFloat32(a + 24, true)])[0];
const place = (px, pz) => x.sp_t_place(0, px, 6, pz, 0, 0);
const tap = (code) => { x.sp_key(code, 1); frame(1); x.sp_key(code, 0); frame(1); };
// world ground point -> framebuffer pixel, with the engine's own camera numbers (sp_3d project-point)
const pixelOf = (wx, wz, wy = 0) => {
  const dx = wx - gf(G.camX), dy = wy - gf(G.camY), dz = wz - gf(G.camZ), sy = gf(G.sy), cy = gf(G.cy), sp = gf(G.sp), cp = gf(G.cp);
  const cx = dx * cy + dz * sy, cyy = -dx * sy * sp + dy * cp + dz * cy * sp, cz = dx * sy * cp + dy * sp - dz * cy * cp;
  return [Math.round(320 + (cx / cz) * gf(G.scale)), Math.round(180 - (cyy / cz) * gf(G.scale)), cz];
};

frame(3); x.sp_pointer(320, 204); x.sp_button(0, 1); x.sp_button(0, 0); x.sp_lock(1); frame(140);   // PLAY + the countdown
ok(gi(G.fp) === 1, 'it starts in first person');
place(0, 0); frame(2);

// click in first person: ignored
x.sp_click(320, 300); frame(1);
ok(gi(G.dest) === 0, 'first person: a click sets no destination (the mouse is aiming)');

// the wheel: zoom out leaves first person; the camera moves behind and above the hero
x.sp_zoom(-1); frame(60);
ok(gi(G.fp) === 0 && gi(G.arm) > 1500, `one wheel notch out: third person (arm ${gi(G.arm)} milli)`);
ok(gf(G.pitch) <= -10, `it looks down at the hero (pitch ${gf(G.pitch).toFixed(0)})`);
for (let i = 0; i < 6; i++) { x.sp_zoom(-1); frame(2); }
frame(120);
const farArm = gi(G.arm);
ok(farArm > 20000 && farArm <= 40000, `more notches zoom further out, capped (${(farArm / 1000).toFixed(1)} units)`);
const [hx0, hz0] = hero();
ok(Math.hypot(gf(G.camX) - hx0, gf(G.camZ) - hz0) > 8 && gf(G.camY) > 8, 'the camera sits well behind and above the hero');
x.sp_zoom(1); frame(120);
ok(gi(G.arm) < farArm, 'the wheel the other way zooms back in');

// click to move: a ground point in the plaza
frame(30);
const target = [30, -25];
const [px, py, cz] = pixelOf(...target);
ok(cz > 0 && px > 0 && px < 640 && py > 0 && py < 360, `the target is on screen (${px}, ${py})`);
x.sp_click(px, py); frame(1);
ok(gi(G.dest) === 1 && Math.hypot(gf(G.dx) - target[0], gf(G.dz) - target[1]) < 2.5, `a left click on the ground sets the destination (${gf(G.dx).toFixed(1)}, ${gf(G.dz).toFixed(1)})`);
const d0 = Math.hypot(target[0] - hero()[0], target[1] - hero()[1]);
frame(120);
const d1 = Math.hypot(target[0] - hero()[0], target[1] - hero()[1]);
ok(d1 < d0 * 0.5, `the hero walks there (${d0.toFixed(1)} -> ${d1.toFixed(1)} units away)`);
frame(300);
console.log('info: hero', hero().map((v) => v.toFixed(1)).join(','), 'dest', gi(G.dest));
ok(Math.hypot(target[0] - hero()[0], target[1] - hero()[1]) < 1.5 && gi(G.dest) === 0, 'it arrives and the destination clears');

// sky: no ground under the pixel
x.sp_click(320, 4); frame(1);
ok(gi(G.dest) === 0, 'a click on the sky is not somewhere to walk');

// WASD cancels
{ const [a, b] = hero(); x.sp_click(...pixelOf(a - 8, b + 6).slice(0, 2)); frame(1); }
ok(gi(G.dest) === 1, 'a new destination');
tap(87); frame(1);
ok(gi(G.dest) === 0, 'W cancels click to move');

// the sniper has a right click: click to move is off
poke(G.want, 4); frame(2);
{ const [a, b] = hero(); x.sp_click(...pixelOf(a + 7, b + 5).slice(0, 2)); frame(1); }
ok(gi(G.dest) === 0, 'with the sniper wanted, click to move is disabled (its right mouse is the scope)');
poke(G.want, 0); frame(2);
{ const [a, b] = hero(); x.sp_click(...pixelOf(a + 7, b + 5).slice(0, 2)); frame(1); }
ok(gi(G.dest) === 1, 'with the knife again it works');

// click a zombie: it becomes the attack target; the hero closes in, faces it and fights
{
  x.sp_t_poke(G.want, 0);
  const [a, b] = hero();
  const zp = [a + 14, b + 2];
  const sa = x.sp_player_addr(1);
  const setI = (off, v) => dv().setInt32(sa + off, v, true), setF = (off, v) => dv().setFloat32(sa + off, v, true);
  x.sp_t_spawn(1, zp[0], 6, zp[1]);
  dv().setInt32(sa + 4, 1, true);     // active
  frame(2);
  const [zx, zy] = (() => { const zy0 = dv().getFloat32(sa + 20, true); const k = pixelOf(dv().getFloat32(sa + 16, true), dv().getFloat32(sa + 24, true), zy0 + 2.0); return [k[0], k[1]]; })();
  x.sp_click(zx, zy); frame(1);
  ok(gi(662) === 2, `a click on a zombie targets it (atk ${gi(662)})`);
  ok(gi(G.dest) === 0, 'and drops any ground destination');
  const d0 = Math.hypot(dv().getFloat32(sa + 16, true) - hero()[0], dv().getFloat32(sa + 24, true) - hero()[1]);
  frame(40);
  ok(Math.hypot(dv().getFloat32(sa + 16, true) - hero()[0], dv().getFloat32(sa + 24, true) - hero()[1]) < d0, 'the hero closes on it');
  tap(87); frame(1);
  ok(gi(662) === 0, 'W drops the attack target');
  dv().setInt32(sa + 4, 0, true);
}

// all the way in: first person, destination dropped
for (let i = 0; i < 12; i++) { x.sp_zoom(1); frame(2); }
frame(150);
ok(gi(G.fp) === 1 && gi(G.dest) === 0, 'zooming all the way in is first person and drops the destination');

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
