/* tests/test_big_o_lab_station.c -- verification of stdlib/big_o/lab_station_rules.prn (SECTION 592, card #507).
 * Expected values are hand-derived from the rules in the .prn header comment. */
#include "parena_runtime.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

#include "test_big_o_lab_station_gen.c"

int main(void) {
    assert(labst_max_samples() == 9 && labst_max_clones() == 8 && labst_reach_tenths() == 40);
    assert(labst_clamp(5, 0, 3) == 3 && labst_clamp(-5, 0, 3) == 0 && labst_clamp(2, 0, 3) == 2);
    assert(labst_kind_ok(0) && labst_kind_ok(5) && !labst_kind_ok(-1) && !labst_kind_ok(6));
    assert(labst_base_ok(0) && labst_base_ok(2) && !labst_base_ok(3) && !labst_base_ok(-1));
    /* splice: needs a sample and a free clone slot */
    assert(labst_can_use(0, 1, 1, 0) && !labst_can_use(0, 1, 0, 0) && !labst_can_use(0, 1, 5, 8) && labst_can_use(0, 1, 5, 7));
    /* centrifuge: needs 2 */
    assert(!labst_can_use(1, 0, 1, 0) && labst_can_use(1, 0, 2, 0));
    /* pcr: needs 1 and room under 9 */
    assert(!labst_can_use(2, 0, 0, 0) && labst_can_use(2, 0, 8, 0) && !labst_can_use(2, 0, 9, 0));
    /* vat: needs a clone; fridge/console always */
    assert(!labst_can_use(3, 0, 9, 0) && labst_can_use(3, 0, 0, 1));
    assert(labst_can_use(4, 0, 0, 0) && labst_can_use(5, 2, 0, 0));
    /* bad kind/base never usable */
    assert(!labst_can_use(6, 0, 9, 0) && !labst_can_use(0, 3, 9, 0) && !labst_can_use(-1, 0, 9, 0));
    /* deltas */
    assert(labst_sample_delta(0) == -1 && labst_sample_delta(1) == -2 && labst_sample_delta(2) == 1 && labst_sample_delta(3) == 0);
    assert(labst_target_delta(1) == 1 && labst_target_delta(0) == 0 && labst_target_delta(2) == 0);
    assert(labst_clone_delta(0) == 1 && labst_clone_delta(3) == -1 && labst_clone_delta(1) == 0 && labst_clone_delta(5) == 0);
    /* refine ladder wraps: 0->1->2->0 */
    assert(labst_refine_base(0) == 1 && labst_refine_base(1) == 2 && labst_refine_base(2) == 0);
    assert(labst_refine_base(-7) == 1 && labst_refine_base(99) == 0);
    assert(labst_heal(3) == 25 && labst_heal(0) == 0);
    assert(labst_opens_phone(5) && !labst_opens_phone(4));
    /* hostile inputs never trap (UBSan) and stay in range */
    int hs[] = { INT_MIN, -1, 0, 1, 8, 9, INT_MAX };
    for (unsigned a = 0; a < 7; a++) for (unsigned b = 0; b < 7; b++) for (unsigned c = 0; c < 7; c++) {
        (void)labst_can_use(hs[a], hs[b], hs[c], hs[a]);
        int r = labst_refine_base(hs[b]);
        assert(r >= 0 && r <= 2);
        (void)labst_sample_delta(hs[a]); (void)labst_target_delta(hs[a]); (void)labst_clone_delta(hs[a]);
        (void)labst_heal(hs[a]); (void)labst_opens_phone(hs[a]);
    }
    puts("test_big_o_lab_station: ALL PASS");
    return 0;
}
