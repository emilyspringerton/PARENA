/* tests/test_forgedlevel.c -- the thinnest possible C shim over stdlib/forgedlevel/level.prn's
 * own `self-test` and stdlib/forgedlevel/mods/cottage.prn's own `self-test-mod` (the same
 * convention tests/test_base4_pattern.c and tests/test_forgedinface.c already establish): every
 * real assertion lives in PARENA, this file calls each check once, prints the individual results,
 * and asserts. parena-c has no real `(defn main ...)` -> C `int main` emission convention, so a
 * standalone PARENA executable genuinely is not possible today -- this is that boundary's
 * established fallback, not a choice about where the tests should live.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>

#include "test_forgedlevel_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    int materials = self_test_materials(&arena);
    int block = self_test_block(&arena);
    int quantities = self_test_quantities(&arena);
    int wall = self_test_wall(&arena);
    int plan = self_test_plan(&arena);
    int nested = self_test_nested(&arena);
    int mod = self_test_mod(&arena);

    printf("materials=%d block=%d quantities=%d wall=%d plan=%d nested=%d mod=%d\n",
           materials, block, quantities, wall, plan, nested, mod);

    assert(materials);
    assert(block);
    assert(quantities);
    assert(wall);
    assert(plan);
    assert(nested);
    assert(mod);
    assert(self_test(&arena));

    printf("test_forgedlevel: self-test (real PARENA-native test logic) passed\n");
    return 0;
}
