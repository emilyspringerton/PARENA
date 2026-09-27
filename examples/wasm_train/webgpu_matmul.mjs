// webgpu_matmul.mjs -- one hand-written WGSL compute kernel: C = A * B + bias (bias broadcast
// across rows), A is m x k, B is k x n, all row-major. This is the matmul that would move to
// the GPU first in a training loop: the first layer's pre-activations for a whole batch,
// X (rows x n_in) * W1^T (n_in x h) + b1.
//
// PARENA does not emit WGSL; this kernel is written by hand and is only trusted because the
// page checks every output against the PARENA-generated CPU reference (nt_hidden_pre in the
// WebAssembly module). WGSL has no f64, so the GPU computes in f32: expect ~1e-6 relative
// agreement with the f64 CPU path, not bit equality.

export const MATMUL_WGSL = /* wgsl */ `
struct Dims { m : u32, k : u32, n : u32, _pad : u32 };

@group(0) @binding(0) var<uniform> dims : Dims;
@group(0) @binding(1) var<storage, read> a : array<f32>;
@group(0) @binding(2) var<storage, read> b : array<f32>;
@group(0) @binding(3) var<storage, read> bias : array<f32>;
@group(0) @binding(4) var<storage, read_write> c : array<f32>;

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
  let row = gid.x;
  let col = gid.y;
  if (row >= dims.m || col >= dims.n) { return; }
  var acc = bias[col];
  for (var i = 0u; i < dims.k; i = i + 1u) {
    acc = acc + a[row * dims.k + i] * b[i * dims.n + col];
  }
  c[row * dims.n + col] = acc;
}
`;

export async function getDevice() {
  if (!globalThis.navigator || !navigator.gpu) return null;
  const adapter = await navigator.gpu.requestAdapter();
  if (!adapter) return null;
  return adapter.requestDevice();
}

// gpuMatmul -- returns a Float32Array of m*n results. `a`, `b`, `bias` are plain arrays or
// typed arrays (converted to f32).
export async function gpuMatmul(device, a, b, bias, m, k, n) {
  const usage = GPUBufferUsage;
  const upload = (data, flags) => {
    const arr = new Float32Array(data);
    const buf = device.createBuffer({ size: Math.max(16, arr.byteLength), usage: flags | usage.COPY_DST });
    device.queue.writeBuffer(buf, 0, arr);
    return buf;
  };
  const dims = device.createBuffer({ size: 16, usage: usage.UNIFORM | usage.COPY_DST });
  device.queue.writeBuffer(dims, 0, new Uint32Array([m, k, n, 0]));
  const bufA = upload(a, usage.STORAGE);
  const bufB = upload(b, usage.STORAGE);
  const bufBias = upload(bias, usage.STORAGE);
  const outBytes = m * n * 4;
  const bufC = device.createBuffer({ size: Math.max(16, outBytes), usage: usage.STORAGE | usage.COPY_SRC });
  const readback = device.createBuffer({ size: Math.max(16, outBytes), usage: usage.MAP_READ | usage.COPY_DST });

  const pipeline = device.createComputePipeline({
    layout: "auto",
    compute: { module: device.createShaderModule({ code: MATMUL_WGSL }), entryPoint: "main" },
  });
  const bind = device.createBindGroup({
    layout: pipeline.getBindGroupLayout(0),
    entries: [dims, bufA, bufB, bufBias, bufC].map((buffer, binding) => ({ binding, resource: { buffer } })),
  });
  const enc = device.createCommandEncoder();
  const pass = enc.beginComputePass();
  pass.setPipeline(pipeline);
  pass.setBindGroup(0, bind);
  pass.dispatchWorkgroups(Math.ceil(m / 8), Math.ceil(n / 8));
  pass.end();
  enc.copyBufferToBuffer(bufC, 0, readback, 0, outBytes);
  device.queue.submit([enc.finish()]);
  await readback.mapAsync(GPUMapMode.READ);
  const out = new Float32Array(readback.getMappedRange(0, outBytes).slice(0));
  readback.unmap();
  [dims, bufA, bufB, bufBias, bufC, readback].forEach((buf) => buf.destroy());
  return out;
}

// firstLayerOperands -- lay out X (rows x nIn) and W1^T (nIn x h) + b1 from nn_train.prn's flat
// parameter vector (W1[j][k] at j*nIn + k, b1 at h*nIn).
export function firstLayerOperands(params, xs, rows, nIn, h) {
  const w1t = new Float32Array(nIn * h);
  for (let j = 0; j < h; j++) for (let k = 0; k < nIn; k++) w1t[k * h + j] = params[j * nIn + k];
  const b1 = params.slice(h * nIn, h * nIn + h);
  return { a: xs.slice(0, rows * nIn), b: w1t, bias: b1 };
}
