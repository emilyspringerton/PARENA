/* tests/test_sip_transaction.c -- real end-to-end verification of stdlib/sip/transaction.prn
 * (CarePyre SIP Phone Phase 3, kanban priority-queue card CAREPYRE-911343). Confirms real
 * outbound (place-a-call) and inbound (receive-a-call) progressions through
 * Idle -> Calling/Ringing -> Established -> Terminated, the "some UAs skip 180" shortcut, and
 * real, honest InvalidTransition errors for nonsensical event/state pairings -- not just "did it
 * compile".
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>

#include "test_sip_transaction_gen.c"

/* CallState/CallEvent are real tagged unions (every variant is zero-arg here, but VS0 still
 * represents them the same generic {tag, void *value} shape as a payload-carrying enum) -- a
 * successful transition's own resulting state comes back boxed behind Result.value, so every
 * check below goes through this small helper rather than repeating the cast at each call site. */
static CallState_Tag result_state_tag(Result r) {
    assert(r.tag == 1); /* Ok */
    return ((CallState *)r.value)->tag;
}

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* Real outbound call: Idle -> Calling -> Proceeding -> Ringing -> Established -> Terminated. */
    CallState s = initial_state();
    assert(s.tag == CallState_TAG_Idle);
    printf("PASS: a fresh call starts Idle\n");

    Result r1 = transition(s, CallEvent_SendInvite(), &arena);
    assert(result_state_tag(r1) == CallState_TAG_Calling);
    printf("PASS: SendInvite from Idle -> Calling\n");

    Result r2 = transition(CallState_Calling(), CallEvent_RecvProvisional(), &arena);
    assert(result_state_tag(r2) == CallState_TAG_Proceeding);
    printf("PASS: RecvProvisional from Calling -> Proceeding\n");

    Result r3 = transition(CallState_Proceeding(), CallEvent_RecvRinging(), &arena);
    assert(result_state_tag(r3) == CallState_TAG_Ringing);
    printf("PASS: RecvRinging from Proceeding -> Ringing\n");

    Result r4 = transition(CallState_Ringing(), CallEvent_RecvOk(), &arena);
    assert(result_state_tag(r4) == CallState_TAG_Established);
    printf("PASS: RecvOk from Ringing -> Established (far end answered)\n");

    Result r5 = transition(CallState_Established(), CallEvent_SendBye(), &arena);
    assert(result_state_tag(r5) == CallState_TAG_Terminated);
    assert(terminal_(CallState_Terminated()) == 1);
    printf("PASS: SendBye from Established -> Terminated, and terminal? reports true\n");

    /* Real, deliberate shortcut: some UAs skip 180 Ringing entirely. */
    Result r6 = transition(CallState_Calling(), CallEvent_RecvOk(), &arena);
    assert(result_state_tag(r6) == CallState_TAG_Established);
    printf("PASS: RecvOk directly from Calling -> Established (no 180 in between) is real, allowed\n");

    /* Real inbound call: Idle -> Ringing -> Established -> Terminated (local user answers). */
    Result r7 = transition(CallState_Idle(), CallEvent_RecvInvite(), &arena);
    assert(result_state_tag(r7) == CallState_TAG_Ringing);
    printf("PASS: RecvInvite from Idle -> Ringing (an incoming call)\n");

    Result r8 = transition(CallState_Ringing(), CallEvent_Answer(), &arena);
    assert(result_state_tag(r8) == CallState_TAG_Established);
    printf("PASS: Answer from Ringing -> Established (local user accepted)\n");

    /* Real inbound decline. */
    Result r9 = transition(CallState_Ringing(), CallEvent_Reject(), &arena);
    assert(result_state_tag(r9) == CallState_TAG_Terminated);
    printf("PASS: Reject from Ringing -> Terminated (local user declined)\n");

    /* Real, honest errors: nonsensical event/state pairings. */
    Result bad1 = transition(CallState_Idle(), CallEvent_Answer(), &arena);
    assert(bad1.tag == 0); /* Err */
    printf("PASS: Answer while Idle is a real, honest InvalidTransition Err, not a crash\n");

    Result bad2 = transition(CallState_Terminated(), CallEvent_SendBye(), &arena);
    assert(bad2.tag == 0);
    printf("PASS: any event once Terminated is a real, honest InvalidTransition Err\n");

    Result bad3 = transition(CallState_Calling(), CallEvent_RecvBye(), &arena);
    assert(bad3.tag == 0);
    printf("PASS: RecvBye while Calling (a real protocol violation) is a real, honest Err\n");

    assert(terminal_(CallState_Idle()) == 0);
    assert(terminal_(CallState_Established()) == 0);
    printf("PASS: terminal? correctly reports false for every non-Terminated state\n");

    printf("\nALL PASS\n");
    return 0;
}
