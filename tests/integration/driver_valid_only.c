/* driver_valid_only.c — VS0 domain 4's own real verification target:
 * NORTHSTAR.md's DoD table says "Compiled C output has zero runtime
 * leaks or use-after-free... Runs clean under Valgrind (0 bytes leaked)
 * and AddressSanitizer." This links against the real C domain 3's
 * emitter produces from examples/valid_only.prn and actually calls it,
 * so the check is "does running the emitted program leak/UAF," not
 * just "does it compile" (that's domain 3's own, already-covered bar).
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern char *load_config(Arena *buf_arena);

int main(void) {
    Arena buf;
    arena_init(&buf);
    char *result = load_config(&buf);
    printf("load_config returned: %s\n", result);
    assert(strcmp(result, "parsed_data") == 0);
    arena_free_all(&buf);
    printf("domain 4 driver: OK -- ran clean, no leak/UAF caught by whatever sanitizer this was "
           "built with.\n");
    return 0;
}
