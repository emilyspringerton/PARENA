/* tests/test_base4_pattern.c -- the thinnest possible C shim over stdlib/base4/pattern.prn's own
 * `self-test` (founder real-time: "write the MAIN in pure parena, at least like the ffi for it").
 * parena-c has no real `(defn main ...)` -> C `int main` emission convention yet (confirmed
 * directly against src/emit.c, per selfhost/main.prn's own already-documented header comment), so
 * a real standalone PARENA executable genuinely isn't possible today -- this file is that same
 * honest boundary's established fallback: every real assertion and test case lives in
 * pattern.prn's own `self-test`, written in pure PARENA; this file only calls it once and checks
 * the one returned Bool, real minimal FFI, nothing else.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>

#include "test_base4_pattern_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    int result = self_test(&arena);
    assert(result);

    printf("test_base4_pattern: self-test (real PARENA-native test logic) passed\n");
    return 0;
}
