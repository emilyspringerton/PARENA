/* tests/test_chest_rules.c -- stdlib/shankpit/chest_rules.prn (SHANKPIT loot chests, card #528). Hand-derived from the .prn. */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>

#include "test_chest_rules_gen.c"

int main(void) {
    assert(chest_max_hp(0) == 100 && chest_max_hp(1) == 240 && chest_max_hp(2) == 420);
    assert(chest_max_hp(-5) == 100 && chest_max_hp(99) == 420);

    assert(chest_tier_for_roll(0) == 0 && chest_tier_for_roll(69) == 0);
    assert(chest_tier_for_roll(70) == 1 && chest_tier_for_roll(92) == 1);
    assert(chest_tier_for_roll(93) == 2 && chest_tier_for_roll(99) == 2);
    int n[3] = {0, 0, 0};
    for (int r = 0; r < 100; r++) n[chest_tier_for_roll(r)]++;
    assert(n[0] == 70 && n[1] == 23 && n[2] == 7);

    /* wooden: 40 magnum / 35 AR / 25 shotgun */
    assert(chest_loot_weapon(0, 0) == 1 && chest_loot_weapon(0, 39) == 1 && chest_loot_weapon(0, 40) == 2);
    assert(chest_loot_weapon(0, 74) == 2 && chest_loot_weapon(0, 75) == 3 && chest_loot_weapon(0, 99) == 3);
    /* reinforced: 30 shotgun / 25 katana / 25 sniper / 20 AR */
    assert(chest_loot_weapon(1, 29) == 3 && chest_loot_weapon(1, 30) == 5 && chest_loot_weapon(1, 54) == 5);
    assert(chest_loot_weapon(1, 55) == 4 && chest_loot_weapon(1, 79) == 4 && chest_loot_weapon(1, 80) == 2);
    /* rare: 40 sniper / 40 missile / 20 katana */
    assert(chest_loot_weapon(2, 39) == 4 && chest_loot_weapon(2, 40) == 6 && chest_loot_weapon(2, 79) == 6);
    assert(chest_loot_weapon(2, 80) == 5 && chest_loot_weapon(2, 99) == 5);
    int w[7] = {0};
    for (int r = 0; r < 100; r++) w[chest_loot_weapon(2, r)]++;
    assert(w[4] == 40 && w[6] == 40 && w[5] == 20 && w[1] == 0);

    assert(chest_damage_permille(5) == 1500 && chest_damage_permille(6) == 2000);
    assert(chest_damage_permille(0) == 1200 && chest_damage_permille(2) == 1000);
    puts("chest rules: ok");
    return 0;
}
