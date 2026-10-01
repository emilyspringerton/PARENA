/* tests/test_tensor.c -- stdlib/tensor/f32.prn + stdlib/tensor/conv.prn. Small cases are hand-derived; the conv/convT/
 * matmul kernels are compared against plain double-precision C reference loops on random data (tolerance sized for
 * float32); hostile arguments must never fault. Built strict + ASan/UBSan (make test-tensor). */
#include "parena_runtime.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "test_tensor_gen.c"

static unsigned int g_s = 2463534242u;
static float rnd(void) { g_s ^= g_s << 13; g_s ^= g_s >> 17; g_s ^= g_s << 5; return (float)(g_s % 20001u) / 10000.0f - 1.0f; }

static Bytes mk(Arena *a, int n) { Bytes b = f32_alloc(n, a); for (int i = 0; i < n; i++) f32_set_(b, i, rnd()); return b; }

static void close_to(double got, double want, double tol, const char *what, int idx) {
    double err = fabs(got - want), scale = fabs(want) > 1.0 ? fabs(want) : 1.0;
    if (err > tol * scale) { printf("MISMATCH %s[%d]: got %.7f want %.7f\n", what, idx, got, want); assert(0); }
}

int main(void) {
    Arena a; arena_init(&a);

    /* ---- core buffer ---- */
    Bytes z = f32_alloc(10, &a);
    assert(f32_len(z) == 10);
    for (int i = 0; i < 10; i++) assert(f32_get(z, i) == 0.0);                 /* zero-filled */
    f32_set_(z, 3, 2.5); f32_set_(z, 12, 9.0); f32_set_(z, -1, 9.0);              /* OOB writes are no-ops */
    assert(f32_get(z, 3) == 2.5 && f32_get(z, 12) == 0.0 && f32_get(z, -1) == 0.0);
    assert(f32_len(f32_alloc(-5, &a)) == 0 && f32_len(f32_alloc(0x7fffffff, &a)) == 0);   /* hostile sizes -> empty */
    f32_fill_(z, 2, 3, 7.0);                                                       /* [2,5) = 7 */
    assert(f32_get(z, 1) == 0.0 && f32_get(z, 2) == 7.0 && f32_get(z, 4) == 7.0 && f32_get(z, 5) == 0.0);
    f32_fill_(z, 8, 5, 1.0);                                                       /* range overruns: whole call is a no-op */
    assert(f32_get(z, 8) == 0.0 && f32_get(z, 9) == 0.0);
    /* copy with overlap (memmove): [2,5) -> [3,6): 7 7 7 at 3,4,5 */
    f32_copy_(z, 3, z, 2, 3);
    assert(f32_get(z, 2) == 7.0 && f32_get(z, 3) == 7.0 && f32_get(z, 5) == 7.0 && f32_get(z, 6) == 0.0);

    /* axpy / dot / scale with exactly-representable integers: y = [1,2,3,4,5,6,7,8,9,10,11], x = [1]*11 */
    Bytes y = f32_alloc(11, &a), x = f32_alloc(11, &a);
    for (int i = 0; i < 11; i++) { f32_set_(y, i, (double)(i + 1)); f32_set_(x, i, 1.0); }
    f32_axpy_(y, 0, x, 0, 3.0, 11);                                                /* y[i] = i+1+3 */
    for (int i = 0; i < 11; i++) assert(f32_get(y, i) == (double)(i + 4));
    assert(f32_dot(y, 0, x, 0, 11) == 99.0);                                     /* 4+5+..+14 = 99 (n=11: 8 + 3 tail) */
    assert(f32_dot(y, 0, y, 0, 3) == 16.0 + 25.0 + 36.0);                        /* tail-only path */
    assert(f32_dot(y, 0, x, 0, 12) == 0.0 && f32_dot(y, -1, x, 0, 3) == 0.0 && f32_dot(y, 0, x, 0, 0) == 0.0);   /* invalid ranges -> 0 */
    f32_scale_(y, 2, 3, 2.0);                                                      /* y[2..4]: 6,7,8 -> 12,14,16 */
    assert(f32_get(y, 1) == 5.0 && f32_get(y, 2) == 12.0 && f32_get(y, 4) == 16.0 && f32_get(y, 5) == 9.0);
    /* strided axpy: y[1 + 3*i] += 10 * x[i], i in [0,3): y[1], y[4], y[7] */
    Bytes ys = f32_alloc(10, &a), xs = f32_alloc(3, &a);
    for (int i = 0; i < 3; i++) f32_set_(xs, i, (double)(i + 1));
    f32_axpy_strided_(ys, 1, 3, xs, 0, 10.0, 3);
    assert(f32_get(ys, 1) == 10.0 && f32_get(ys, 4) == 20.0 && f32_get(ys, 7) == 30.0 && f32_get(ys, 2) == 0.0);
    f32_axpy_strided_(ys, 1, 4, xs, 0, 10.0, 3);                                   /* last index 1+8=9 ok */
    assert(f32_get(ys, 9) == 30.0);
    f32_axpy_strided_(ys, 2, 5, xs, 0, 10.0, 3);                                   /* last index 12 > 9: whole call no-op */
    assert(f32_get(ys, 2) == 0.0 && f32_get(ys, 7) == 30.0);
    f32_axpy_strided_(ys, 0, 0, xs, 0, 1.0, 3);                                    /* zero stride refused */
    assert(f32_get(ys, 0) == 0.0);
    /* misaligned buffer (a view one byte into an allocation) must take the memcpy path, not fault under UBSan */
    {
        Bytes raw = bytes_alloc_impl(&a, 4 * 9 + 1);
        Bytes mis = raw; mis.data += 1; mis.len = 4 * 8;
        for (int i = 0; i < 8; i++) f32_set_(mis, i, (double)i);
        Bytes ones = f32_alloc(8, &a); f32_fill_(ones, 0, 8, 1.0);
        f32_axpy_(mis, 0, ones, 0, 2.0, 8);
        for (int i = 0; i < 8; i++) assert(f32_get(mis, i) == (double)(i + 2));
        assert(f32_dot(mis, 0, ones, 0, 8) == 44.0);                              /* 2+3+..+9 */
    }
    /* little-endian loader: 0x3f800000 = 1.0, 0xc0000000 = -2.0, 0x00000000 = 0, 0x7f800000 = +inf;
       a leading junk byte and byte_off = 1 prove the offset, an embedded NUL proves length-not-strlen */
    {
        unsigned char raw[] = { 0xAA, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x7f };
        Bytes f = f32_load_le((char *)raw, (int)sizeof raw, 1, 4, &a);
        assert(f32_len(f) == 4);
        assert(f32_get(f, 0) == 1.0 && f32_get(f, 1) == -2.0 && f32_get(f, 2) == 0.0 && isinf(f32_get(f, 3)) && f32_get(f, 3) > 0);
        assert(f32_len(f32_load_le((char *)raw, (int)sizeof raw, 1, 99, &a)) == 4);   /* clamps to what exists */
        assert(f32_len(f32_load_le((char *)raw, (int)sizeof raw, 99, 4, &a)) == 0 && f32_len(f32_load_le((char *)raw, (int)sizeof raw, -1, 4, &a)) == 0);
    }

    /* ---- conv1d: hand-derived tiny case. cin=1,cout=1,k=3,dil=1,x=[1,2,3,4,5] (lp=5) l=3:
       out[t] = x[t]*w0 + x[t+1]*w1 + x[t+2]*w2 with w=[1,10,100], b=0.5
       t0: 1+20+300+.5=321.5; t1: 2+30+400+.5=432.5; t2: 3+40+500+.5=543.5 */
    {
        Bytes cx = f32_alloc(5, &a), cw = f32_alloc(3, &a), cb = f32_alloc(1, &a), co = f32_alloc(3, &a);
        for (int i = 0; i < 5; i++) f32_set_(cx, i, (double)(i + 1));
        f32_set_(cw, 0, 1.0); f32_set_(cw, 1, 10.0); f32_set_(cw, 2, 100.0); f32_set_(cb, 0, 0.5);
        assert(conv1d(cx, cw, cb, co, 1, 1, 3, 1, 5, 3) == 0);
        assert(f32_get(co, 0) == 321.5 && f32_get(co, 1) == 432.5 && f32_get(co, 2) == 543.5);
        /* dilation 2: out[t] = x[t]*1 + x[t+2]*10 + x[t+4]*100 needs lp>=l+4: l=1 -> t0: 1+30+500 = 531 */
        Bytes co2 = f32_alloc(1, &a);
        assert(conv1d(cx, cw, f32_alloc(0, &a), co2, 1, 1, 3, 2, 5, 1) == 0);
        assert(f32_get(co2, 0) == 531.0);
        /* an input too short for the geometry is REFUSED whole (-1), nothing written: x=[1,2,3], k=3, l=3 needs lp>=5 */
        Bytes cs = f32_alloc(3, &a), co3 = f32_alloc(3, &a);
        for (int i = 0; i < 3; i++) f32_set_(cs, i, (double)(i + 1));
        f32_set_(co3, 0, 77.0);
        assert(conv1d(cs, cw, cb, co3, 1, 1, 3, 1, 3, 3) == -1);
        assert(f32_get(co3, 0) == 77.0);
        /* lp claims more than the buffer holds -> refused too */
        assert(conv1d(cs, cw, cb, co3, 1, 1, 3, 1, 5, 3) == -1);
    }

    /* ---- conv1d / depthwise / convT / matmul vs plain-C double references on random data ---- */
    {
        const int cin = 5, cout = 4, k = 7, dil = 3, l = 33, lp = l + (k - 1) * dil;
        Bytes x = mk(&a, cin * lp), w = mk(&a, cout * cin * k), b = mk(&a, cout), o = f32_alloc(cout * l, &a);
        conv1d(x, w, b, o, cin, cout, k, dil, lp, l);
        for (int oc = 0; oc < cout; oc++) for (int t = 0; t < l; t++) {
            double r = f32_get(b, oc);
            for (int c = 0; c < cin; c++) for (int j = 0; j < k; j++) r += f32_get(w, (oc * cin + c) * k + j) * f32_get(x, c * lp + t + j * dil);
            close_to(f32_get(o, oc * l + t), r, 1e-4, "conv1d", oc * l + t);
        }
    }
    {
        const int ch = 6, k = 3, dil = 9, l = 40, lp = l + (k - 1) * dil;
        Bytes x = mk(&a, ch * lp), w = mk(&a, ch * k), b = mk(&a, ch), o = f32_alloc(ch * l, &a);
        conv1d_depthwise(x, w, b, o, ch, k, dil, lp, l);
        for (int c = 0; c < ch; c++) for (int t = 0; t < l; t++) {
            double r = f32_get(b, c);
            for (int j = 0; j < k; j++) r += f32_get(w, c * k + j) * f32_get(x, c * lp + t + j * dil);
            close_to(f32_get(o, c * l + t), r, 1e-4, "depthwise", c * l + t);
        }
    }
    {   /* transposed conv, the three HiFi-GAN upsample shapes (k16 s8 p4), (k8 s4 p2) and an odd one (k5 s3 p1) */
        const int cfg[3][3] = { { 16, 8, 4 }, { 8, 4, 2 }, { 5, 3, 1 } };
        for (int q = 0; q < 3; q++) {
            const int k = cfg[q][0], s = cfg[q][1], p = cfg[q][2], cin = 3, cout = 2, lin = 11;
            const int lout = convt1d_out_len(lin, k, s, p);
            assert(lout == (lin - 1) * s - 2 * p + k);
            Bytes x = mk(&a, cin * lin), w = mk(&a, cin * cout * k), b = mk(&a, cout), o = f32_alloc(cout * lout, &a);
            assert(convt1d(x, w, b, o, cin, cout, k, s, p, lin) == lout);
            for (int oc = 0; oc < cout; oc++) for (int u = 0; u < lout; u++) {
                double r = f32_get(b, oc);
                for (int c = 0; c < cin; c++) for (int j = 0; j < k; j++) {
                    int num = u + p - j;                                   /* output u receives tap j from input t = (u+p-j)/s */
                    if (num >= 0 && num % s == 0 && num / s < lin) r += f32_get(w, (c * cout + oc) * k + j) * f32_get(x, c * lin + num / s);
                }
                close_to(f32_get(o, oc * lout + u), r, 1e-4, "convt", oc * lout + u);
            }
        }
    }
    {
        const int m = 7, kk = 13, n = 9;
        Bytes am = mk(&a, m * kk), bm = mk(&a, kk * n), cm = mk(&a, m * n);   /* C pre-filled with junk: matmul must overwrite */
        assert(matmul(am, bm, cm, m, kk, n) == 0);
        for (int i = 0; i < m; i++) for (int j = 0; j < n; j++) {
            double r = 0; for (int q = 0; q < kk; q++) r += f32_get(am, i * kk + q) * f32_get(bm, q * n + j);
            close_to(f32_get(cm, i * n + j), r, 1e-4, "matmul", i * n + j);
        }
    }

    /* ---- hostile shapes: refused in O(1) (-1), never fault, never spin ---- */
    {
        Bytes tiny = f32_alloc(4, &a), e = f32_alloc(0, &a);
        assert(conv1d(tiny, tiny, tiny, tiny, 0, 0, 0, 0, 0, 0) == -1);
        assert(conv1d(tiny, tiny, tiny, tiny, -3, 99999, -1, -5, 7, 100000) == -1);
        assert(conv1d(e, e, e, e, 2, 2, 2, 1, 5, 3) == -1);
        assert(conv1d(tiny, tiny, tiny, tiny, 2147483647, 2147483647, 2147483647, 2147483647, 2147483647, 2147483647) == -1);
        assert(conv1d_depthwise(tiny, tiny, tiny, tiny, 2147483647, 3, 1, 4, 4) == -1);
        assert(conv1d_depthwise(tiny, tiny, tiny, tiny, 1, 2147483647, 2147483647, 2147483647, 1) == -1);
        assert(convt1d(tiny, tiny, tiny, tiny, 1, 1, 3, 0, 1, 2) == -1);
        assert(convt1d(tiny, tiny, tiny, tiny, 1, 1, 3, -2, 5, 2) == -1);
        assert(convt1d(tiny, tiny, tiny, tiny, 1, 1, 100000, 100000, 0, 100000) == -1);
        assert(convt1d(tiny, tiny, tiny, tiny, 1, 1, 2147483647, 4096, 65536, 262144) == -1);
        assert(matmul(tiny, tiny, tiny, 100000, 100000, 100000) == -1);
        assert(matmul(tiny, tiny, tiny, 2147483647, 2147483647, 2147483647) == -1);
        assert(matmul(e, e, e, 3, 3, 3) == -1);
        /* and a legal tiny matmul still works: [1 2; 3 4] * [5 6; 7 8] = [19 22; 43 50] */
        Bytes ma = f32_alloc(4, &a), mb = f32_alloc(4, &a), mc = f32_alloc(4, &a);
        double av[4] = { 1, 2, 3, 4 }, bv[4] = { 5, 6, 7, 8 };
        for (int i = 0; i < 4; i++) { f32_set_(ma, i, av[i]); f32_set_(mb, i, bv[i]); }
        assert(matmul(ma, mb, mc, 2, 2, 2) == 0);
        assert(f32_get(mc, 0) == 19.0 && f32_get(mc, 1) == 22.0 && f32_get(mc, 2) == 43.0 && f32_get(mc, 3) == 50.0);
    }
    printf("test_tensor: all assertions passed\n");
    arena_free_all(&a);
    return 0;
}
