// Drives the bedrockracers wasm with scripted input and writes the framebuffer as PNG screenshots.
//   node br_shot.mjs <br.wasm> <outdir> [script]
// script is a ';'-separated list:  key:13  keyup:13  tap:13  hold:65:30  frames:120  shot:name  click  lock:1
//   mouse:dx:dy  pointer:x:y  wheel:1  set:globalIndex:value (raw I32)  call:exportName:arg:arg
// Used for eyeballing the renderer and by shankpit460_engine_test.mjs for "the screen is not blank" checks.
import fs from 'node:fs';
import zlib from 'node:zlib';
import path from 'node:path';

export function pngEncode(w, h, rgba) {
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) {
    raw[y * (w * 4 + 1)] = 0;
    Buffer.from(rgba.buffer, rgba.byteOffset + y * w * 4, w * 4).copy(raw, y * (w * 4 + 1) + 1);
  }
  const chunk = (type, data) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
    const td = Buffer.concat([Buffer.from(type), data]);
    const crc = Buffer.alloc(4); crc.writeUInt32BE(zlib.crc32(td) >>> 0);
    return Buffer.concat([len, td, crc]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 6;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr),
    chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}

export async function load(wasmPath) {
  const { instance } = await WebAssembly.instantiate(fs.readFileSync(wasmPath), {});
  const x = instance.exports;
  let now = 1000;
  const api = {
    x,
    frame(n = 1) { for (let i = 0; i < n; i++) { now += 1000 / 60; x.br_frame(now); } },
    key(id, down) { x.br_key(id, down ? 1 : 0); },
    tap(id, frames = 3) { x.br_key(id, 1); api.frame(frames); x.br_key(id, 0); api.frame(2); },
    hold(id, frames) { x.br_key(id, 1); api.frame(frames); x.br_key(id, 0); },
    click(px, py) { if (px !== undefined) x.br_pointer(px, py); x.br_button(0, 1); api.frame(2); x.br_button(0, 0); api.frame(1); },
    fb() { return new Uint8Array(x.memory.buffer, x.br_fb_addr(), x.br_fb_w() * x.br_fb_h() * 4); },
    png() { const a = api.fb(); return pngEncode(x.br_fb_w(), x.br_fb_h(), a); },
    now: () => now,
  };
  x.br_init();
  return api;
}

if (process.argv[1] && process.argv[1].endsWith('br_shot.mjs')) {
  const [wasm, outdir, script = ''] = process.argv.slice(2);
  fs.mkdirSync(outdir, { recursive: true });
  const g = await load(wasm);
  for (const step of script.split(';').map((s) => s.trim()).filter(Boolean)) {
    const [op, ...a] = step.split(':');
    if (op === 'key') g.key(+a[0], true);
    else if (op === 'keyup') g.key(+a[0], false);
    else if (op === 'tap') g.tap(+a[0], a[1] ? +a[1] : 3);
    else if (op === 'hold') g.hold(+a[0], +a[1]);
    else if (op === 'frames') g.frame(+a[0]);
    else if (op === 'click') g.click(a[0] !== undefined ? +a[0] : undefined, a[1] !== undefined ? +a[1] : undefined);
    else if (op === 'lock') g.x.br_lock(+a[0]);
    else if (op === 'mouse') g.x.br_mouse(+a[0], +a[1]);
    else if (op === 'pointer') g.x.br_pointer(+a[0], +a[1]);
    else if (op === 'call') g.x[a[0]](...a.slice(1).map(Number));
    else if (op === 'shot') { g.frame(2); fs.writeFileSync(path.join(outdir, a[0] + '.png'), g.png()); console.log('wrote', a[0] + '.png', 'phase', g.x.br_phase()); }
  }
}
