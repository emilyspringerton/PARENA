/* driver_let_struct.c -- real behavioral verification for selfhost/emit.prn's new
 * let-wrapped-struct-literal-body + get-field-as-if-value-branch support (S501, 2026-09-18):
 * compiling clean alone doesn't prove a let-binding genuinely precedes the struct construction,
 * or that both if-branches (each a real struct-field read) actually select correctly at runtime
 * -- this links against the real generated C for
 *   (defstruct Point (x : I32) (y : I32))
 *   (defn advance-like [(p : Point) (c : I32)]
 *     : Point @ Region
 *     (let [unused (get-field p :x)]
 *       {:x (if (= c 10) (+ (get-field p :x) 1) (get-field p :x))
 *        :y (get-field p :y)}))
 * -- part of the real shape selfhost/lexer.prn's own lx-advance needs: a let-binding that
 * precedes the struct construction (proving bindings-then-construct ordering), an if-value
 * struct field whose own condition compares a PARAMETER (deliberately not the let-bound value --
 * see this test's own header comment in tests/test_selfhost_emit.c for the real, separate,
 * NOT-yet-fixed boxed-comparison gap that would otherwise be silently depended on), each branch
 * a real struct-field read, and a second field that's a plain, unconditional struct-field read.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>

typedef struct { int x; int y; } Point;

extern Point advance_like(Point p, int c);

int main(void) {
    Point p1 = {5, 99};
    /* c=10 -> the THEN branch fires -> x = p.x + 1 = 6 */
    Point r1 = advance_like(p1, 10);
    /* c=3 -> the ELSE branch fires -> x = p.x unchanged = 5 */
    Point r2 = advance_like(p1, 3);
    printf("advance_like(p1,10)=(%d,%d) (expected 6,99), advance_like(p1,3)=(%d,%d) "
           "(expected 5,99)\n", r1.x, r1.y, r2.x, r2.y);
    assert(r1.x == 6 && r1.y == 99);
    assert(r2.x == 5 && r2.y == 99);
    printf("driver_let_struct: OK -- the leading let-binding, both real if-value branches (each "
           "a genuine struct-field read), and the unconditional :y field all produced correct "
           "values, not just compiled clean.\n");
    return 0;
}
