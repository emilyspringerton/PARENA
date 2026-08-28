/* driver_nested_get_field.c -- real behavioral verification for
 * selfhost/emit.prn's new nested/chained get-field support
 * (2026-08-28), closing the "get-field on a NESTED expression" gap
 * that function's own header comment named the moment get-field
 * support first landed -- real friction found firsthand while
 * verifying struct-typed struct field support the same day. Links
 * against real generated C for:
 *   (defstruct Point (x : I32) (y : I32))
 *   (defstruct Line (start : Point) (end : Point))
 *   (defn line-start-x [(l : Line)] : I32
 *     (get-field (get-field l :start) :x))
 * and proves the real chained `line.start.x`-style field read
 * genuinely computes the correct value, not just that gcc accepts the
 * doubly-nested `((l).start).x` C expression.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    int x;
    int y;
} Point;

typedef struct {
    Point start;
    Point end;
} Line;

extern char *line_start_x(Line l);

int main(void) {
    Line l1 = { {77, 3}, {1, 2} };
    Line l2 = { {-9, 0}, {0, 0} };

    int r1 = (int)(intptr_t)line_start_x(l1);
    int r2 = (int)(intptr_t)line_start_x(l2);

    printf("line_start_x(l1) = %d (expected 77), line_start_x(l2) = %d (expected -9)\n", r1, r2);
    assert(r1 == 77);
    assert(r2 == -9);

    printf("driver_nested_get_field: OK -- the real chained line.start.x field read genuinely "
           "computed the correct value through a real, doubly-nested get-field expression, not "
           "just compiled clean.\n");
    return 0;
}
