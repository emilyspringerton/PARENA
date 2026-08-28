/* driver_ctor_as_arg.c -- real behavioral verification for selfhost/
 * emit.prn's new Ok/Err/Some/None-construction-as-a-call-argument
 * support (2026-08-28), closing TWO more real, live, silently-wrong-C
 * bugs found via the same direct-probe technique that caught the
 * or/and/not one: `(f (Some s))` used to silently emit `f(Some(s))` (a
 * call to a NEVER-DEFINED function `Some`, since `Ok`/`Err`/`Some`
 * were ALSO never excluded from plain-call-shaped?'s own name checks);
 * a bare `(f None)` used to silently emit `f(None)` (referencing a
 * NEVER-DECLARED identifier `None`, since a bare `None` symbol already
 * satisfied the generic bare-symbol-argument fallback with no special-
 * casing). Links against real generated C for:
 *   (defn wrap-opt [(o : Option)] : I32
 *     (match o ((Some s) 1) (None 0)))
 *   (defn make-and-wrap [(s : String @ Region)] : I32
 *     (wrap-opt (Some s)))
 *   (defn make-and-wrap-none [] : I32
 *     (wrap-opt None))
 * and proves both real constructions genuinely round-trip through the
 * outer call and the real match dispatch, not just compile clean.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

extern char *make_and_wrap(char *s);
extern char *make_and_wrap_none(void);

int main(void) {
    int r1 = (int)(intptr_t)make_and_wrap("hello");
    int r2 = (int)(intptr_t)make_and_wrap_none();

    printf("make_and_wrap(\"hello\")=%d (expected 1), make_and_wrap_none()=%d (expected 0)\n",
           r1, r2);
    assert(r1 == 1);
    assert(r2 == 0);

    printf("driver_ctor_as_arg: OK -- both real Option constructions, passed straight through as "
           "call arguments, genuinely round-tripped through the real match dispatch correctly, "
           "not just compiled clean.\n");
    return 0;
}
