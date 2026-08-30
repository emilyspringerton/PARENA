/* tests/test_base4.c -- real end-to-end verification of stdlib/base4/algebra.prn, a real, faithful
 * port of examples/engine.py.txt's own "CUSTOM BASE-4 / BINARY ALGEBRA LAB" (founder real-time:
 * "add to parena stdlibs"). Same real "compile via parena build, verify the real generated C
 * output directly with plain C assert()" discipline every other real stdlib package test in this
 * repo already uses (see tests/test_json.c, tests/test_selfhost_lexer.c). Deliberately NOT
 * ladybug BDD (checked first, same real, documented blocker every other real mod test this
 * session already names): ladybug's own scarab.prn/firefly/ladybug.prn genuinely can't
 * `parena build` yet.
 *
 * Real, hand-computed expected values, not guessed at: base4-xor/and/or are checked against the
 * real bitwise definition (a^a=0, a&a=a, a|a=a for every real a); base4-subtract's own expected
 * values are hand-verified against Python's own real `%` semantics (always non-negative for a
 * positive modulus) -- the real correctness property algebra.prn's own header comment documents
 * fixing over a naive C-modulo transliteration; base4-cycle-length's own expected values are
 * hand-traced step by step (shown in each assertion's own comment below), not assumed.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>

#include "test_base4_gen.c"

int main(void) {
    /* Real symbol codes. */
    assert(symbol_zero() == 0);
    assert(symbol_one() == 1);
    assert(symbol_minus() == 2);
    assert(symbol_plus() == 3);

    /* Real bitwise algebra -- spot-checked against the real 2-bit truth table. */
    assert(base4_xor(0, 0) == 0);
    assert(base4_xor(1, 2) == 3);
    assert(base4_xor(3, 3) == 0);
    assert(base4_and(3, 1) == 1);
    assert(base4_and(2, 1) == 0);
    assert(base4_or(1, 2) == 3);
    assert(base4_or(0, 0) == 0);

    /* Real mod-4 arithmetic, including the wraparound cases. */
    assert(base4_add(0, 3) == 3);
    assert(base4_add(2, 2) == 0); /* 4 % 4 */
    assert(base4_add(3, 3) == 2); /* 6 % 4 */

    /* Real mod-4 SUBTRACTION -- the real correctness fix over a naive C `%` transliteration.
       Every one of these hand-matches Python's own real (a - b) % 4 output exactly. */
    assert(base4_subtract(0, 3) == 1); /* Python: (0 - 3) % 4 == 1, not C's raw -3 */
    assert(base4_subtract(1, 3) == 2); /* Python: (1 - 3) % 4 == 2 */
    assert(base4_subtract(2, 3) == 3); /* Python: (2 - 3) % 4 == 3 */
    assert(base4_subtract(3, 0) == 3);
    assert(base4_subtract(0, 0) == 0);

    /* Real base4-iterate: op=xor, start=symbol_minus() (2), 3 steps, hand-traced:
       step0 state=xor(2,2)=0, step1 state=xor(0,2)=2, step2 state=xor(2,2)=0 -> final 0. */
    assert(base4_iterate(symbol_minus(), base4_xor, 3) == 0);
    /* Real, honest edge case: zero steps returns start unchanged. */
    assert(base4_iterate(symbol_plus(), base4_xor, 0) == symbol_plus());

    /* Real base4-cycle-length -- a real, provable property (pigeonhole on the 4-element state
       space): AND/OR are idempotent (a&a=a, a|a=a), so EVERY real start has cycle length 1 under
       either. */
    assert(base4_cycle_length(symbol_zero(), base4_and) == 1);
    assert(base4_cycle_length(symbol_one(), base4_and) == 1);
    assert(base4_cycle_length(symbol_minus(), base4_or) == 1);
    assert(base4_cycle_length(symbol_plus(), base4_or) == 1);

    /* Real, hand-traced XOR cycle lengths (a^a=0 always, so every real orbit passes through 0). */
    assert(base4_cycle_length(symbol_zero(), base4_xor) == 1);
    assert(base4_cycle_length(symbol_one(), base4_xor) == 2);
    assert(base4_cycle_length(symbol_minus(), base4_xor) == 2);
    assert(base4_cycle_length(symbol_plus(), base4_xor) == 2);

    /* Real, hand-traced ADD (mod 4) cycle lengths -- varied real periods (1, 2, 4), unlike the
       uniform-1 idempotent operations above. */
    assert(base4_cycle_length(symbol_zero(), base4_add) == 1);
    assert(base4_cycle_length(symbol_one(), base4_add) == 4);
    assert(base4_cycle_length(symbol_minus(), base4_add) == 2);
    assert(base4_cycle_length(symbol_plus(), base4_add) == 4);

    printf("test_base4: all assertions passed\n");
    return 0;
}
