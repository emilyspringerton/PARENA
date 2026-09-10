/* Real driver for the if-as-comparison-operand test (tests/test_selfhost_emit.c)
 * -- proves every-call-arg-symbol-or-number?/emit-call-arg's own if-value-shaped
 * widening (2026-09-10) is genuinely correct: the previously-empty `return ;`
 * (an honest silent non-match falling through the whole bool-expr chain) is now
 * a real `return (char *)(intptr_t)(n > (flag > 0 ? 1 : 0));`. */
#include <assert.h>
#include <stdint.h>

extern char *check(int flag, int n);

int main(void) {
    /* flag > 0 -> operand is 1: n=1 is NOT > 1 */
    assert((intptr_t)check(1, 1) == 0);
    /* flag > 0 -> operand is 1: n=2 IS > 1 */
    assert((intptr_t)check(1, 2) == 1);
    /* flag <= 0 -> operand is 0: n=1 IS > 0 */
    assert((intptr_t)check(0, 1) == 1);
    /* flag <= 0 -> operand is 0: n=0 is NOT > 0 */
    assert((intptr_t)check(0, 0) == 0);
    return 0;
}
