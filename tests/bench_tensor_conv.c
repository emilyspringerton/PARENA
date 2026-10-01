/* tests/bench_tensor_conv.c -- PARENA-driven conv1d (stdlib/tensor/conv.prn over the fused f32-axpy! primitive) against a
 * plain hand-written C conv1d on the same VITS-decoder-shaped problem. Reports GMAC/s for both and checks they agree.
 * Shapes: cin=cout=64, k=7, dil=3, L=4096 (a mid-decoder ResBlock conv). */
#include "parena_runtime.h"
#include <stdio.h>
#include <time.h>
#include <math.h>
#include "test_tensor_gen.c"

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

int main(void) {
    Arena a; arena_init(&a);
    const int cin = 64, cout = 64, k = 7, dil = 3, l = 4096, lp = l + (k - 1) * dil;
    Bytes x = f32_alloc(cin * lp, &a), w = f32_alloc(cout * cin * k, &a), b = f32_alloc(cout, &a), o = f32_alloc(cout * l, &a);
    float *xf = (float *)x.data, *wf = (float *)w.data;
    for (int i = 0; i < cin * lp; i++) xf[i] = (float)((i * 37) % 101) / 101.0f - 0.5f;
    for (int i = 0; i < cout * cin * k; i++) wf[i] = (float)((i * 53) % 97) / 970.0f - 0.05f;
    double macs = (double)cout * cin * k * l;
    double best_p = 1e9, best_c = 1e9;
    float *ref = (float *)malloc(sizeof(float) * (size_t)cout * l);
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now();
        conv1d(x, w, b, o, cin, cout, k, dil, lp, l);
        double t1 = now();
        for (int oc = 0; oc < cout; oc++) {
            float *yo = ref + (size_t)oc * l;
            for (int t = 0; t < l; t++) yo[t] = 0.0f;
            for (int c = 0; c < cin; c++) for (int j = 0; j < k; j++) {
                const float wv = wf[(oc * cin + c) * k + j];
                const float *xp = xf + (size_t)c * lp + j * dil;
                for (int t = 0; t < l; t++) yo[t] += wv * xp[t];
            }
        }
        double t2 = now();
        if (t1 - t0 < best_p) best_p = t1 - t0;
        if (t2 - t1 < best_c) best_c = t2 - t1;
    }
    double maxd = 0; float *of = (float *)o.data;
    for (int i = 0; i < cout * l; i++) { double d = fabs((double)of[i] - (double)ref[i]); if (d > maxd) maxd = d; }
    printf("conv1d cin=%d cout=%d k=%d dil=%d L=%d: PARENA %.2f GMAC/s | hand-written C %.2f GMAC/s | max |diff| %.3g\n",
           cin, cout, k, dil, l, macs / best_p * 1e-9, macs / best_c * 1e-9, maxd);
    free(ref);
    return 0;
}
