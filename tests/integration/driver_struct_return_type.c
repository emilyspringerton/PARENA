/* driver_struct_return_type.c -- real behavioral verification for
 * selfhost/emit.prn's new struct-return-type + non-get-field-nested-
 * call-as-a-get-field-target support (2026-08-28), closing two of
 * this same day's own remaining still-open gaps together (they're
 * naturally paired: a function returning a struct BY VALUE is the
 * real, motivating reason a get-field target needs to accept a
 * plain-call in the first place). Links against real generated C for:
 *   (defstruct Point (x : I32) (y : I32))
 *   (defn identity-point [(p : Point)] : Point
 *     p)
 *   (defn point-x-via-identity [(p : Point)] : I32
 *     (get-field (identity-point p) :x))
 * and proves the real function-returned struct genuinely round-trips
 * through a real get-field read, on multiple real inputs including a
 * negative value, not just that gcc accepts the generated text.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    int x;
    int y;
} Point;

extern char *point_x_via_identity(Point p);

int main(void) {
    Point p1 = {55, 9};
    Point p2 = {-3, 0};

    int r1 = (int)(intptr_t)point_x_via_identity(p1);
    int r2 = (int)(intptr_t)point_x_via_identity(p2);

    printf("point_x_via_identity(p1) = %d (expected 55), point_x_via_identity(p2) = %d "
           "(expected -3)\n",
           r1, r2);
    assert(r1 == 55);
    assert(r2 == -3);

    printf("driver_struct_return_type: OK -- the real struct value, returned BY VALUE from "
           "identity-point and read straight back through a real get-field call on the call's own "
           "result, genuinely computed the correct value on every real input, not just compiled "
           "clean.\n");
    return 0;
}
