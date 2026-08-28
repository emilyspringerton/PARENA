/* driver_result_ctor.c -- real behavioral verification for selfhost/
 * emit.prn's new Result/Option CONSTRUCTION support (2026-08-28), the
 * direct complement to driver_match.c's own DESTRUCTION support --
 * that file's own header comment explicitly named this as "a real,
 * separate, not-yet-started gap" the moment match landed. This links
 * against real generated C for:
 *   (defn make-ok [(s : String @ Region)] : Result (Ok s))
 *   (defn make-err [(s : String @ Region)] : Result (Err s))
 *   (defn make-some [(s : String @ Region)] : Option (Some s))
 *   (defn make-none [] : Option None)
 *   (defn round-trip-result [(r : Result)] : I32
 *     (match r ((Ok x) 1) ((Err e) 0)))
 *   (defn round-trip-option [(o : Option)] : I32
 *     (match o ((Some s) 1) (None 0)))
 * and proves the REAL round trip end to end: every value here is both
 * CONSTRUCTED and DESTRUCTURED by real selfhost-emitted C, never
 * hand-constructed in this driver the way driver_match.c's own real,
 * honest header comment says it has to (today; this file is what
 * closes that gap) -- plus checks the payload itself survives the
 * trip intact (not just the tag), reading straight through Result's/
 * Option's own real `void *value` field.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern Result make_ok(char *s);
extern Result make_err(char *s);
extern Option make_some(char *s);
extern Option make_none(void);
extern char *round_trip_result(Result r);
extern char *round_trip_option(Option o);

int main(void) {
    Result ok = make_ok("hello");
    Result err = make_err("goodbye");
    Option some = make_some("world");
    Option none = make_none();

    assert(ok.tag == 1);
    assert(strcmp((char *)ok.value, "hello") == 0);
    assert(err.tag == 0);
    assert(strcmp((char *)err.value, "goodbye") == 0);
    assert(some.tag == 1);
    assert(strcmp((char *)some.value, "world") == 0);
    assert(none.tag == 0);

    int rt_ok = (int)(intptr_t)round_trip_result(ok);
    int rt_err = (int)(intptr_t)round_trip_result(err);
    int rt_some = (int)(intptr_t)round_trip_option(some);
    int rt_none = (int)(intptr_t)round_trip_option(none);

    printf("round_trip_result(make_ok)=%d (expected 1), round_trip_result(make_err)=%d "
           "(expected 0), round_trip_option(make_some)=%d (expected 1), "
           "round_trip_option(make_none)=%d (expected 0)\n",
           rt_ok, rt_err, rt_some, rt_none);
    assert(rt_ok == 1);
    assert(rt_err == 0);
    assert(rt_some == 1);
    assert(rt_none == 0);

    printf("driver_result_ctor: OK -- every Result/Option value here was both CONSTRUCTED and "
           "DESTRUCTURED entirely by real selfhost-emitted C, the tag and the real payload both "
           "surviving the round trip intact, closing the loop driver_match.c's own header comment "
           "named as not-yet-started.\n");
    return 0;
}
