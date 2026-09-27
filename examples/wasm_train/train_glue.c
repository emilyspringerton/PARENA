/* examples/wasm_train/train_glue.c -- the thin C layer that turns stdlib/nn_train.prn's
 * generated C into a WebAssembly module JS can drive. Owns one Arena plus the parameter and
 * dataset Vecs; everything numeric is the PARENA-generated code (nn_train_gen.c).
 *
 * JS passes data in through the module heap (Float64Array views over _malloc'd buffers), not
 * one boxed value at a time. The same file compiles natively (make wasm-train builds it with
 * gcc too) so the native and WebAssembly runs can be compared number for number.
 */
/* Built with -DPARENA_NO_GRAPHICS (no SDL2 in a browser); set on the command line because
 * runtime/parena_runtime.c needs it too. */
#include "parena_runtime.h"

#include "nn_train_gen.c"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT
#endif

static Arena g_arena;
static int g_ready = 0;
static Vec g_params, g_xs, g_ys;
static int g_n_in, g_h, g_rows;

static void push(Vec *v, double x) { vec_push_(v, vec_box_f64(v, x)); }

/* nt_setup -- (re)initialise a fresh n_in -> h -> 1 network from `seed` and copy in a
 * row-major dataset of `rows` samples. Frees the previous run's arena first, so a page can
 * retrain as often as it likes without growing memory. Returns the parameter count. */
EXPORT int nt_setup(int n_in, int h, int seed, const double *xs, const double *ys, int rows) {
    if (g_ready) arena_free_all(&g_arena);
    arena_init(&g_arena);
    g_ready = 1;
    g_n_in = n_in; g_h = h; g_rows = rows;
    g_params = vec_new(&g_arena);
    g_xs = vec_new(&g_arena);
    g_ys = vec_new(&g_arena);
    for (int i = 0; i < rows * n_in; i++) push(&g_xs, xs[i]);
    for (int r = 0; r < rows; r++) push(&g_ys, ys[r]);
    init_params_(&g_params, n_in, h, seed);
    return vec_len(&g_params);
}

/* nt_train -- `epochs` SGD passes; returns the mean pre-update loss of the last pass. */
EXPORT double nt_train(int epochs, double lr) {
    double loss = 0.0;
    for (int e = 0; e < epochs; e++)
        loss = train_epoch_(&g_params, &g_xs, &g_ys, g_rows, g_n_in, g_h, lr);
    return loss;
}

EXPORT double nt_loss(void) { return dataset_loss(&g_params, &g_xs, &g_ys, g_rows, g_n_in, g_h); }
EXPORT double nt_predict(int row) { return predict(&g_params, &g_xs, row, g_n_in, g_h); }

/* nt_hidden_pre -- W1[j].x[row] + b1[j], the PARENA CPU reference the WebGPU matmul kernel's
 * first-layer output is checked against. */
EXPORT double nt_hidden_pre(int row, int j) {
    return hidden_pre(&g_params, &g_xs, row, g_n_in, g_h, j);
}

/* nt_get_params -- copy the flat parameter vector out (layout: see stdlib/nn_train.prn). */
EXPORT void nt_get_params(double *out) {
    for (int i = 0; i < vec_len(&g_params); i++) out[i] = *(double *)vec_get(&g_params, i);
}

#ifndef __EMSCRIPTEN__
#include <stdio.h>
/* Native build: train the same XOR run the WebAssembly smoke test does and print the final
 * parameters at full precision, so the two can be diffed. */
int main(void) {
    const double xs[8] = { 0, 0, 0, 1, 1, 0, 1, 1 };
    const double ys[4] = { 0, 1, 1, 0 };
    int n = nt_setup(2, 4, 7, xs, ys, 4);
    nt_train(3000, 0.5);
    double p[64];
    nt_get_params(p);
    for (int i = 0; i < n; i++) printf("%.17g\n", p[i]);
    return 0;
}
#endif
