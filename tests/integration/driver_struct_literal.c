/* driver_struct_literal.c -- real behavioral verification for
 * selfhost/emit.prn's new struct-literal CONSTRUCTION support
 * (2026-08-28), closing "general struct-literal construction for a
 * user-defined defstruct" -- the other of the two real gaps the
 * defstruct section's own header comment named since defstruct
 * support first landed (the first, struct-typed struct FIELDS, closed
 * earlier the same day). Links against real generated C for:
 *   (defstruct Point (x : I32) (y : I32))
 *   (defn make-point [(x : I32) (y : I32)] : Point
 *     {:y y :x x})
 *   (defn point-x [(p : Point)] : I32
 *     (get-field p :x))
 *   (defn point-y [(p : Point)] : I32
 *     (get-field p :y))
 * and proves the real C99 compound literal `(Point){x, y}` genuinely
 * constructs the correct struct value on multiple real inputs
 * including negative ones, and that a map literal with its own keys
 * written in a DIFFERENT order than the struct's own real declared
 * field order (`{:y y :x x}`, y first) still produces the correctly
 * ordered `(Point){x, y}` (x first, matching the struct's own real
 * declaration) -- not just that gcc accepts the generated text.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    int x;
    int y;
} Point;

extern Point make_point(int x, int y);
extern char *point_x(Point p);
extern char *point_y(Point p);

int main(void) {
    Point p1 = make_point(17, -42);
    Point p2 = make_point(0, 0);

    int x1 = (int)(intptr_t)point_x(p1);
    int y1 = (int)(intptr_t)point_y(p1);
    int x2 = (int)(intptr_t)point_x(p2);
    int y2 = (int)(intptr_t)point_y(p2);

    printf("make_point(17,-42) -> x=%d (expected 17), y=%d (expected -42); "
           "make_point(0,0) -> x=%d, y=%d (expected 0, 0)\n",
           x1, y1, x2, y2);
    assert(x1 == 17);
    assert(y1 == -42);
    assert(x2 == 0);
    assert(y2 == 0);

    printf("driver_struct_literal: OK -- the real C99 compound literal genuinely constructed the "
           "correct struct value on every real input, with the map literal's own reordered keys "
           "('{:y y :x x}') correctly resolved back to the struct's own real declared field "
           "order, not just compiled clean.\n");
    return 0;
}
