/* driver_bool_expr.c -- real behavioral verification for selfhost/
 * emit.prn's new recursive boolean-expression sub-language (2026-08-28,
 * or/and/not as real cond tests): compiling-clean alone doesn't prove
 * emit-bool-op/emit-not actually branch correctly, only that gcc
 * accepts the types -- this links against the real generated C for
 *   (defn classify2 [(n : I32)] : I32
 *     (cond
 *       ((or (= n 1) (= n 3)) 100)
 *       ((and (>= n 4) (<= n 6)) 200)
 *       ((not (= n 0)) 300)
 *       (true 0)))
 * and actually calls it across every real branch (both sides of the
 * or, both sides of the and, the not, and the final true fallback),
 * unboxing each result the same way this feature's own header comment
 * documents any real caller must (`(int)(intptr_t)result`).
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

extern char *classify2(int n);

int main(void) {
    struct { int n; int expected; } cases[] = {
        {1, 100}, /* or, left side */
        {3, 100}, /* or, right side */
        {5, 200}, /* and, both sides true */
        {4, 200}, /* and, boundary low */
        {6, 200}, /* and, boundary high */
        {2, 300}, /* not */
        {0, 0},   /* final true fallback */
    };
    int ok = 1;
    for (int i = 0; i < 7; i++) {
        int r = (int)(intptr_t)classify2(cases[i].n);
        printf("classify(%d) = %d (expected %d)\n", cases[i].n, r, cases[i].expected);
        if (r != cases[i].expected) ok = 0;
    }
    assert(ok);
    printf("driver_bool_expr: OK -- all 7 real or/and/not branches produced the correct value, "
           "not just compiled clean.\n");
    return 0;
}
