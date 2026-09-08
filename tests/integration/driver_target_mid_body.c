/* tests/integration/driver_target_mid_body.c -- real end-to-end proof that the self-hosted
 * emitter's own new MID-BODY `#target {:c (inline-c "...")}` STATEMENT support (2026-09-08,
 * closing the "next real, named step" NORTHSTAR.md's own Self-hosting section flagged right
 * after the whole-body #target case shipped) produces a real, correctly-behaving function --
 * not just C that compiles, the actual `strcpy`/`strcat` side effect genuinely happens and the
 * real tail value is returned afterward, unwrapped, exactly matching stdlib/string.prn's own
 * real `concat` motivating shape.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>

extern char *concat_into_buf(char *a, char *b, Arena *dest);

int main(void) {
    Arena arena;
    arena_init(&arena);
    char *result = concat_into_buf("hi", "there", &arena);
    assert(result != NULL);
    assert(strcmp(result, "hithere") == 0);
    arena_free_all(&arena);
    return 0;
}
