/* tests/test_selfhost_main_multifile.c -- real end-to-end verification of
 * selfhost/main.prn's own build-files (2026-08-28), the multi-file
 * companion to build-file domain 5 already established (test_selfhost_
 * main.c). Direct continuation of "continue working on parena self
 * hosted" / "removing c ffi when possible" (founder real-time).
 *
 * Real, honest scope: uses examples/selfhost_multifile_a.prn/b.prn (a
 * dedicated fixture, NOT the reference compiler's own examples/
 * multifile_a.prn/b.prn) -- see that fixture's own header comment for
 * the three real, separate, pre-existing selfhost/emit.prn gaps found
 * and deliberately avoided while building it (defstruct not walked at
 * top level; a bare alloc/arithmetic/match/cond body falls through to
 * a broken emit-tail-symbol fallback; plain-call-shaped let-values
 * require every argument to be a bare symbol), none of which this test
 * means to fix. Reuses examples/valid_only.prn's own real, proven
 * `with-arena` + `let` + `alloc` shape (domain 4's own DoD acceptance
 * case) for the cross-file callee, so this test isolates and proves
 * the one real thing it means to check: build-files' own real
 * cross-file function resolution, within the selfhost emitter's actual
 * current, narrow, proven scope.
 *
 * Two real checks, same shape as run_multifile_check.sh's own two:
 *   1. a.prn + b.prn built together via build-files exit Ok and produce
 *      real, gcc-clean C, with use-message's call to get-message
 *      (defined only in the OTHER file) genuinely resolved.
 *   2. b.prn built ALONE (a real one-element Vec) still produces C that
 *      genuinely fails to compile (get-message undeclared) -- proving
 *      check 1's success is real cross-file resolution, not a
 *      coincidence of some other fix. build-files itself still reports
 *      Ok here (region-analyze's own real, documented scope doesn't
 *      include full call-graph validation -- the reference compiler's
 *      own `parena build` on b.prn alone ALSO exits 0, only gcc -Werror
 *      catches the real implicit-declaration problem, confirmed
 *      directly), so the real check is the gcc compile, not
 *      build-files' own return tag.
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test_selfhost_main_multifile_gen.c"

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
    snprintf(out_c_path, sizeof out_c_path, "/tmp/parena_selfhost_multifile_test_%d.c", (int)getpid());

    /* --- check 1: a.prn + b.prn together, via build-files, exit Ok and
     * produce real, gcc-clean C with a genuinely resolved cross-file call. --- */
    Vec paths = vec_new(&a);
    vec_push_(&paths, (void *)"examples/selfhost_multifile_a.prn");
    vec_push_(&paths, (void *)"examples/selfhost_multifile_b.prn");

    Result r = build_files(&paths, out_c_path, &a);
    CHECK(r.tag == 1, "build-files succeeds end to end on selfhost_multifile_a.prn + _b.prn together");
    if (r.tag != 1) {
        printf("       error: %s\n", (char *)r.value);
    } else {
        FILE *f = fopen(out_c_path, "rb");
        CHECK(f != NULL, "build-files' own real output file exists on disk");
        if (f) {
            fseek(f, 0, SEEK_END);
            long len = ftell(f);
            CHECK(len > 0, "build-files' own real output file is non-empty");
            fseek(f, 0, SEEK_SET);
            char *generated_c = (char *)arena_alloc(&a, (size_t)len + 1);
            size_t nread = fread(generated_c, 1, (size_t)len, f);
            generated_c[nread] = '\0';
            fclose(f);

            CHECK(strstr(generated_c, "get_message(Arena") != NULL,
                  "build-files' own real output contains get-message -- a.prn's own function");
            CHECK(strstr(generated_c, "use_message(Arena") != NULL,
                  "build-files' own real output contains use-message -- b.prn's own function, not just a.prn's");
            CHECK(strstr(generated_c, "get_message(dest)") != NULL,
                  "use-message's own real body genuinely calls get_message -- a.prn's own function, "
                  "resolved across the file boundary, not stubbed or dropped");

            char cmd[1024];
            snprintf(cmd, sizeof cmd,
                     "gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -c %s -o %s.o 2>&1",
                     out_c_path, out_c_path);
            int compile_status = system(cmd);
            CHECK(compile_status == 0,
                  "build-files' own real output compiles clean under gcc -std=c99 -Wall -Wextra -pedantic -Werror");
            remove(out_c_path);
            char obj_path[300];
            snprintf(obj_path, sizeof obj_path, "%s.o", out_c_path);
            remove(obj_path);
        }
    }

    /* --- check 2: b.prn alone (a real one-element Vec) still produces C
     * that genuinely fails to compile -- get_message is real, honestly
     * undeclared, not silently resolved to something else. build-files
     * itself still reports Ok (region-analyze's own real, documented
     * scope has no general call-graph validation -- confirmed directly:
     * the reference compiler's own `parena build` on b.prn alone ALSO
     * exits 0, only gcc -Werror catches the real implicit-declaration
     * problem), so the real check here is the gcc compile, matching
     * what actually distinguishes "resolved" from "not resolved." --- */
    {
        char out_c_path2[256];
        snprintf(out_c_path2, sizeof out_c_path2, "/tmp/parena_selfhost_multifile_test_alone_%d.c", (int)getpid());
        Vec paths_alone = vec_new(&a);
        vec_push_(&paths_alone, (void *)"examples/selfhost_multifile_b.prn");
        Result r2 = build_files(&paths_alone, out_c_path2, &a);
        CHECK(r2.tag == 1, "build-files itself still reports Ok on b.prn alone -- region-analyze has no "
              "general call-graph validation, same real, documented scope the reference compiler shares");
        if (r2.tag == 1) {
            char cmd2[1024];
            snprintf(cmd2, sizeof cmd2,
                     "gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -c %s -o %s.o 2>&1",
                     out_c_path2, out_c_path2);
            int compile_status2 = system(cmd2);
            CHECK(compile_status2 != 0,
                  "but the real gcc compile of b.prn alone genuinely fails -- get_message is honestly "
                  "undeclared without a.prn, proving check 1's success above is real cross-file "
                  "resolution, not a coincidence of some other fix");
            char obj_path2[300];
            snprintf(obj_path2, sizeof obj_path2, "%s.o", out_c_path2);
            remove(obj_path2);
        }
        remove(out_c_path2);
    }

    arena_free_all(&a);
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
