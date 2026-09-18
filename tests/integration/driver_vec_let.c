/* driver_vec_let.c -- real compile+run proof for tests/test_selfhost_emit.c's own vec/-qualified
 * let-binding-value coverage (S501, 2026-09-18): vec-len-of-new builds a real Vec via
 * `(vec/new dest)` then reads its length back via `(vec/len &v)`, both as chained let-binding
 * values -- the exact real shape `stdlib/array.prn`'s own `strides-for` needed. Its own I32
 * return type stays this narrow emitter's pre-existing, uniform boxed `char *` convention (NOT a
 * raw C int -- confirmed directly against this same file's own established behavior for every
 * other I32-returning defn), so the real result is unboxed via `(intptr_t)`.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

extern char *vec_len_of_new(Arena *dest);

int main(void) {
    Arena a;
    arena_init(&a);
    char *result = vec_len_of_new(&a);
    intptr_t n = (intptr_t)result;
    printf("vec_len_of_new returned: %ld (expected 0)\n", (long)n);
    assert(n == 0);
    arena_free_all(&a);
    printf("driver_vec_let: OK -- vec/new -> vec/len round-tripped correctly through real, "
           "self-hosted C.\n");
    return 0;
}
