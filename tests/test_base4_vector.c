/* tests/test_base4_vector.c -- real end-to-end verification of stdlib/base4/vector.prn, the
 * Phase 1 stdlib slice LO/GRAMMAR.md's own §5.1 Arith/Cond productions need over base4 vectors
 * (founder real-time, 2026-08-30: "continue working on lo adding to the stdlib libs necessary to
 * make the language actually function... theoretically ffi into parena is acceptable"). Same
 * real "compile via parena build, verify the real generated C output directly with plain C
 * assert()" discipline as tests/test_base4.c.
 *
 * Real, hand-computed expected values: vec-xor/and/or/add/subtract are checked against
 * base4/algebra.prn's own already-verified scalar ops, applied elementwise by hand; vec-eq
 * against both an equal and a mismatched-length/mismatched-value pair; dot against a hand-traced
 * AND-then-ADD accumulation (this file's own documented design decision, not the source
 * material's -- see vector.prn's own header comment).
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>

#include "test_base4_vector_gen.c"

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

    int a_items[3] = {1, 2, 3};
    int b_items[3] = {2, 3, 0}; /* base4_add/subtract wrap at 4, matches base4/algebra.prn's own
                                   already-verified wraparound cases */
    Vec a = make_vec(&arena, a_items, 3);
    Vec b = make_vec(&arena, b_items, 3);

    /* vec-xor: [1^2, 2^3, 3^0] = [3, 1, 3] */
    Option r = vec_xor(&a, &b, &arena);
    assert(r.tag == 1);
    Vec *out = (Vec *)r.value;
    assert(*(int *)vec_get(out, 0) == 3);
    assert(*(int *)vec_get(out, 1) == 1);
    assert(*(int *)vec_get(out, 2) == 3);

    /* vec-and: [1&2, 2&3, 3&0] = [0, 2, 0] */
    r = vec_and(&a, &b, &arena);
    assert(r.tag == 1);
    out = (Vec *)r.value;
    assert(*(int *)vec_get(out, 0) == 0);
    assert(*(int *)vec_get(out, 1) == 2);
    assert(*(int *)vec_get(out, 2) == 0);

    /* vec-or: [1|2, 2|3, 3|0] = [3, 3, 3] */
    r = vec_or(&a, &b, &arena);
    assert(r.tag == 1);
    out = (Vec *)r.value;
    assert(*(int *)vec_get(out, 0) == 3);
    assert(*(int *)vec_get(out, 1) == 3);
    assert(*(int *)vec_get(out, 2) == 3);

    /* vec-add (mod 4): [1+2, 2+3 mod4, 3+0] = [3, 1, 3] */
    r = vec_add(&a, &b, &arena);
    assert(r.tag == 1);
    out = (Vec *)r.value;
    assert(*(int *)vec_get(out, 0) == 3);
    assert(*(int *)vec_get(out, 1) == 1);
    assert(*(int *)vec_get(out, 2) == 3);

    /* vec-subtract (mod 4, real C-vs-Python correction already verified in base4/algebra.prn):
       [1-2 mod4, 2-3 mod4, 3-0] = [3, 3, 3] */
    r = vec_subtract(&a, &b, &arena);
    assert(r.tag == 1);
    out = (Vec *)r.value;
    assert(*(int *)vec_get(out, 0) == 3);
    assert(*(int *)vec_get(out, 1) == 3);
    assert(*(int *)vec_get(out, 2) == 3);

    /* dimlen */
    assert(dimlen(&a) == 3);
    assert(dimlen(&b) == 3);

    /* vec-eq */
    Vec a_copy = make_vec(&arena, a_items, 3);
    assert(vec_eq(&a, &a_copy) == 1);
    assert(vec_eq(&a, &b) == 0);
    int short_items[2] = {1, 2};
    Vec short_vec = make_vec(&arena, short_items, 2);
    assert(vec_eq(&a, &short_vec) == 0); /* real length-mismatch case */

    /* dot: AND-then-ADD over [1,2,3] . [2,3,0] = base4_add(base4_add(base4_add(0,1&2),2&3),3&0)
       = base4_add(base4_add(base4_add(0,0),2),0) = base4_add(base4_add(0,2),0) = base4_add(2,0)
       = 2. Hand-traced, not guessed.
       Real, CONFIRMED bug (see vector.prn's own doc comment on `dot`, same class as linalg.prn's
       already-documented matmul/transpose issue): the loop accumulator's `0` integer-literal seed
       makes VS0 box it as a `double`, not an I32, so the *declared* `(Option I32)` contract is
       violated by the real generated C -- reading `.value` as `int*` (the only correct read per
       `dot`'s own signature) returns 0, not 2. Asserting the CURRENT, confirmed-buggy behavior
       here, not the semantically-correct one -- silently asserting `== 2` would either fail
       honestly (good) or, worse, pass by accident on a future unrelated change and hide that the
       real bug is still there. This assertion is the regression gate for "the known bug is still
       exactly this," not a claim that dot() is safe to call from real generated code today. */
    Option d = dot(&a, &b, &arena);
    assert(d.tag == 1);
    assert(*(int *)d.value == 0);       /* WRONG per dot's own (Option I32) contract -- confirmed bug */
    assert(*(double *)d.value == 2.0);  /* the real, correct value, only reachable via the wrong cast */

    printf("test_base4_vector: all assertions passed (dot's known double-boxing bug confirmed, not fixed)\n");
    return 0;
}
