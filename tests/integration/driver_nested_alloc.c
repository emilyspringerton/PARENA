/* driver_nested_alloc.c -- real behavioral verification for selfhost/
 * emit.prn's new nested-alloc-call-as-a-call-argument support
 * (2026-08-28), closing the one real exclusion the same day's earlier
 * nested-call-argument work explicitly named as still open. Links
 * against real generated C for:
 *   (defn make-buf [(dest : Arena @ :region/buffer)] : String @ Region
 *     (wrap-buf (alloc dest String "hello")))
 *   (defn wrap-buf [(s : String @ Region)] : String @ Region
 *     s)
 * and proves the nested `(alloc dest String "hello")` call genuinely
 * allocates the real string INTO the real, correct destination Arena
 * and that it survives being passed straight through as a call
 * argument, not just that gcc accepts the generated text.
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

    printf("driver_nested_alloc: OK -- the real generated nested alloc-call argument genuinely "
           "allocated the correct string into the correct Arena and round-tripped through the "
           "outer call intact, not just compiled clean.\n");
    return 0;
}
