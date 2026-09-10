/* Real driver for the tail-position bare true/false literal test
 * (tests/test_selfhost_emit.c) -- proves emit-tail-symbol's own boxed-return
 * fix (2026-09-10) is genuinely correct, not just gcc-clean text: the first,
 * broken version emitted a bare `return 1;`/`return 0;` from a `char *`
 * declared function, which fails to even compile under -Werror=int-conversion
 * for the true case (0 is a real null-pointer constant in C, 1 is not). */
#include <assert.h>
#include <stdint.h>

extern char *always_true(int x);
extern char *always_false(int x);

int main(void) {
    assert((intptr_t)always_true(0) == 1);
    assert((intptr_t)always_false(0) == 0);
    return 0;
}
