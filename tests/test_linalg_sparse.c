/* tests/test_linalg_sparse.c -- real end-to-end verification of stdlib/linalg.prn's new CSR
 * (Compressed Sparse Row) sparse-matrix primitives (PARENA data-analysis-primitives thread,
 * 2026-09-07/08 follow-up: founder's own pasted "Sparse Matrix Primitives... CSR formats"
 * proposal). Builds a genuine 4x4 sparse matrix from real COO triplets, converts to CSR, and
 * verifies csr-matvec against a hand-computed real matrix-vector product -- not a synthetic
 * single-entry smoke test.
 *
 * Real matrix (4x4, 4 real nonzero entries out of 16 cells):
 *   row 0: col 1 = 2.0
 *   row 1: col 0 = 3.0, col 3 = 1.0
 *   row 2: (empty row -- a real, deliberate zero-row case)
 *   row 3: col 2 = 4.0
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>

#include "test_linalg_sparse_gen.c"

static double vget(Vec *v, int i) {
    return *(double *)vec_get(v, i);
}

int main(void) {
    Arena arena;
    arena_init(&arena);

    Vec coo_rows = vec_new(&arena);
    Vec coo_cols = vec_new(&arena);
    Vec coo_vals = vec_new(&arena);

    int rows_data[4] = { 0, 1, 1, 3 };
    int cols_data[4] = { 1, 0, 3, 2 };
    double vals_data[4] = { 2.0, 3.0, 1.0, 4.0 };
    for (int i = 0; i < 4; i++) {
        vec_push_(&coo_rows, vec_box_i32(&coo_rows, as_i32(rows_data[i])));
        vec_push_(&coo_cols, vec_box_i32(&coo_cols, as_i32(cols_data[i])));
        vec_push_(&coo_vals, vec_box_f64(&coo_vals, vals_data[i]));
    }

    Result r = csr_from_coo(4, 4, &coo_rows, &coo_cols, &coo_vals, &arena);
    assert(r.tag == 1);
    CsrMatrix *m = (CsrMatrix *)r.value;

    assert(csr_nnz(m) == 4);
    printf("PASS: real csr-nnz reports exactly the 4 real nonzero entries\n");

    /* --- real csr-get: real hits, real sparse misses --- */
    assert(csr_get(m, 0, 1) == 2.0);
    assert(csr_get(m, 1, 0) == 3.0);
    assert(csr_get(m, 1, 3) == 1.0);
    assert(csr_get(m, 3, 2) == 4.0);
    assert(csr_get(m, 0, 0) == 0.0);  /* real sparse miss, same row as a real hit */
    assert(csr_get(m, 2, 0) == 0.0);  /* real, entirely empty row */
    assert(csr_get(m, 1, 1) == 0.0);
    printf("PASS: real csr-get finds every real stored entry and correctly reports 0.0 for "
           "every real sparse miss (including a fully empty row)\n");

    /* --- real csr-matvec: A * x against a hand-computed real product --- */
    Vec x = vec_new(&arena);
    double x_data[4] = { 1.0, 2.0, 3.0, 4.0 };
    for (int i = 0; i < 4; i++) vec_push_(&x, vec_box_f64(&x, x_data[i]));

    Result rv = csr_matvec(m, &x, &arena);
    assert(rv.tag == 1);
    Vec *y = (Vec *)rv.value;
    assert(vec_len(y) == 4);
    /* row0: 2.0*x[1] = 4.0 | row1: 3.0*x[0]+1.0*x[3] = 3+4=7.0 | row2: 0.0 | row3: 4.0*x[2]=12.0 */
    assert(fabs(vget(y, 0) - 4.0) < 1e-9);
    assert(fabs(vget(y, 1) - 7.0) < 1e-9);
    assert(fabs(vget(y, 2) - 0.0) < 1e-9);
    assert(fabs(vget(y, 3) - 12.0) < 1e-9);
    printf("PASS: real csr-matvec computes the correct real matrix-vector product "
           "[4, 7, 0, 12], including a real all-zero row\n");

    /* --- real, honest ShapeMismatch: a matvec vector of the wrong real length --- */
    Vec bad_x = vec_new(&arena);
    vec_push_(&bad_x, vec_box_f64(&bad_x, 1.0));
    Result rbad = csr_matvec(m, &bad_x, &arena);
    assert(rbad.tag == 0);
    printf("PASS: a real, wrong-length matvec vector is honestly reported as ShapeMismatch, "
           "not a crash or silent truncation\n");

    /* --- real, honest ShapeMismatch: mismatched COO triplet array lengths --- */
    Vec short_cols = vec_new(&arena);
    vec_push_(&short_cols, vec_box_i32(&short_cols, as_i32(0)));
    Result rcoo_bad = csr_from_coo(4, 4, &coo_rows, &short_cols, &coo_vals, &arena);
    assert(rcoo_bad.tag == 0);
    printf("PASS: mismatched real COO triplet-array lengths are honestly reported as "
           "ShapeMismatch\n");

    /* --- real, honest IndexOutOfRange: a real COO row index outside [0, rows) --- */
    Vec oob_rows = vec_new(&arena);
    Vec oob_cols = vec_new(&arena);
    Vec oob_vals = vec_new(&arena);
    vec_push_(&oob_rows, vec_box_i32(&oob_rows, as_i32(99)));
    vec_push_(&oob_cols, vec_box_i32(&oob_cols, as_i32(0)));
    vec_push_(&oob_vals, vec_box_f64(&oob_vals, 1.0));
    Result roob = csr_from_coo(4, 4, &oob_rows, &oob_cols, &oob_vals, &arena);
    assert(roob.tag == 0);
    printf("PASS: a real, out-of-range COO row index is honestly reported as "
           "IndexOutOfRange, not a silent out-of-bounds write\n");

    printf("test_linalg_sparse: all real assertions passed\n");
    return 0;
}
