/* tests/test_action_bar.c -- stdlib/actionbar/{action_bar_rules,example_plugin_mod}.prn. Numbers hand-derived from the .prn. */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>

#include "test_action_bar_gen.c"
#include "test_action_bar_plugin_gen.c"

#define W(id) (1 * 65536 + (id))
#define M(id) (4 * 65536 + (id))

int main(void) {
    /* pack/unpack */
    assert(ab_pack(1, 9) == 65545 && ab_kind_of(65545) == 1 && ab_id_of(65545) == 9);
    assert(ab_pack(4, 65535) == 4 * 65536 + 65535 && ab_id_of(ab_pack(4, 65535)) == 65535);
    assert(ab_pack(-3, -3) == 0 && ab_pack(99, 70000) == 15 * 65536 + 65535);
    assert(ab_kind_of(-5) == 0 && ab_id_of(-5) == 0);

    /* digit -> slot: 1..9 = 1..9, 0 = 10, junk = 0 */
    for (int d = 1; d <= 9; d++) assert(ab_slot_for_digit(d) == d);
    assert(ab_slot_for_digit(0) == 10 && ab_slot_for_digit(-1) == 0 && ab_slot_for_digit(10) == 0);
    assert(ab_digit_for_slot(10) == 0 && ab_digit_for_slot(7) == 7 && ab_digit_for_slot(0) == -1 && ab_digit_for_slot(11) == -1);

    /* modifiers: Alt > Ctrl > Shift */
    assert(ab_bank_for_mods(0, 0, 0) == 1);
    assert(ab_bank_for_mods(1, 0, 0) == 2);
    assert(ab_bank_for_mods(0, 1, 0) == 3);
    assert(ab_bank_for_mods(0, 0, 1) == 4);
    assert(ab_bank_for_mods(1, 1, 0) == 3);   /* Shift+Ctrl -> Ctrl */
    assert(ab_bank_for_mods(1, 0, 1) == 4);
    assert(ab_bank_for_mods(0, 1, 1) == 4);
    assert(ab_bank_for_mods(1, 1, 1) == 4);
    assert(ab_bank_for_mods(2, 0, 0) == 2);   /* any non-zero = held */

    /* default bank 1 == today's lobby bindings exactly: 1-6 -> 0..5, 7 flashlight, 8 hammer, 9 spraycan, 0 empty */
    assert(ab_default(1, 1) == W(0) && ab_default(1, 2) == W(1) && ab_default(1, 3) == W(2));
    assert(ab_default(1, 4) == W(3) && ab_default(1, 5) == W(4) && ab_default(1, 6) == W(5));
    assert(ab_default(1, 7) == W(7) && ab_default(1, 8) == W(8) && ab_default(1, 9) == W(9));
    assert(ab_default(1, 10) == 0);
    for (int b = 2; b <= 4; b++) for (int s = 1; s <= 10; s++) assert(ab_default(b, s) == 0);
    /* end-to-end with the digit key, no bind, no plugin */
    for (int d = 0; d <= 9; d++)
        assert(ab_resolve(1, ab_slot_for_digit(d), 0, 0) == ab_default(1, d == 0 ? 10 : d));

    /* out of range -> 0 even with bind/plugin */
    assert(ab_resolve(0, 1, W(3), M(1)) == 0 && ab_resolve(5, 1, W(3), M(1)) == 0);
    assert(ab_resolve(1, 0, W(3), M(1)) == 0 && ab_resolve(1, 11, W(3), M(1)) == 0 && ab_resolve(-1, -1, 0, 0) == 0);
    assert(ab_source(0, 1, W(3), 0) == 0 && ab_source(1, 11, 0, 0) == 0);

    /* precedence: bind > plugin > default */
    assert(ab_resolve(1, 1, 0, M(5)) == M(5));                 /* plugin over default */
    assert(ab_resolve(1, 1, W(4), M(5)) == W(4));              /* bind over plugin */
    assert(ab_resolve(1, 1, W(4), 0) == W(4));                 /* bind over default */
    assert(ab_resolve(2, 3, 0, 0) == 0);                       /* empty slot */
    assert(ab_resolve(2, 3, W(4), 0) == W(4));                 /* bind fills empty bank */
    /* cleared bind forces empty over plugin and default */
    assert(ab_resolve(1, 1, ab_pack(15, 0), M(5)) == 0);
    assert(ab_resolve(1, 10, ab_pack(15, 0), 0) == 0);
    /* a plugin cannot itself "clear" a default (cleared kind from a plugin -> empty, treated as an explicit clear) */
    assert(ab_resolve(1, 2, 0, ab_pack(15, 0)) == 0);
    /* sources */
    assert(ab_source(1, 1, 0, 0) == 1 && ab_source(1, 10, 0, 0) == 0 && ab_source(1, 1, 0, M(5)) == 2);
    assert(ab_source(1, 1, W(4), M(5)) == 3 && ab_source(1, 1, ab_pack(15, 0), M(5)) == 4);

    /* plugin chaining */
    assert(ab_first_nonzero(0, 0) == 0 && ab_first_nonzero(M(1), M(2)) == M(1) && ab_first_nonzero(0, M(2)) == M(2));

    /* hotbar scroll wraps 1..10 */
    assert(ab_cycle_slot(1, 1) == 2 && ab_cycle_slot(10, 1) == 1 && ab_cycle_slot(1, -1) == 10);
    assert(ab_cycle_slot(5, 0) == 5 && ab_cycle_slot(5, 10) == 5 && ab_cycle_slot(5, -23) == 2 && ab_cycle_slot(5, 23) == 8);
    assert(ab_cycle_slot(0, 3) == 1 && ab_cycle_slot(11, 3) == 1);

    /* example plugin: only bank 2 slot 1 */
    assert(ab_plugin_example_action(2, 1) == M(1));
    for (int b = -1; b <= 5; b++) for (int s = -1; s <= 11; s++)
        if (!(b == 2 && s == 1)) assert(ab_plugin_example_action(b, s) == 0);
    /* Shift+1 through the whole pipeline: plugin fills bank 2, bank 1 slot 1 untouched */
    assert(ab_resolve(ab_bank_for_mods(1, 0, 0), ab_slot_for_digit(1), 0, ab_plugin_example_action(2, 1)) == M(1));
    assert(ab_resolve(1, 1, 0, ab_plugin_example_action(1, 1)) == W(0));
    /* player bind beats the example plugin */
    assert(ab_resolve(2, 1, W(2), ab_plugin_example_action(2, 1)) == W(2));

    printf("action_bar: all assertions passed\n");
    return 0;
}
