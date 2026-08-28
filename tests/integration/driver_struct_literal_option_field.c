/* driver_struct_literal_option_field.c -- real behavioral
 * verification for two real bugs/gaps closed together (2026-08-28):
 *
 * 1. "a struct-literal field itself holding a freshly-constructed
 *    Result/Option value" -- named as a real, separate, deliberately
 *    out-of-scope case when struct-literal support first landed the
 *    same day. struct-literal-field-value-supported? widened to
 *    accept result-option-ctor-shaped?/none-shaped? -- a genuinely
 *    free widening, since emit-call-arg already dispatches both
 *    correctly.
 *
 * 2. A real, pre-existing, never-before-exercised bug found while
 *    verifying #1: a defstruct field WITH an explicit `@ Region`
 *    suffix (`(label : String @ Region)`, the exact real shape String
 *    fields typically use throughout this codebase) has 5 real
 *    children, not 3, and was silently rejected by struct-field-
 *    shaped?'s own old exact-3 check -- an honest "skipped, not
 *    registered" failure, never a wrong guess, but a real gap all
 *    the same (every prior struct-field test in this file happened to
 *    use a scalar field with no region annotation).
 *
 * Links against real generated C for:
 *   (defstruct Box (label : String @ Region) (opt : Option))
 *   (defn make-box [(label : String @ Region) (v : String @ Region)] : Box
 *     {:label label :opt (Some v)})
 * and proves the real struct literal genuinely constructs a struct
 * whose Option field holds a real, freshly-constructed Some value,
 * not just that gcc accepts the generated text.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    char *label;
    Option opt;
} Box;

extern Box make_box(char *label, char *v);

int main(void) {
    Box b = make_box("mybox", "hello");

    printf("b.label='%s' (expected mybox), b.opt.tag=%d (expected 1), b.opt.value='%s' "
           "(expected hello)\n",
           b.label, b.opt.tag, (char *)b.opt.value);
    assert(strcmp(b.label, "mybox") == 0);
    assert(b.opt.tag == 1);
    assert(strcmp((char *)b.opt.value, "hello") == 0);

    printf("driver_struct_literal_option_field: OK -- the real struct literal genuinely "
           "constructed a struct whose real String field AND whose real, freshly-constructed "
           "Option field both hold the correct values, not just compiled clean.\n");
    return 0;
}
