/* driver_struct_field_tail.c -- real behavioral verification for
 * selfhost/emit.prn's new struct-typed-field-as-a-tail-position-
 * return support (2026-08-28), closing the one gap struct-return-type
 * support's own landing explicitly left open the same day:
 * emit-form's own get-field-shaped? dispatch always boxes its result
 * via emit-i32-boxed, correct only for a scalar field -- boxing a
 * real struct VALUE that way isn't even valid C (a struct can't be
 * cast through intptr_t). Links against real generated C for:
 *   (defstruct Point (x : I32) (y : I32))
 *   (defstruct Line (start : Point) (end : Point))
 *   (defn line-start [(l : Line)] : Point
 *     (get-field l :start))
 * and proves the real, unboxed `return (l).start;` genuinely returns
 * the correct, complete struct value (both fields) at runtime, not
 * just that gcc accepts the direct struct return.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
    int x;
    int y;
} Point;

typedef struct {
    Point start;
    Point end;
} Line;

extern Point line_start(Line l);

int main(void) {
    Line l1 = { {12, 34}, {56, 78} };
    Line l2 = { {-1, -2}, {0, 0} };

    Point p1 = line_start(l1);
    Point p2 = line_start(l2);

    printf("line_start(l1) = {%d, %d} (expected {12, 34}), line_start(l2) = {%d, %d} "
           "(expected {-1, -2})\n",
           p1.x, p1.y, p2.x, p2.y);
    assert(p1.x == 12);
    assert(p1.y == 34);
    assert(p2.x == -1);
    assert(p2.y == -2);

    printf("driver_struct_field_tail: OK -- the real, unboxed struct-typed field return "
           "genuinely produced the correct, complete struct value (both fields) on every real "
           "input, not just compiled clean.\n");
    return 0;
}
