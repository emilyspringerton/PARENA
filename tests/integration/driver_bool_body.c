/* driver_bool_body.c -- real behavioral verification for selfhost/
 * emit.prn's new top-level (tail-position AND let-value) or/and/not
 * support (2026-08-28): compiling-clean alone doesn't prove the real
 * boolean logic actually branches correctly, only that gcc accepts the
 * types -- this links against the real generated C for
 *   (defstruct Point (x : I32) (y : I32))
 *   (defn is-origin? [(p : Point)] : Bool
 *     (and (= (get-field p :x) 0) (= (get-field p :y) 0)))
 *   (defn is-boring [(n : I32)] : Bool
 *     (let [b (or (= n 0) (= n 1))]
 *       b))
 * and actually calls both across every real branch (an `and` whose
 * whole body IS the defn, composing get-field inside a comparison
 * inside the and; an `or` used as a LET-BINDING value, not tail
 * position), unboxing each result the same way this feature's own
 * header comment documents any real caller must
 * (`(int)(intptr_t)result`).
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

typedef struct { int x; int y; } Point;

extern char *is_origin_(Point p);
extern char *is_boring(int n);

int main(void) {
    Point origin = {0, 0};
    Point off_x = {1, 0};
    Point off_y = {0, 1};

    int r_origin = (int)(intptr_t)is_origin_(origin);
    int r_off_x = (int)(intptr_t)is_origin_(off_x);
    int r_off_y = (int)(intptr_t)is_origin_(off_y);
    printf("is_origin_(0,0)=%d (expected 1), is_origin_(1,0)=%d (expected 0), "
           "is_origin_(0,1)=%d (expected 0)\n", r_origin, r_off_x, r_off_y);
    assert(r_origin == 1);
    assert(r_off_x == 0);
    assert(r_off_y == 0);

    int b0 = (int)(intptr_t)is_boring(0);
    int b1 = (int)(intptr_t)is_boring(1);
    int b5 = (int)(intptr_t)is_boring(5);
    printf("is_boring(0)=%d (expected 1), is_boring(1)=%d (expected 1), "
           "is_boring(5)=%d (expected 0)\n", b0, b1, b5);
    assert(b0 == 1);
    assert(b1 == 1);
    assert(b5 == 0);

    printf("driver_bool_body: OK -- is-origin?'s own real top-level 'and' body (composing "
           "get-field inside a comparison) and is-boring's own real 'or' let-value both branched "
           "correctly on every real input, not just compiled clean.\n");
    return 0;
}
