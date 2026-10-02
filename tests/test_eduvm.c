/* tests/test_eduvm.c -- stdlib/shankpit/eduvm.prn (card #494) against scripted host stubs. */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>

/* scripted host: compile fails for slot 1, run fails for slot 2, otherwise both succeed and "solve" the world for slot 0 */
static int w_gate, w_bridge, w_portal, w_stab;
int eduvm_host_slot_count(void) { return 8; }
int eduvm_host_seed_slot(int s, int p) { return s >= 0 && s < 8 && p >= 0 && p <= 4; }
int eduvm_host_compile_slot(int s) { return s != 1; }
int eduvm_host_run_slot(int s) { if (s == 2) return 0; if (s == 0) { w_gate = w_bridge = w_portal = 1; w_stab = 100; } return 1; }
int eduvm_host_slot_compiled(int s) { return s != 1; }
int eduvm_host_world(int which) { return which == 0 ? w_gate : which == 1 ? w_bridge : which == 2 ? w_portal : which == 3 ? w_stab : 0; }
void eduvm_host_reset_world(void) { w_gate = w_bridge = w_portal = w_stab = 0; }

#include "test_eduvm_gen.c"

int main(void) {
    assert(edu_slot_count() == 8);
    assert(edu_trial_complete(1, 1, 1, 100) == 1);
    assert(edu_trial_complete(1, 1, 1, 99) == 0);
    assert(edu_trial_complete(0, 1, 1, 100) == 0);
    assert(edu_trial_complete(1, 0, 1, 100) == 0);
    assert(edu_trial_complete(1, 1, 0, 100) == 0);
    assert(edu_trial_step(1) == 0);   /* compile failure -> 0, run never attempted */
    assert(edu_trial_step(2) == 0);   /* run failure -> 0 */
    assert(edu_trial_step(3) == 0);   /* runs fine but world unsolved */
    assert(edu_trial_step(0) == 1);   /* solves it */
    edu_reset_world();
    assert(edu_gate_open() == 0 && edu_portal_stability() == 0);
    printf("All eduvm checks passed.\n");
    return 0;
}
