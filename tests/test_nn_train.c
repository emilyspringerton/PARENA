/* tests/test_nn_train.c -- end-to-end verification of stdlib/nn_train.prn (the backward pass
 * stdlib/nn.prn never had). Three real checks, not "did it compile":
 *   1. Gradient check: for every parameter of a 3-input, 5-hidden net on a non-trivial sample,
 *      the analytic gradient sgd-step! applies ((old - new) / lr) matches a central finite
 *      difference of sample-loss to 1e-6 relative error. This is what proves backprop is right.
 *   2. XOR: a 2-4-1 net trained with train-epoch! drives mean BCE from ~0.69 to < 0.05 and
 *      classifies all four XOR points correctly -- a problem no linear model can solve.
 *   3. Determinism: two runs from the same seed produce bit-identical parameters.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "test_nn_train_gen.c"

static double get(Vec *v, int i) { return *(double *)vec_get(v, i); }
static void put(Vec *v, int i, double x) { vec_set_at_(v, i, vec_box_f64(v, x)); }
static void push(Vec *v, double x) { vec_push_(v, vec_box_f64(v, x)); }

static Vec copy_vec(Arena *a, Vec *src) {
    Vec out = vec_new(a);
    for (int i = 0; i < vec_len(src); i++) push(&out, get(src, i));
    return out;
}

static void gradient_check(Arena *a) {
    const int n_in = 3, h = 5;
    Vec xs = vec_new(a), ys = vec_new(a);
    push(&xs, 0.7); push(&xs, -1.2); push(&xs, 0.4);
    push(&ys, 1.0);
    Vec p = vec_new(a);
    init_params_(&p, n_in, h, 42);
    assert(vec_len(&p) == param_count(n_in, h));
    assert(vec_len(&p) == 3 * 5 + 2 * 5 + 1);

    const double lr = 1.0, eps = 1e-5;
    Vec stepped = copy_vec(a, &p);
    double loss_before = sgd_step_(&stepped, &xs, &ys, 0, n_in, h, lr);
    assert(fabs(loss_before - sample_loss(&p, &xs, &ys, 0, n_in, h)) < 1e-12);

    double worst = 0.0;
    for (int i = 0; i < vec_len(&p); i++) {
        double analytic = (get(&p, i) - get(&stepped, i)) / lr;
        Vec plus = copy_vec(a, &p), minus = copy_vec(a, &p);
        put(&plus, i, get(&p, i) + eps);
        put(&minus, i, get(&p, i) - eps);
        double numeric = (sample_loss(&plus, &xs, &ys, 0, n_in, h) -
                          sample_loss(&minus, &xs, &ys, 0, n_in, h)) / (2 * eps);
        double denom = fmax(1e-8, fabs(analytic) + fabs(numeric));
        double rel = fabs(analytic - numeric) / denom;
        if (rel > worst) worst = rel;
        if (rel > 1e-6) {
            fprintf(stderr, "param %d: analytic %.10g numeric %.10g rel %.3g\n", i, analytic,
                    numeric, rel);
        }
        assert(rel < 1e-6);
    }
    printf("PASS: gradient check -- all %d analytic gradients match finite differences "
           "(worst rel err %.2e)\n", vec_len(&p), worst);
}

static Vec train_xor(Arena *a, Vec *xs, Vec *ys, double *first, double *last) {
    const int n_in = 2, h = 4;
    Vec p = vec_new(a);
    init_params_(&p, n_in, h, 7);
    *first = dataset_loss(&p, xs, ys, 4, n_in, h);
    for (int e = 0; e < 3000; e++) train_epoch_(&p, xs, ys, 4, n_in, h, 0.5);
    *last = dataset_loss(&p, xs, ys, 4, n_in, h);
    return p;
}

static void xor_training(Arena *a) {
    const double X[4][2] = { {0, 0}, {0, 1}, {1, 0}, {1, 1} };
    const double Y[4] = { 0, 1, 1, 0 };
    Vec xs = vec_new(a), ys = vec_new(a);
    for (int r = 0; r < 4; r++) { push(&xs, X[r][0]); push(&xs, X[r][1]); push(&ys, Y[r]); }

    double first, last;
    Vec p = train_xor(a, &xs, &ys, &first, &last);
    printf("XOR: mean BCE %.4f -> %.4f after 3000 epochs\n", first, last);
    assert(first > 0.5);
    assert(last < 0.05);
    for (int r = 0; r < 4; r++) {
        double out = predict(&p, &xs, r, 2, 4);
        printf("  xor(%g,%g) = %.4f (want %g)\n", X[r][0], X[r][1], out, Y[r]);
        assert((out > 0.5) == (Y[r] > 0.5));
    }
    printf("PASS: XOR learned -- all 4 points classified correctly\n");

    double first2, last2;
    Vec p2 = train_xor(a, &xs, &ys, &first2, &last2);
    for (int i = 0; i < vec_len(&p); i++) assert(get(&p, i) == get(&p2, i));
    printf("PASS: training is deterministic -- same seed, bit-identical parameters\n");
}

int main(void) {
    Arena arena;
    arena_init(&arena);
    gradient_check(&arena);
    xor_training(&arena);
    printf("test_nn_train: all assertions passed\n");
    return 0;
}
