/* tests/integration/driver_target_whole_body.c -- real end-to-end proof that the self-hosted
 * emitter's own new `#target {:c (inline-c "...")}` whole-body support (2026-09-08, the shipped
 * half of selfhost's own "next frontier" toward true bootstrapping) produces a real, correct,
 * running function, not just a clean compile. Linked directly against the generated .c this
 * test's own snippet compiles to (get_greeting()), matching every other selfhost driver's own
 * "link against the real generated C, call the real generated function" convention.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>

extern char *get_greeting(void);

int main(void) {
    char *g = get_greeting();
    assert(g != NULL);
    assert(strcmp(g, "hello from inline c") == 0);
    return 0;
}
