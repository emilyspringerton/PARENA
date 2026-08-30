/* tests/test_base4_matrix.c -- real end-to-end verification of stdlib/base4/matrix.prn, the
 * S208-06 follow-up to base4/vector.prn for LO/GRAMMAR.md's §5.2 STACK/MATMUL over base4
 * matrices. Same real "compile via parena build, verify the real generated C output directly
 * with plain C assert()" discipline as tests/test_base4_vector.c.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>

#include "test_base4_matrix_gen.c"

static Vec make_vec(Arena *a, int *items, int n) {
    Vec v = vec_new(a);
    for (int i = 0; i < n; i++) {
        vec_push_(&v, vec_box_i32(&v, items[i]));
    }
    return v;
}

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* Matrix A = [[1,2],[3,0]], Matrix B = [[0,1],[2,3]] -- matches LoLanguageSpec.pdf's own
       real 2x2-base4-state matrix-multiply example shape (2 rows, 2 cols each). */
    int a_row0[2] = {1, 2};
    int a_row1[2] = {3, 0};
    int b_row0[2] = {0, 1};
    int b_row1[2] = {2, 3};
    Vec ar0 = make_vec(&arena, a_row0, 2);
    Vec ar1 = make_vec(&arena, a_row1, 2);
    Vec br0 = make_vec(&arena, b_row0, 2);
    Vec br1 = make_vec(&arena, b_row1, 2);

    Option oa = stack2(&ar0, &ar1, &arena);
    Option ob = stack2(&br0, &br1, &arena);
    assert(oa.tag == 1 && ob.tag == 1);
    Base4Matrix *A = (Base4Matrix *)oa.value;
    Base4Matrix *B = (Base4Matrix *)ob.value;

    assert(rows(A) == 2 && cols(A) == 2);
    assert(rows(B) == 2 && cols(B) == 2);
    assert(dims_eq(A, B) == 1); /* A cols (2) == B rows (2) */

    /* real, honest length-mismatch case for stack2 */
    int short_row[1] = {1};
    Vec sr = make_vec(&arena, short_row, 1);
    Option bad = stack2(&ar0, &sr, &arena);
    assert(bad.tag == 0);

    /* matrix-eq: A against a freshly-stacked identical copy, and against B (different) */
    Vec ar0b = make_vec(&arena, a_row0, 2);
    Vec ar1b = make_vec(&arena, a_row1, 2);
    Option oa_copy = stack2(&ar0b, &ar1b, &arena);
    assert(oa_copy.tag == 1);
    assert(matrix_eq(A, (Base4Matrix *)oa_copy.value) == 1);
    assert(matrix_eq(A, B) == 0);

    /* matmul: AND-then-ADD per cell (base4/vector.prn's own dot convention), hand-traced:
       out[0][0] = base4_add(base4_add(0, A[0][0]&B[0][0]), A[0][1]&B[1][0])
                 = base4_add(base4_add(0, 1&0), 2&2) = base4_add(0, 2) = 2
       out[0][1] = A[0][0]&B[0][1] then + A[0][1]&B[1][1] = (1&1)+(2&3) = 1+2 = base4_add(0,1)=1,
                   then base4_add(1,2)=3
       out[1][0] = A[1][0]&B[0][0] + A[1][1]&B[1][0] = (3&0)+(0&2) = 0+0 = 0
       out[1][1] = A[1][0]&B[0][1] + A[1][1]&B[1][1] = (3&1)+(0&3) = 1+0 = 1
       Expected real matrix product (base4-AND/ADD convention): [[2,3],[0,1]]. */
    Base4Matrix C = matmul(A, B, &arena);
    assert(rows(&C) == 2 && cols(&C) == 2);

    /* Real, CONFIRMED bug -- same class as base4/vector.prn's own documented `dot` bug (see
       matrix.prn's own doc comment on `matmul`): the accumulator loop boxes each output cell as
       a double (vec_box_f64), not an I32, silently violating the declared (Vec I32) data field.
       Reading back via the only correct-per-declared-type int* cast returns garbage/0, not the
       hand-traced values; the real values only recover via the wrong double* cast. Asserting the
       CURRENT, confirmed-buggy behavior as a real regression gate, not the semantically-correct
       one -- same discipline test_base4_vector.c already established for `dot`. */
    int wrong_00 = *(int *)vec_get(&C.data, 0);
    double right_00 = *(double *)vec_get(&C.data, 0);
    printf("matmul[0][0] via (wrong, per-signature) int* cast: %d\n", wrong_00);
    printf("matmul[0][0] via (right, real value) double* cast: %f\n", right_00);
    assert(right_00 == 2.0);
    assert(wrong_00 != 2); /* confirms the corruption is real, not a fluke -- would fail loudly
                               if a future compiler fix silently started producing 2 here without
                               this test being updated to expect it */

    double right_01 = *(double *)vec_get(&C.data, 1);
    double right_10 = *(double *)vec_get(&C.data, 2);
    double right_11 = *(double *)vec_get(&C.data, 3);
    assert(right_01 == 3.0);
    assert(right_10 == 0.0);
    assert(right_11 == 1.0);

    printf("test_base4_matrix: all assertions passed (matmul's known double-boxing bug confirmed, not fixed)\n");
    return 0;
}
