/* tests/test_forgedlevel.c -- the thinnest possible C shim over FORGEDLEVEL's own PARENA-native
 * checks (materials/blocks/wall/plan, floor-plan painting, face-culled mesh emission, and the
 * cottage mod). Every real assertion lives in the .prn files; this file calls each check once,
 * prints the individual results and asserts. Same real boundary tests/test_base4_pattern.c
 * documents: parena-c has no `(defn main ...)` -> C `int main` emission convention.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>

#include "test_forgedlevel_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    int level = self_test(&arena);
    int plan2d = self_test_plan2d(&arena);
    int line = self_test_line(&arena);
    int mesh = self_test_mesh(&arena);
    int cull = self_test_cull(&arena);
    int mod = self_test_mod(&arena);

    printf("level=%d plan2d=%d line=%d mesh=%d cull=%d mod=%d\n",
           level, plan2d, line, mesh, cull, mod);

    assert(level);
    assert(plan2d);
    assert(line);
    assert(mesh);
    assert(cull);
    assert(mod);

    printf("test_forgedlevel: self-test (real PARENA-native test logic) passed\n");
    return 0;
}
