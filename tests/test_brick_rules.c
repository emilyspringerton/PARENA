/* tests/test_brick_rules.c -- verification of stdlib/shankpit/brick_rules.prn composed with
 * PAPERCRAFT's paper_fragment_mod.prn + interact_falloff_mod.prn (SHANKPIT destructible brick).
 * Every expected number below is hand-derived from the .prn source, not copied from the output. */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>

#include "test_brick_rules_gen.c"

int main(void) {
    /* weapon multipliers: knife 0, magnum 1, ar 2, shotgun 3, sniper 4, katana 5, missile 6, flash 7 */
    assert(on_brick_weapon_damage(0, 200) == 0);
    assert(on_brick_weapon_damage(1, 45) == 45);
    assert(on_brick_weapon_damage(2, 20) == 20);
    assert(on_brick_weapon_damage(3, 16) == 16);
    assert(on_brick_weapon_damage(4, 101) == 126);   /* 101 + 101/4 (=25) */
    assert(on_brick_weapon_damage(5, 40) == 0);
    assert(on_brick_weapon_damage(6, 130) == 650);
    assert(on_brick_weapon_damage(7, 0) == 0);

    /* debris: only worsening changes throw anything */
    assert(on_brick_debris_count(0, 0) == 0);
    assert(on_brick_debris_count(0, 1) == 1);
    assert(on_brick_debris_count(0, 2) == 2);
    assert(on_brick_debris_count(1, 3) == 4);
    assert(on_brick_debris_count(3, 3) == 0);
    assert(on_brick_debris_count(2, 1) == 0);

    assert(on_brick_paper_material(0) == 2);
    assert(on_brick_cell_max_hp(0) == 80);

    /* composition with the PAPERCRAFT mods: an AR round (20) at the cell centre (0 permille):
       falloff leaves 20, concrete resists 50 percent -> 10 effective, so an 80 HP cell survives
       7 rounds at 10 each (80 -> 10) and is gone on the 8th. */
    int mat = on_brick_paper_material(0);
    int max_hp = on_brick_cell_max_hp(0);
    int hp = max_hp;
    int shots = 0;
    while (hp > 0) {
        int d = on_papercraft_interact_damage_falloff(on_brick_weapon_damage(2, 20), 0);
        hp = on_paper_fragment_damage(mat, hp, d);
        shots++;
        assert(shots <= 8);
    }
    assert(shots == 8);
    /* tiers: 25 percent of 80 = 20 -> TORN below 20 HP; 60 percent = 48 -> CRACKED below 48 HP */
    assert(on_paper_fragment_state_for_hp(80, 80) == 0);
    assert(on_paper_fragment_state_for_hp(48, 80) == 0);
    assert(on_paper_fragment_state_for_hp(47, 80) == 1);
    assert(on_paper_fragment_state_for_hp(19, 80) == 2);
    assert(on_paper_fragment_state_for_hp(0, 80) == 3);

    /* a missile (x5 = 650): at its blast centre 650 -> 325 effective, gone in one hit; 500 permille:
       650*500/1000 = 325 -> 163, gone; at the rim (1000 permille): 0 damage, 80 HP left;
       800 permille: 650*200/1000 = 130 -> 65 effective -> 15 HP left (TORN) */
    assert(on_paper_fragment_damage(mat, 80, on_papercraft_interact_damage_falloff(on_brick_weapon_damage(6, 130), 0)) == 0);
    assert(on_paper_fragment_damage(mat, 80, on_papercraft_interact_damage_falloff(on_brick_weapon_damage(6, 130), 500)) == 0);
    assert(on_paper_fragment_damage(mat, 80, on_papercraft_interact_damage_falloff(on_brick_weapon_damage(6, 130), 1000)) == 80);
    assert(on_paper_fragment_damage(mat, 80, on_papercraft_interact_damage_falloff(on_brick_weapon_damage(6, 130), 800)) == 15);
    assert(on_paper_fragment_state_for_hp(15, 80) == 2);

    printf("test_brick_rules: all assertions passed\n");
    return 0;
}
