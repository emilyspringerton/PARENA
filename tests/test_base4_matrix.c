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

    /* Real, deliberate UPDATE (2026-09-08, EMILY/BACKLOG.md's own "loop-variable I32 boxing bug"
       -- the real, cross-cutting fix, not just this file's own narrow workaround): this test
       used to assert the CONFIRMED-buggy behavior (the accumulator boxed as a double, read back
       correctly only via the WRONG double* cast, garbage via the correct-per-declared-type int*
       cast) as a real regression gate. The real fix landed: NODE_NUMBER now distinguishes a
       whole-number literal from a decimal one, and a loop accumulator seeded from one (`acc`,
       here reassigned via `base4-add`, itself a real, known I32-returning function) now
       correctly declares as `int` and boxes via `vec_box_i32` -- confirmed live in the generated
       C (`int acc = 0;` / `vec_box_i32(&(out), cell)`). The correct-per-declared-type `int*`
       cast now returns the real, hand-traced values directly. */
    int cell_00 = *(int *)vec_get(&C.data, 0);
    int cell_01 = *(int *)vec_get(&C.data, 1);
    int cell_10 = *(int *)vec_get(&C.data, 2);
    int cell_11 = *(int *)vec_get(&C.data, 3);
    printf("matmul: [[%d,%d],[%d,%d]] (expected [[2,3],[0,1]])\n", cell_00, cell_01, cell_10, cell_11);
    assert(cell_00 == 2);
    assert(cell_01 == 3);
    assert(cell_10 == 0);
    assert(cell_11 == 1);

    printf("test_base4_matrix: all assertions passed (matmul's own real, correct product, "
           "loop-variable I32 boxing bug now fixed)\n");
    return 0;
}
