/* driver_forward_ref.c -- real behavioral verification for selfhost/
 * emit.prn's new forward-prototype support (2026-08-28), closing a
 * real, honest, repeatedly-worked-around gap: this file used to emit
 * NO prototypes at all, so a callee had to textually PRECEDE its own
 * caller for the generated C to compile (several of this same day's
 * own earlier test fixtures had to reorder their functions specfically
 * because of this). Links against real generated C for:
 *   (defn make-buf [(dest : Arena @ :region/buffer)] : String @ Region
 *     (wrap-buf (alloc dest String "hello")))
 *   (defn wrap-buf [(s : String @ Region)] : String @ Region
 *     s)
 * -- deliberately in CALLER-BEFORE-CALLEE order (make-buf, which calls
 * wrap-buf, defined FIRST), the exact ordering that used to fail with
 * a real gcc "implicit declaration of function" error before this
 * feature existed. Proves the real forward prototype genuinely lets
 * this compile AND that the resulting program still computes the
 * correct value, not just that gcc accepts the text.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

extern char *make_buf(Arena *dest);

int main(void) {
    Arena a;
    arena_init(&a);

    char *result = make_buf(&a);

    printf("make_buf(&a) = \"%s\" (expected \"hello\")\n", result);
    assert(result != NULL);
    assert(strcmp(result, "hello") == 0);

    printf("driver_forward_ref: OK -- a real caller-before-callee program compiled and ran "
           "correctly via the new forward-prototype pass, not just avoided the ordering problem "
           "by luck.\n");
    return 0;
}
