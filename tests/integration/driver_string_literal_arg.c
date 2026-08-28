/* driver_string_literal_arg.c -- real behavioral verification for
 * selfhost/emit.prn's new raw-string-literal-as-a-call-argument
 * support (2026-08-28), closing the "no literals" half of a real,
 * repeatedly-named gap this file's own header comments have carried
 * since the very first binary-op/plain-call work (numbers were closed
 * same-day back then; strings stayed open until now). Links against
 * real generated C for:
 *   (defn greet [(name : String @ Region)] : String @ Region
 *     name)
 *   (defn greet-world [] : String @ Region
 *     (greet "world"))
 * and proves the real string literal genuinely survives being passed
 * straight through as a call argument, not just that gcc accepts the
 * re-quoted text.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

extern char *greet_world(void);

int main(void) {
    char *result = greet_world();

    printf("greet_world() = \"%s\" (expected \"world\")\n", result);
    assert(result != NULL);
    assert(strcmp(result, "world") == 0);

    printf("driver_string_literal_arg: OK -- the real generated string-literal call argument "
           "genuinely round-tripped through greet-world -> greet intact, not just compiled "
           "clean.\n");
    return 0;
}
