/* driver_let_get_field.c -- real behavioral verification for
 * selfhost/emit.prn's new get-field-as-a-let-value support
 * (2026-08-28), closing a real, honest, until-now-unsupported gap
 * confirmed live via a direct probe: `(let [n (get-field p :x)] n)`
 * used to fall straight to the honest #error fallback,
 * get-field-shaped? never having been wired into emit-let-value at
 * all. Links against real generated C for:
 *   (defstruct Point (x : I32) (y : I32))
 *   (defn point-x-via-let [(p : Point)] : I32
 *     (let [n (get-field p :x)] n))
 * and proves the real scalar struct-field read, boxed and passed
 * through a real let-binding, genuinely computes the correct value on
 * multiple real inputs including a negative one, not just that gcc
 * accepts the generated text.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    int x;
    int y;
} Point;

extern char *point_x_via_let(Point p);

int main(void) {
    Point p1 = {41, 2};
    Point p2 = {-7, 0};

    int r1 = (int)(intptr_t)point_x_via_let(p1);
    int r2 = (int)(intptr_t)point_x_via_let(p2);

    printf("point_x_via_let(p1) = %d (expected 41), point_x_via_let(p2) = %d (expected -7)\n", r1,
           r2);
    assert(r1 == 41);
    assert(r2 == -7);

    printf("driver_let_get_field: OK -- the real scalar struct-field read, boxed and passed "
           "through a real let-binding, genuinely computed the correct value on every real "
           "input, not just compiled clean.\n");
    return 0;
}
