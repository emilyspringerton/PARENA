/* tests/test_big_o_harvest.c -- stdlib/big_o/harvest_rules.prn. Every expected number is derived by hand from the .prn. */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>

#include "test_big_o_harvest_gen.c"

int main(void) {
    /* tables */
    int bt[6] = { 2, 5, 4, 2, 3, 12 }, bg[6] = { 3, 5, 4, 2, 4, 8 }, et[6] = { 96, 160, 128, 64, 96, 320 }, es[6] = { 2, 4, 3, 1, 2, 9 };
    for (int k = 0; k < 6; k++) {
        assert(harvest_base_tokens(k) == bt[k]); assert(harvest_base_grade(k) == bg[k]);
        assert(harvest_extract_ticks(k) == et[k]); assert(harvest_extract_stain(k) == es[k]);
    }
    /* hostile kinds clamp into the table: -7 -> 0 (SHAMBLER), 99 -> 5 (GIANT) */
    assert(harvest_base_tokens(-7) == 2 && harvest_base_tokens(99) == 12);
    assert(harvest_foe_count() == 6);

    /* grade: SHAMBLER base 3; precision 1000 -> +4; 999 -> +3 (999/250=3); 249 -> 0 */
    assert(harvest_precision_bonus(1000) == 4 && harvest_precision_bonus(999) == 3 && harvest_precision_bonus(249) == 0);
    assert(harvest_precision_bonus(-50) == 0 && harvest_precision_bonus(5000) == 4);
    /* contamination: 1 kind -> 0, 3 kinds -> 2, 6 -> 5, 0 or negative treated as 1 -> 0, 99 -> 5 */
    assert(harvest_contamination_penalty(1) == 0 && harvest_contamination_penalty(3) == 2 && harvest_contamination_penalty(6) == 5);
    assert(harvest_contamination_penalty(0) == 0 && harvest_contamination_penalty(-4) == 0 && harvest_contamination_penalty(99) == 5);
    assert(harvest_grade(0, 1000, 1) == 7);       /* 3 + 4 - 0 */
    assert(harvest_grade(0, 0, 4) == 0);          /* 3 + 0 - 3 */
    assert(harvest_grade(5, 1000, 1) == 9);       /* 8 + 4 = 12 clamped to 9 */
    assert(harvest_grade(3, 0, 6) == 0);          /* 2 + 0 - 5 < 0 clamped to 0 */
    assert(harvest_grade(1, 750, 2) == 7);        /* 5 + 3 - 1 */

    /* tokens: base*(10+grade)/10 */
    assert(harvest_tokens(0, 0) == 2);            /* 2*10/10 */
    assert(harvest_tokens(0, 9) == 3);            /* 2*19/10 = 3 (integer) */
    assert(harvest_tokens(5, 9) == 22);           /* 12*19/10 = 22 */
    assert(harvest_tokens(1, 5) == 7);            /* 5*15/10 = 7 */
    assert(harvest_tokens(1, 99) == 9);           /* grade clamps to 9: 5*19/10 = 9 */

    /* rush: half the time, double the noise */
    assert(harvest_extract_ticks_adjusted(1, 0) == 160 && harvest_extract_ticks_adjusted(1, 1) == 80);
    assert(harvest_stain_gain(5, 0) == 9 && harvest_stain_gain(5, 1) == 18);
    assert(harvest_apply_stains(95, 18) == 100 && harvest_apply_stains(-5, 3) == 3 && harvest_apply_stains(10, -4) == 10);

    /* night disposal */
    assert(harvest_disposal_decay(0) == 2 && harvest_disposal_decay(1) == 5 && harvest_disposal_decay(2) == 12 && harvest_disposal_decay(9) == 12);
    assert(harvest_disposal_cost_ticks(0) == 64 && harvest_disposal_cost_ticks(1) == 192 && harvest_disposal_cost_ticks(2) == 480);
    assert(harvest_witness_weight_pm(0) == 100 && harvest_witness_weight_pm(10) == 180 && harvest_witness_weight_pm(100) == 900 && harvest_witness_weight_pm(1000) == 900);
    /* every disposal removes strictly more at the more expensive site, and costs strictly more time */
    for (int s = 0; s < 2; s++) { assert(harvest_disposal_decay(s) < harvest_disposal_decay(s + 1)); assert(harvest_disposal_cost_ticks(s) < harvest_disposal_cost_ticks(s + 1)); }

    /* extraction bonus: 50 tokens carried -> 10 base bonus; half the clock left -> 5 */
    assert(harvest_extraction_bonus(50, 1000) == 10 && harvest_extraction_bonus(50, 500) == 5 && harvest_extraction_bonus(50, 0) == 0);
    assert(harvest_extraction_bonus(4, 1000) == 0 && harvest_extraction_bonus(-9, 1000) == 0);

    /* exhaustive range sweep: no input combination leaves its documented range */
    for (int k = -3; k < 10; k++) for (int p = -100; p <= 1100; p += 50) for (int d = -2; d <= 9; d++) {
        int g = harvest_grade(k, p, d);
        assert(g >= 0 && g <= 9);
        int t = harvest_tokens(k, g);
        assert(t >= 2 && t <= 22);
    }
    printf("test_big_o_harvest: all assertions passed\n");
    return 0;
}
