/* tests/test_process.c -- real end-to-end verification of stdlib/process.prn's own new
 * run-capture/run-capture-exit-code (LO FRAMEWORK_NORTHSTAR.md's own event-sourcing extension:
 * SQL projectors shell out to real DB CLI clients rather than binding each one's own native C
 * client library via FFI). Confirms real subprocess execution, real stdout capture, and a real,
 * correctly-reported exit code -- not just "did it compile".
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "test_process_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* Real stdout capture, real success exit code. */
    char *out = run_capture("echo hello", &arena);
    assert(strcmp(out, "hello\n") == 0);
    assert(run_capture_exit_code() == 0);

    /* A real, distinct command's own real output -- confirms this isn't a cached/stale result. */
    char *out2 = run_capture("printf 'a,b,c'", &arena);
    assert(strcmp(out2, "a,b,c") == 0);
    assert(run_capture_exit_code() == 0);

    /* A real, nonzero exit code is reported correctly, not silently swallowed. */
    run_capture("exit 3", &arena);
    assert(run_capture_exit_code() == 3);

    /* A command producing no output at all -- a real empty string, not NULL/garbage. */
    char *out3 = run_capture("true", &arena);
    assert(strcmp(out3, "") == 0);
    assert(run_capture_exit_code() == 0);

    printf("test_process: all assertions passed\n");
    return 0;
}
