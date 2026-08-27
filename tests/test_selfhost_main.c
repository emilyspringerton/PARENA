/* tests/test_selfhost_main.c -- real end-to-end verification of
 * selfhost/main.prn's own build-file, the real fifth domain of
 * PARENA's own self-hosting effort (NORTHSTAR.md's own "Self-hosting"
 * section), directly continuing selfhost/lexer.prn + selfhost/
 * parser.prn + selfhost/region.prn + selfhost/emit.prn (2026-08-27,
 * founder real-time: "self hosted compiler" -> "continue" (repeated)
 * -> "continue on welf hosted parena compiler").
 *
 * Real, honest scope: build-file is a real disk-to-disk pipeline
 * function (read a real .prn file, run it through the selfhost
 * lexer+parser+region+emit pipeline, write real C to a real output
 * file) -- not yet a real argv-parsing standalone executable (see
 * selfhost/main.prn's own header comment for why that's a genuinely
 * separate, unstarted emitter feature). This test drives it with real
 * path strings, reusing the SAME real DoD acceptance file
 * (examples/valid_only.prn) and the SAME real driver
 * (tests/integration/driver_valid_only.c) domain 4's own
 * test_selfhost_emit.c already verifies against -- the difference here
 * is that build-file itself does the real file read AND the real file
 * write, not this test harness.
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test_selfhost_main_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    Arena a;
    arena_init(&a);

    char out_c_path[256];
    snprintf(out_c_path, sizeof out_c_path, "/tmp/parena_selfhost_main_test_%d.c", (int)getpid());

    /* --- real call: build-file reads examples/valid_only.prn (the
     * actual DoD acceptance file) off disk and writes real C to
     * out_c_path, all inside build-file itself. --- */
    Result r = build_file("examples/valid_only.prn", out_c_path, &a);
    CHECK(r.tag == 1, "build-file succeeds end to end on the real valid_only.prn DoD acceptance file");
    if (r.tag != 1) {
        printf("       error: %s\n", (char *)r.value);
        printf("\nSOME FAILED\n");
        return 1;
    }

    /* --- real check: the output file build-file wrote actually
     * exists and holds real, non-empty C text. --- */
    FILE *f = fopen(out_c_path, "rb");
    CHECK(f != NULL, "build-file's own real output file exists on disk");
    if (f) {
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        CHECK(len > 0, "build-file's own real output file is non-empty");
        fseek(f, 0, SEEK_SET);
        char *generated_c = (char *)arena_alloc(&a, (size_t)len + 1);
        size_t nread = fread(generated_c, 1, (size_t)len, f);
        generated_c[nread] = '\0';
        fclose(f);

        CHECK(strstr(generated_c, "char * load_config(Arena *buf_arena") != NULL,
              "build-file's own real output file contains the real, correctly-mangled load_config signature");

        /* --- real compile + link + run of build-file's own real
         * output, against the SAME real driver domain 4's own check
         * already uses. --- */
        char bin_path[300];
        snprintf(bin_path, sizeof bin_path, "%s.bin", out_c_path);
        char cmd[1024];
        snprintf(cmd, sizeof cmd,
                 "gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -o %s "
                 "tests/integration/driver_valid_only.c %s runtime/parena_runtime.c 2>&1",
                 bin_path, out_c_path);
        int compile_status = system(cmd);
        CHECK(compile_status == 0,
              "build-file's own real output file compiles clean under gcc -std=c99 -Wall -Wextra -pedantic -Werror");

        if (compile_status == 0) {
            int run_status = system(bin_path);
            CHECK(run_status == 0,
                  "the real compiled program actually RUNS correctly -- driver_valid_only.c's own "
                  "internal assert passes against build-file's own real, disk-written output");
        }
        remove(bin_path);
    }
    remove(out_c_path);

    /* --- real negative case: a nonexistent input path fails cleanly
     * with a real Err, not a crash. --- */
    {
        Result r2 = build_file("examples/this_file_does_not_exist.prn", out_c_path, &a);
        CHECK(r2.tag != 1, "build-file reports a real Err (not a crash) on a nonexistent input path");
    }

    arena_free_all(&a);
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
