/* tests/test_tensor_nn.c -- stdlib/tensor/nn.prn. Expected values are textbook constants (tanh(1), Phi(1), e, ln 2 ...) or hand
 * arithmetic, never copied from this implementation's output. Strict flags + ASan/UBSan. */
#include "parena_runtime.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "test_tensor_nn_gen.c"

static void near(double got, double want, double tol, const char *what) {
    if (fabs(got - want) > tol) { printf("MISMATCH %s: got %.9f want %.9f\n", what, got, want); assert(0); }
}

static Bytes vec(Arena *a, int n, const double *v) {
    Bytes b = f32_alloc(n, a);
    for (int i = 0; i < n; i++) f32_set_(b, i, v[i]);
    return b;
}

int main(void) {
    Arena a; arena_init(&a);
    {   /* relu / leaky relu */
        double v[4] = { -2.0, -0.5, 0.0, 3.0 };
        Bytes r = vec(&a, 4, v), l = vec(&a, 4, v);
        nn_relu_(r, 0, 4);
        assert(f32_get(r, 0) == 0.0 && f32_get(r, 1) == 0.0 && f32_get(r, 2) == 0.0 && f32_get(r, 3) == 3.0);
        nn_leaky_relu_(l, 0, 4, 0.1);
        near(f32_get(l, 0), -0.2, 1e-7, "leaky[0]"); near(f32_get(l, 1), -0.05, 1e-7, "leaky[1]");
        assert(f32_get(l, 2) == 0.0 && f32_get(l, 3) == 3.0);
        nn_relu_(r, 3, 5);                                   /* range overruns the buffer: whole call no-op */
        assert(f32_get(r, 3) == 3.0);
    }
    {   /* tanh, sigmoid, exact-erf gelu, exp, softplus, recip, neg, sqrt */
        double v[2] = { 0.0, 1.0 };
        Bytes t = vec(&a, 2, v), s = vec(&a, 2, v), g = vec(&a, 2, v), e = vec(&a, 2, v);
        nn_tanh_(t, 0, 2);       near(f32_get(t, 0), 0.0, 1e-9, "tanh0");  near(f32_get(t, 1), 0.7615941559557649, 2e-7, "tanh1");
        nn_sigmoid_(s, 0, 2);    near(f32_get(s, 0), 0.5, 1e-9, "sig0");   near(f32_get(s, 1), 0.7310585786300049, 2e-7, "sig1");
        nn_gelu_(g, 0, 2);       near(f32_get(g, 0), 0.0, 1e-9, "gelu0");  near(f32_get(g, 1), 0.8413447460685429, 2e-7, "gelu1");   /* x*Phi(x), Phi(1)=0.84134474606854 */
        nn_exp_(e, 0, 2);        near(f32_get(e, 0), 1.0, 1e-7, "exp0");   near(f32_get(e, 1), 2.718281828459045, 3e-7, "exp1");
        double m[1] = { -1.0 }; Bytes gm = vec(&a, 1, m); nn_gelu_(gm, 0, 1);
        near(f32_get(gm, 0), -0.15865525393145707, 2e-7, "gelu(-1)");      /* -Phi(-1) = -(1-0.84134474606854) */
        double z[3] = { 0.0, 30.0, -50.0 }; Bytes sp = vec(&a, 3, z); nn_softplus_(sp, 0, 3);
        near(f32_get(sp, 0), 0.6931471805599453, 2e-7, "softplus0");       /* ln 2 */
        near(f32_get(sp, 1), 30.0, 1e-5, "softplus30");                    /* large x passes through */
        near(f32_get(sp, 2), 0.0, 1e-9, "softplus-50");
        double q[3] = { 4.0, 0.0, -0.5 }; Bytes rc = vec(&a, 3, q); nn_recip_(rc, 0, 3);
        assert(f32_get(rc, 0) == 0.25 && f32_get(rc, 1) == 0.0 && f32_get(rc, 2) == -2.0);    /* 1/0 -> 0, never inf */
        Bytes ng = vec(&a, 3, q); nn_neg_(ng, 0, 3); assert(f32_get(ng, 0) == -4.0 && f32_get(ng, 2) == 0.5);
        double sq[3] = { 9.0, 2.25, -4.0 }; Bytes sr = vec(&a, 3, sq); nn_sqrt_(sr, 0, 3);
        assert(f32_get(sr, 0) == 3.0 && f32_get(sr, 1) == 1.5 && f32_get(sr, 2) == 0.0);     /* negative -> 0, never NaN */
    }
    {   /* constants, clamp, mul, add */
        double v[4] = { -3.0, -0.25, 0.5, 2.0 };
        Bytes c = vec(&a, 4, v);
        nn_clamp_(c, 0, 4, 0.5);
        assert(f32_get(c, 0) == -0.5 && f32_get(c, 1) == -0.25 && f32_get(c, 2) == 0.5 && f32_get(c, 3) == 0.5);
        nn_add_const_(c, 0, 4, 1.0); nn_mul_const_(c, 0, 4, 2.0);
        assert(f32_get(c, 0) == 1.0 && f32_get(c, 1) == 1.5 && f32_get(c, 3) == 3.0);
        double w[4] = { 1, 2, 3, 4 }, x[4] = { 10, 20, 30, 40 };
        Bytes y = vec(&a, 4, w), xx = vec(&a, 4, x);
        nn_mul_(y, 0, xx, 0, 4);  assert(f32_get(y, 0) == 10.0 && f32_get(y, 3) == 160.0);
        nn_add_(y, 1, xx, 0, 3);  assert(f32_get(y, 1) == 50.0 && f32_get(y, 2) == 110.0 && f32_get(y, 3) == 190.0);   /* [10,40,90,160] + [_,10,20,30] */
    }
    {   /* softmax: [1,2,3] -> e^-2, e^-1, 1 over their sum = 0.0900306, 0.2447285, 0.6652410 ; equal row -> uniform ; huge logits stay finite */
        double v[6] = { 1, 2, 3, 5, 5, 5 };
        Bytes s = vec(&a, 6, v);
        nn_softmax_rows_(s, 0, 2, 3);
        near(f32_get(s, 0), 0.09003057317038046, 2e-7, "sm0"); near(f32_get(s, 1), 0.24472847105479764, 2e-7, "sm1"); near(f32_get(s, 2), 0.6652409557748219, 2e-7, "sm2");
        near(f32_get(s, 3), 1.0 / 3.0, 2e-7, "sm-uniform");
        near(f32_get(s, 0) + f32_get(s, 1) + f32_get(s, 2), 1.0, 2e-7, "sm-sum");
        double h[2] = { 1000.0, 1000.0 }; Bytes hs = vec(&a, 2, h); nn_softmax_rows_(hs, 0, 1, 2);
        near(f32_get(hs, 0), 0.5, 1e-9, "sm-huge"); near(f32_get(hs, 1), 0.5, 1e-9, "sm-huge");
    }
    {   /* channel layernorm on x[C=2][T=2] = [[1,3],[3,5]]: each time step has two values a<b with var 1 -> (-1,+1)/sqrt(1+eps) */
        double v[4] = { 1, 3, 3, 5 }, gm[2] = { 2.0, 1.0 }, bt[2] = { 0.5, 0.0 };
        Bytes x = vec(&a, 4, v), g = vec(&a, 2, gm), b = vec(&a, 2, bt);
        nn_layernorm_ct_(x, 0, 2, 2, g, b, 1e-5);
        double k = 1.0 / sqrt(1.0 + 1e-5);
        near(f32_get(x, 0), -k * 2.0 + 0.5, 1e-6, "ln[0][0]"); near(f32_get(x, 1), -k * 2.0 + 0.5, 1e-6, "ln[0][1]");
        near(f32_get(x, 2), k, 1e-6, "ln[1][0]");              near(f32_get(x, 3), k, 1e-6, "ln[1][1]");
        /* constant channel vector -> zero variance -> output is just beta (no NaN) */
        double c[2] = { 7, 7 }, one[2] = { 1, 1 }, bz[2] = { 0.25, -0.25 };
        Bytes cx = vec(&a, 2, c), o = vec(&a, 2, one), bb = vec(&a, 2, bz);
        nn_layernorm_ct_(cx, 0, 2, 1, o, bb, 1e-5);
        near(f32_get(cx, 0), 0.25, 1e-6, "ln-const0"); near(f32_get(cx, 1), -0.25, 1e-6, "ln-const1");
        nn_layernorm_ct_(cx, 0, 2, 1, vec(&a, 1, one), bb, 1e-5);          /* gamma too short: refused, untouched */
        near(f32_get(cx, 0), 0.25, 1e-6, "ln-refused");
    }
    {   /* cumsum and padding */
        double v[4] = { 1, 2, 3, 4 };
        Bytes x = vec(&a, 4, v), y = f32_alloc(4, &a);
        nn_cumsum_(y, 0, x, 0, 4);
        assert(f32_get(y, 0) == 1.0 && f32_get(y, 1) == 3.0 && f32_get(y, 2) == 6.0 && f32_get(y, 3) == 10.0);
        double s[6] = { 1, 2, 3, 4, 5, 6 };                              /* C=2, T=3 */
        Bytes src = vec(&a, 6, s), dst = f32_alloc(12, &a);              /* rows of 3 + 1 + 2 = 6 */
        f32_fill_(dst, 0, 12, 9.0);
        nn_pad_rows_(dst, src, 2, 3, 1, 2);
        double want[12] = { 0, 1, 2, 3, 0, 0, 0, 4, 5, 6, 0, 0 };
        for (int i = 0; i < 12; i++) assert(f32_get(dst, i) == want[i]);
        Bytes small = f32_alloc(4, &a); f32_fill_(small, 0, 4, 9.0);     /* dst too small: refused, untouched */
        nn_pad_rows_(small, src, 2, 3, 1, 2);
        assert(f32_get(small, 0) == 9.0);
    }
    printf("test_tensor_nn: all assertions passed\n");
    arena_free_all(&a);
    return 0;
}
