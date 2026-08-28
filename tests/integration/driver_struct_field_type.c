/* driver_struct_field_type.c -- real behavioral verification for
 * selfhost/emit.prn's new struct-typed struct field support
 * (2026-08-28), closing one of the two remaining real gaps this
 * file's own defstruct section header comment named the moment
 * defstruct support first landed ("a field typed as ANOTHER
 * registered struct, a Vec, or an enum is a real, separate, larger
 * undertaking ... not attempted here"). Links against real generated
 * C for:
 *   (defstruct Point (x : I32) (y : I32))
 *   (defstruct Line (start : Point) (end : Point))
 *   (defn point-x [(p : Point)] : I32
 *     (get-field p :x))
 *   (defn line-start-x [(l : Line)] : I32
 *     (point-x (get-field l :start)))
 * and proves the real nested struct field genuinely reads the correct
 * real value through a real by-value struct-in-struct composition,
 * not just that gcc accepts the generated text.
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
    Line l1 = { {42, 7}, {100, 200} };
    Line l2 = { {-5, 0}, {0, 0} };

    int r1 = (int)(intptr_t)line_start_x(l1);
    int r2 = (int)(intptr_t)line_start_x(l2);

    printf("line_start_x(l1) = %d (expected 42), line_start_x(l2) = %d (expected -5)\n", r1, r2);
    assert(r1 == 42);
    assert(r2 == -5);

    printf("driver_struct_field_type: OK -- the real nested Point-inside-Line struct field "
           "genuinely read the correct real value through a real by-value struct composition, "
           "not just compiled clean.\n");
    return 0;
}
