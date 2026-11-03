/* tests/test_forgedinface.c -- the thinnest possible C shim over stdlib/forgedinface/rig.prn's own
 * `self-test` (same established convention as tests/test_base4_pattern.c: every real assertion
 * lives in PARENA, this file only calls it once and checks the one returned Bool, because
 * parena-c still has no `(defn main ...)` -> C `int main` emission convention).
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>

#include "test_forgedinface_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    int result = self_test(&arena);
    assert(result);

    printf("test_forgedinface: self-test (real PARENA-native test logic) passed\n");
    return 0;
}
