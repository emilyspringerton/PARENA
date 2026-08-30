/* tests/test_mag_gematria.c -- thinnest possible C shim over stdlib/mag/gematria.prn's own
 * self-test (same real "write the main in pure PARENA" convention base4/pattern.prn's own
 * test_base4_pattern.c already established). Founder real-time: "can we turn the mag book into
 * parena playground?" -- ports QUEENSALLYONLINEBOOKOFMAGIFICATIONANDUNICOR's squish/gematria
 * pipeline, re-derived from the already-verified Go port (gpt2-alpine-c/pkg/towerprint), pinned
 * against its own real "SALLY" test vector (AZ dec 20330, ZA dec 71661).
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>

#include "test_mag_gematria_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    assert(self_test(&arena));

    printf("test_mag_gematria: self-test passed (SALLY: AZ=20330, ZA=71661)\n");
    return 0;
}
