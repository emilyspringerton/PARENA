/* driver_arith.c -- real behavioral verification for selfhost/emit.prn's
 * new binary-op support (2026-08-28): compiling-clean alone doesn't
 * prove emit-i32-boxed's own real `(char *)(intptr_t)` reinterpretation
 * actually round-trips the real arithmetic VALUE correctly, only that
 * gcc accepts the types -- this links against the real generated C for
 * `(defn add-ten [(n : I32)] : I32 (+ n 10))` and actually calls it,
 * unboxing the same way this feature's own header comment documents
 * any real caller must (`(int)(intptr_t)result`).
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

extern char *add_ten(int n);

int main(void) {
    char *raw = add_ten(5);
    int result = (int)(intptr_t)raw;
    printf("add_ten(5) = %d (expected 15)\n", result);
    assert(result == 15);
    printf("driver_arith: OK -- the real arithmetic value round-tripped correctly through "
           "emit-i32-boxed's own char*/intptr_t reinterpretation, not just compiled clean.\n");
    return 0;
}
