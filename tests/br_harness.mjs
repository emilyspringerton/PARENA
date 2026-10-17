// tests/br_harness.mjs -- boots bedrockracers.wasm for node tests: typed access to the car / ball / global words.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const here = path.dirname(fileURLToPath(import.meta.url));
const src = fs.readFileSync(path.join(here, '../stdlib/bedrockracers/web/br_fields.prn'), 'utf8');
export const K = {}, KB = {}, KF = {};
for (const m of src.matchAll(/\(defn k-([a-z0-9-]+) \[\] : I32 (\d+)\)/g)) K[m[1]] = +m[2];
for (const m of src.matchAll(/\(defn kb-([a-z0-9-]+) \[\] : I32 (\d+)\)/g)) KB[m[1]] = +m[2];
for (const m of src.matchAll(/\(defn kf-([a-z0-9-]+) \[\] : I32 (\d+)\)/g)) KF[m[1]] = +m[2];
const isInt = new Set([...src.matchAll(/\(defn (c|b|f)-([a-z0-9-]+) \[[^\]]*\] : I32/g)].map(m => m[1] + '-' + m[2]));
export async function boot(wasmPath) {
  const { instance } = await WebAssembly.instantiate(fs.readFileSync(wasmPath), {});
  const x = instance.exports;
  x.br_init();
  const dv = () => new DataView(x.memory.buffer);
  const api = {
    x, dv,
    car(s, k) { const a = x.br_car_addr(s) + K[k] * 4; return isInt.has('c-' + k) ? dv().getInt32(a, true) : dv().getFloat32(a, true); },
    setCar(s, k, v) { const a = x.br_car_addr(s) + K[k] * 4; isInt.has('c-' + k) ? dv().setInt32(a, v, true) : dv().setFloat32(a, v, true); },
    ball(k) { const a = x.br_ball_addr() + KB[k] * 4; return isInt.has('b-' + k) ? dv().getInt32(a, true) : dv().getFloat32(a, true); },
    g: (k) => dv().getInt32(135168 + k * 4, true),
    gf: (k) => dv().getFloat32(135168 + k * 4, true),
    pos: (s) => [api.car(s, 'x'), api.car(s, 'y'), api.car(s, 'z')],
    vel: (s) => [api.car(s, 'vx'), api.car(s, 'vy'), api.car(s, 'vz')],
    up: (s) => [api.car(s, 'ux'), api.car(s, 'uy'), api.car(s, 'uz')],
    fwd: (s) => [api.car(s, 'fx'), api.car(s, 'fy'), api.car(s, 'fz')],
    speed: (s) => Math.hypot(...api.vel(s)),
  };
  return api;
}
export const f2 = (a) => a.map(v => v.toFixed(1)).join(',');
