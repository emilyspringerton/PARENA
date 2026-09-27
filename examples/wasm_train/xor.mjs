// xor.mjs -- the XOR dataset plus the heap plumbing for train_glue.c's exports, shared by the
// Node smoke test and the browser page so both drive the WebAssembly module identically.

export const XOR_X = [0, 0, 0, 1, 1, 0, 1, 1];
export const XOR_Y = [0, 1, 1, 0];
export const N_IN = 2;
export const HIDDEN = 4;

// setup -- copy xs/ys into the module heap and (re)initialise the network. Returns the
// parameter count.
export function setup(M, { seed = 7, xs = XOR_X, ys = XOR_Y, nIn = N_IN, hidden = HIDDEN } = {}) {
  const px = M._malloc(xs.length * 8);
  const py = M._malloc(ys.length * 8);
  M.HEAPF64.set(xs, px / 8);
  M.HEAPF64.set(ys, py / 8);
  const n = M._nt_setup(nIn, hidden, seed, px, py, ys.length);
  M._free(px);
  M._free(py);
  return n;
}

export function getParams(M, n) {
  const p = M._malloc(n * 8);
  M._nt_get_params(p);
  const out = Array.from(M.HEAPF64.subarray(p / 8, p / 8 + n));
  M._free(p);
  return out;
}

export function trainXor(M, { epochs = 3000, lr = 0.5, seed = 7 } = {}) {
  const n = setup(M, { seed });
  const first = M._nt_loss();
  M._nt_train(epochs, lr);
  const last = M._nt_loss();
  const preds = XOR_Y.map((_, r) => M._nt_predict(r));
  return { params: getParams(M, n), first, last, preds };
}
