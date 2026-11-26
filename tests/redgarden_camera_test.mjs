// tests/redgarden_camera_test.mjs -- the REDGARDEN web client's camera and palette.
// The camera was frozen on the hero in the rewrite: the wheel word was stored and never consumed, and no key
// could move the view. This drives the real wasm on the play screen and checks zoom (one step per notch),
// arrow panning (frozen camera, clamped to the map) and space handing the camera back to the hero.
//   scripts/build_redgarden_wasm.sh /tmp/redgarden.wasm && node tests/redgarden_camera_test.mjs /tmp/redgarden.wasm
import fs from 'node:fs';
const wasmPath = process.argv[2];
if (!wasmPath) { console.error('usage: redgarden_camera_test.mjs <redgarden.wasm>'); process.exit(2); }
const { instance } = await WebAssembly.instantiate(fs.readFileSync(wasmPath), {});
const x = instance.exports;
let pass = 0, fail = 0;
const ok = (c, m) => { c ? pass++ : fail++; console.log((c ? 'PASS: ' : 'FAIL: ') + m); };
const g = (k) => x.rg_g(k);
const gf = (k) => x.rg_gf(k);
const key = (code, down) => x.rg_key(code, down);
const frame = (t) => x.rg_frame(t);
const rgb = (r, gg, b) => (0xff000000 | r | (gg << 8) | (b << 16)) >>> 0;

x.rg_init();
frame(1000);
x.go_screen_bang(4);   // sc-play: play-update! is where the camera input lives
frame(1016);
ok(g(83) === 0, 'the camera starts following the hero (lock 0)');
const s0 = gf(53), cx0 = gf(51);
ok(s0 === 10, `scale starts at 10 px/unit (${s0})`);

// one wheel notch = one zoom step, not a re-zoom every frame
x.rg_wheel(1);
frame(1032);
const s1 = gf(53);
frame(1048);
ok(Math.abs(s1 - 9) < 1e-4, `one notch zooms out 10 -> 9 (${s1})`);
ok(gf(53) === s1, `the next frame does not zoom again (${gf(53)}) -- the wheel word is consumed`);
x.rg_wheel(-1); frame(1064);
ok(gf(53) > s1, `a notch the other way zooms back in (${gf(53)})`);
for (let i = 0; i < 40; i++) { x.rg_wheel(-1); frame(1080 + i * 16); }
ok(gf(53) >= 17.9 && gf(53) <= 18.0001, `zoom clamps at 18 (${gf(53)})`);

// arrows pan and freeze the camera; space hands it back
key(37, 1); frame(1800); key(37, 0);
ok(g(83) === 1, 'a pan key freezes the camera off the hero (lock 1)');
ok(gf(51) < cx0, `left arrow moves the view left (${cx0} -> ${gf(51)})`);
const frozen = gf(51);
frame(1816);
ok(gf(51) === frozen, 'and it stays there once the key is released');
key(32, 1); frame(1832); key(32, 0);
ok(g(83) === 0, 'space hands the camera back to the hero (lock 0)');
for (let i = 0; i < 30; i++) frame(1848 + i * 16);
ok(gf(51) > frozen, `it glides back toward the hero (${frozen} -> ${gf(51)})`);

// panning is clamped to the arena
key(37, 1);
for (let i = 0; i < 200; i++) frame(2400 + i * 16);
key(37, 0);
ok(gf(51) >= -51.777 - 0.001, `the view cannot pan off the map (${gf(51)})`);

// the palette is SHANKPIT 460's
ok((x.c_bg() >>> 0) === rgb(14, 14, 18), 'the background is 460 HUD black');
ok((x.c_ink() >>> 0) === rgb(246, 241, 230), 'ink is 460 cream');
ok((x.c_gold() >>> 0) === rgb(232, 190, 112), 'the accent is 460 amber');
ok((x.c_teal() >>> 0) === rgb(142, 178, 230), 'team one is 460 sky-blue');
ok((x.c_rose() >>> 0) === rgb(226, 120, 100), 'team two is 460 coral');

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
