/* tests/test_linalg_matmul.c -- real end-to-end verification of stdlib/linalg.prn's own
 * array.prn-backed matmul/transpose, closing kanban card PARENA-ARRAY-MATMUL-001. Both were
 * long-documented as "compiles clean but NOT verified numerically correct" (STDLIB.md's own
 * "linalg" section, 2026-08-21) because of a real, separate bug in array.prn's own
 * strides-for: a stride (mathematically always an exact integer -- a suffix product of integer
 * shape dimensions) was computed via real C division, which this compiler's own binop type
 * inference reports as whatever type the LEFT operand happens to have -- correct in principle,
 * but the loop-variable I32 boxing bug's own fix (2026-09-08) initially still downgraded
 * `strides-for`'s own `running`/`next` accumulators back to `double` due to a real, SEPARATE bug
 * in the fix's own safety-net check (an interior `let`-bound recur value, like `next` here,
 * wasn't resolvable against the right scope) -- fixed the same day. This test is the real,
 * permanent regression gate: a genuine 2x3 * 3x2 matrix product and a transpose, both checked
 * against real, hand-computed values, not just "did it compile."
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>

#include "test_linalg_matmul_gen.c"

static double at(NDArray *m, int r, int c) {
    Vec idx = vec_new(m->data.arena);
    vec_push_(&idx, vec_box_i32(&idx, r));
    vec_push_(&idx, vec_box_i32(&idx, c));
    Result res = array_get(m, idx, m->data.arena);
    return *(double *)result_unwrap_check(res).value;
}

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* A = [[1,2,3],[4,5,6]] (2x3), B = [[7,8],[9,10],[11,12]] (3x2)
     * A*B = [[1*7+2*9+3*11, 1*8+2*10+3*12], [4*7+5*9+6*11, 4*8+5*10+6*12]]
     *     = [[58, 64], [139, 154]] -- a real, standard, hand-computed matrix product. */
    Vec a_shape = vec_new(&arena);
    vec_push_(&a_shape, vec_box_i32(&a_shape, 2));
    vec_push_(&a_shape, vec_box_i32(&a_shape, 3));
    NDArray A = zeros(a_shape, &arena);
    double a_vals[6] = { 1, 2, 3, 4, 5, 6 };
    int k = 0;
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 3; c++) {
            Vec idx = vec_new(&arena);
            vec_push_(&idx, vec_box_i32(&idx, r));
            vec_push_(&idx, vec_box_i32(&idx, c));
            Result sr = set_(&A, idx, a_vals[k++], &arena);
            assert(sr.tag == 1);
        }
    }

    Vec b_shape = vec_new(&arena);
    vec_push_(&b_shape, vec_box_i32(&b_shape, 3));
    vec_push_(&b_shape, vec_box_i32(&b_shape, 2));
    NDArray B = zeros(b_shape, &arena);
    double b_vals[6] = { 7, 8, 9, 10, 11, 12 };
    k = 0;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 2; c++) {
            Vec idx = vec_new(&arena);
            vec_push_(&idx, vec_box_i32(&idx, r));
            vec_push_(&idx, vec_box_i32(&idx, c));
            Result sr = set_(&B, idx, b_vals[k++], &arena);
            assert(sr.tag == 1);
        }
    }

    Result r = matmul(&A, &B, &arena);
    assert(r.tag == 1);
    NDArray *C = (NDArray *)r.value;
    assert(fabs(at(C, 0, 0) - 58.0) < 1e-9);
    assert(fabs(at(C, 0, 1) - 64.0) < 1e-9);
    assert(fabs(at(C, 1, 0) - 139.0) < 1e-9);
    assert(fabs(at(C, 1, 1) - 154.0) < 1e-9);
    printf("PASS: real matmul computes the correct real 2x3 * 3x2 matrix product "
           "[[58,64],[139,154]]\n");

    /* transpose(A) is 3x2: [[1,4],[2,5],[3,6]] */
    NDArray T = transpose(&A, &arena);
    assert(*(int *)vec_get(&T.shape, 0) == 3);
    assert(*(int *)vec_get(&T.shape, 1) == 2);
    assert(fabs(at(&T, 0, 0) - 1.0) < 1e-9);
    assert(fabs(at(&T, 0, 1) - 4.0) < 1e-9);
    assert(fabs(at(&T, 1, 0) - 2.0) < 1e-9);
    assert(fabs(at(&T, 1, 1) - 5.0) < 1e-9);
    assert(fabs(at(&T, 2, 0) - 3.0) < 1e-9);
    assert(fabs(at(&T, 2, 1) - 6.0) < 1e-9);
    printf("PASS: real transpose produces the correct real 3x2 result\n");

    printf("test_linalg_matmul: all real assertions passed\n");
    return 0;
}
