/* driver_defstruct.c -- real behavioral verification for selfhost/
 * emit.prn's new top-level defstruct + get-field support, and the
 * widened mangle() (2026-08-28): compiling-clean alone doesn't prove
 * the real generated struct-accessor functions actually return the
 * real field values, only that gcc accepts the types -- this links
 * against the real generated C for
 *   (defstruct Point (x : I32) (y : I32))
 *   (defn get-x [(p : Point)] : I32 (get-field p :x))
 *   (defn is-zero-x? [(p : Point)] : Bool (= (get-field p :x) 0))
 * and actually constructs real Point values and calls both real
 * accessor functions, proving: the typedef's own real field layout
 * matches what a real C caller expects, get-field's own real `(p).x`
 * emission reads the right field, get-field composes correctly as a
 * comparison argument, and the widened mangle() correctly turns the
 * real `?`-suffixed predicate name into a valid, callable C
 * identifier (is_zero_x_).
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

typedef struct { int x; int y; } Point;

extern char *get_x(Point p);
extern char *is_zero_x_(Point p);

int main(void) {
    Point p1 = {5, 10};
    Point p2 = {0, 3};

    int gx1 = (int)(intptr_t)get_x(p1);
    int gx2 = (int)(intptr_t)get_x(p2);
    printf("get_x(p1)=%d (expected 5), get_x(p2)=%d (expected 0)\n", gx1, gx2);
    assert(gx1 == 5);
    assert(gx2 == 0);

    int z1 = (int)(intptr_t)is_zero_x_(p1);
    int z2 = (int)(intptr_t)is_zero_x_(p2);
    printf("is_zero_x_(p1)=%d (expected 0), is_zero_x_(p2)=%d (expected 1)\n", z1, z2);
    assert(z1 == 0);
    assert(z2 == 1);

    printf("driver_defstruct: OK -- the real Point typedef, get-x's own real field read, and "
           "is-zero-x?'s own real get-field-inside-a-comparison all produced correct values, "
           "and the ?-suffixed name mangled to a real, valid, callable C identifier -- not just "
           "compiled clean.\n");
    return 0;
}
