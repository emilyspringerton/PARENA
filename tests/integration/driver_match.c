/* driver_match.c -- real behavioral verification for selfhost/
 * emit.prn's new, narrow, TAIL-POSITION-ONLY Result/Option `match`
 * support (2026-08-28): compiling-clean alone doesn't prove the real
 * tag dispatch actually branches on the right variant, only that gcc
 * accepts the types -- this links against the real generated C for
 *   (defn describe-result [(r : Result)] : I32
 *     (match r
 *       ((Ok x) 1)
 *       ((Err e) 0)))
 *   (defn describe-option [(o : Option)] : I32
 *     (match o
 *       ((Some s) 1)
 *       (None 0)))
 * and actually calls both across every real tag, constructing the
 * real Result/Option scrutinee values directly in C (this narrow
 * selfhost emitter doesn't support CONSTRUCTING an Ok/Err/Some/None
 * value yet -- a real, separate, not-yet-started gap, unrelated to
 * this feature's own real scope: DESTRUCTURING an already-held one),
 * unboxing each result the same way every other real caller of this
 * emitter's uniform char*-boxed-return convention already must
 * (`(int)(intptr_t)result`).
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

extern char *describe_result(Result r);
extern char *describe_option(Option o);

int main(void) {
    Result ok = result_ok((void *)(intptr_t)42);
    Result err = result_err((void *)(intptr_t)7);
    Option some = option_some((void *)(intptr_t)99);
    Option none = option_none();

    int r_ok = (int)(intptr_t)describe_result(ok);
    int r_err = (int)(intptr_t)describe_result(err);
    int r_some = (int)(intptr_t)describe_option(some);
    int r_none = (int)(intptr_t)describe_option(none);

    printf("describe_result(Ok)=%d (expected 1), describe_result(Err)=%d (expected 0), "
           "describe_option(Some)=%d (expected 1), describe_option(None)=%d (expected 0)\n",
           r_ok, r_err, r_some, r_none);
    assert(r_ok == 1);
    assert(r_err == 0);
    assert(r_some == 1);
    assert(r_none == 0);

    printf("driver_match: OK -- the real generated Result/Option tag-dispatch if/else chain "
           "genuinely branched to the correct clause on every real tag, not just compiled "
           "clean.\n");
    return 0;
}
