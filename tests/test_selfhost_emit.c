/* tests/test_selfhost_emit.c -- real end-to-end verification of
 * selfhost/emit.prn, the real fourth domain of PARENA's own self-
 * hosting effort (NORTHSTAR.md's own "Self-hosting" section), directly
 * continuing selfhost/lexer.prn + selfhost/parser.prn + selfhost/
 * region.prn (2026-08-27, founder real-time: "self hosted compiler" ->
 * "continue" (repeated)).
 *
 * Real, honest scope note: unlike lexer/parser/region's own tests
 * (pure in-process assertions), this domain's own real DoD acceptance
 * bar is "gcc ... compiles emitted output with 0 warnings" PLUS "a
 * real driver program linking the emitted code ... actually runs it
 * correctly" (see NORTHSTAR.md's own VS0 status notes on domain 3) --
 * so this test does the real thing: reads examples/valid_only.prn (the
 * actual real DoD acceptance file) off disk, runs it through the
 * selfhost lexer+parser+emit pipeline, writes the real resulting C to
 * a real temp file, shells out to a REAL gcc invocation to compile +
 * link it against tests/integration/driver_valid_only.c (the SAME
 * real driver domain 4's own run_domain4_check.sh already uses against
 * the C reference's own emitted output -- reused verbatim here, not
 * reinvented), then actually RUNS the resulting binary and checks its
 * real exit code (the driver's own real internal
 * `assert(strcmp(result, "parsed_data") == 0)` is what actually
 * verifies the emitted C is behaviorally correct, not just that it
 * compiles).
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test_selfhost_emit_gen.c"

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

    /* --- real file read: examples/valid_only.prn, the actual DoD
     * acceptance file, not a copy pasted into this test --- */
    FILE *f = fopen("examples/valid_only.prn", "rb");
    CHECK(f != NULL, "examples/valid_only.prn (the real DoD acceptance file) opens");
    if (!f) { printf("\nSOME FAILED\n"); return 1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *src = (char *)arena_alloc(&a, (size_t)len + 1);
    size_t nread = fread(src, 1, (size_t)len, f);
    src[nread] = '\0';
    fclose(f);

    /* --- real parse + emit through the selfhost pipeline --- */
    Result pr = parse_program(src, &a);
    CHECK(pr.tag == 1, "the real valid_only.prn source parses cleanly through the selfhost lexer+parser");
    if (pr.tag != 1) { printf("\nSOME FAILED\n"); return 1; }
    Node program = *(Node *)pr.value;
    char *generated_c = emit_program(&program, &a);
    CHECK(generated_c != NULL && strlen(generated_c) > 0, "emit-program produces real, non-empty C output");

    /* --- real, honest sanity checks on the generated text itself
     * (structural, not the full behavioral proof -- that's the real
     * compile+run below) --- */
    CHECK(strstr(generated_c, "char * load_config(Arena *buf_arena") != NULL,
          "the real generated C declares load_config with the real, correctly-mangled parameter name");
    CHECK(strstr(generated_c, "arena_strdup(buf_arena, \"parsed_data\", 11)") != NULL,
          "the real generated C emits the real arena_strdup call for the buffer-arena allocation, "
          "using the bare pointer parameter (no '&') -- the real arena-kind distinction this file's "
          "own header comment documents");
    CHECK(strstr(generated_c, "arena_strdup(&scratch, \"config.json\", 11)") != NULL,
          "the real generated C emits '&scratch' for the with-arena-LOCAL allocation -- the same "
          "real arena-kind distinction, the other real branch");
    CHECK(strstr(generated_c, "__attribute__((cleanup(arena_free_all)))") != NULL,
          "the real generated C declares the with-arena-local Arena with the real cleanup attribute "
          "NORTHSTAR.md's own Memory model section requires");

    /* --- real compile + link + run: writes the real generated C to a
     * real temp file, shells out to a real gcc invocation (matching
     * this repo's own real -std=c99 -Wall -Wextra -pedantic -Werror
     * DoD bar), links against the SAME real driver_valid_only.c domain
     * 4's own check already uses, and actually runs the result. --- */
    char c_path[] = "/tmp/parena_selfhost_emit_test_XXXXXX.c";
    /* mkstemps-style: mkstemp needs a fixed-length suffix-free
     * template, so build the path by hand instead (real, simple,
     * avoids a second temp-file API this repo doesn't use elsewhere). */
    snprintf(c_path, sizeof c_path, "/tmp/parena_selfhost_emit_test_%d.c", (int)getpid());
    FILE *out = fopen(c_path, "w");
    CHECK(out != NULL, "a real temp file opens to write the generated C into");
    if (out) {
        fputs(generated_c, out);
        fclose(out);

        char bin_path[256];
        snprintf(bin_path, sizeof bin_path, "%s.bin", c_path);
        char cmd[1024];
        snprintf(cmd, sizeof cmd,
                 "gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -o %s "
                 "tests/integration/driver_valid_only.c %s runtime/parena_runtime.c 2>&1",
                 bin_path, c_path);
        int compile_status = system(cmd);
        CHECK(compile_status == 0,
              "the real generated C compiles clean under gcc -std=c99 -Wall -Wextra -pedantic -Werror, "
              "linked against the real driver_valid_only.c");

        if (compile_status == 0) {
            int run_status = system(bin_path);
            CHECK(run_status == 0,
                  "the real compiled program actually RUNS correctly -- driver_valid_only.c's own "
                  "internal assert(strcmp(result, \"parsed_data\") == 0) passes against the selfhost "
                  "emitter's own real output, not just the C reference's");
        }

        remove(c_path);
        remove(bin_path);
    }

    /* --- real regression test for a real, live segfault found and
     * fixed 2026-08-27 attempting a real self-compile of
     * selfhost/lexer.prn through this very pipeline: emit-let-bindings
     * used to call emit-alloc-call unconditionally on every let-
     * binding's own expr-node, with no check that the node was
     * actually an `alloc` call at all. A plain function-call binding
     * (a real, in-scope PARENA shape this narrow v0 emitter has never
     * claimed to support) made emit-alloc-call read past the real
     * `alloc` call's own expected shape via vec/get's own honest
     * out-of-bounds NULL, then crash on a bare get-field dereference
     * of that NULL. Fixed with a real alloc-call-shaped? guard: a
     * non-alloc-shaped binding now emits a real, clean `#error` line
     * instead. This test proves the fix without needing a real
     * self-compile fixture: same non-alloc-call-shaped let-binding
     * shape, hand-written. */
    {
        char *snippet =
            "(defn f [(a : Arena @ :region/buffer)]\n"
            "  (let [x (some-call a)]\n"
            "    x))";
        Result pr2 = parse_program(snippet, &a);
        CHECK(pr2.tag == 1, "a real non-alloc-shaped let-binding (a plain function call) parses fine");
        if (pr2.tag == 1) {
            Node program2 = *(Node *)pr2.value;
            /* the real regression: this used to segfault the whole
             * process instead of returning. */
            char *generated2 = emit_program(&program2, &a);
            CHECK(generated2 != NULL, "emit-program returns cleanly (does not crash) on a non-alloc-shaped let-binding");
            CHECK(generated2 != NULL && strstr(generated2, "#error") != NULL,
                  "the real generated C carries a clean, honest #error line for the unsupported shape, "
                  "instead of silently guessing at wrong C");
        }
    }

    /* --- real coverage for param-type-name/param-c-type, added
     * 2026-08-27: emit-params used to hard-code EVERY param's own C
     * type as `Arena *` regardless of its real declared type -- a
     * real, confirmed type-correctness bug (this file's own prior
     * header comment already honestly flagged it as a known, narrow
     * v0 gap). selfhost/lexer.prn's own real `new-lexer` function,
     * `(defn new-lexer [(src : String @ Region)] ...)`, is a real,
     * live example: its own single param is String-typed, not
     * Arena-typed. --- */
    {
        char *snippet =
            "(defn string-param-fn [(s : String @ Region)]\n"
            "  s)";
        Result pr3 = parse_program(snippet, &a);
        CHECK(pr3.tag == 1, "a real String-typed param parses fine");
        if (pr3.tag == 1) {
            Node program3 = *(Node *)pr3.value;
            char *generated3 = emit_program(&program3, &a);
            CHECK(generated3 != NULL && strstr(generated3, "char * string_param_fn(char *s") != NULL,
                  "a real String-typed param is declared as a real C char *, not the old hard-coded Arena *");
        }
    }
    {
        char *snippet =
            "(defn i32-param-fn [(n : I32)]\n"
            "  n)";
        Result pr4 = parse_program(snippet, &a);
        CHECK(pr4.tag == 1, "a real I32-typed param parses fine");
        if (pr4.tag == 1) {
            Node program4 = *(Node *)pr4.value;
            char *generated4 = emit_program(&program4, &a);
            CHECK(generated4 != NULL && strstr(generated4, "char * i32_param_fn(int n") != NULL,
                  "a real I32-typed param is declared as a real C int, not the old hard-coded Arena *");
        }
    }
    {
        /* real, honest regression guard: an Arena-typed param must
         * still get its own real "Arena *" C type and still get added
         * to the real ArenaBinding scope (so an alloc call referencing
         * it still resolves bare, no stray '&') -- the whole reason
         * this codepath existed before param-type-name/param-c-type
         * were added. */
        char *snippet =
            "(defn arena-param-fn [(buf-arena : Arena @ :region/buffer)]\n"
            "  (let [x (alloc buf-arena String \"hi\")]\n"
            "    x))";
        Result pr5 = parse_program(snippet, &a);
        CHECK(pr5.tag == 1, "a real Arena-typed param parses fine");
        if (pr5.tag == 1) {
            Node program5 = *(Node *)pr5.value;
            char *generated5 = emit_program(&program5, &a);
            CHECK(generated5 != NULL && strstr(generated5, "char * arena_param_fn(Arena *buf_arena") != NULL,
                  "a real Arena-typed param still gets its own real Arena * C type, zero regression");
            CHECK(generated5 != NULL && strstr(generated5, "arena_strdup(buf_arena, \"hi\", 2)") != NULL,
                  "a real Arena-typed param is still correctly added to the ArenaBinding scope -- "
                  "referenced bare (no stray '&'), zero regression");
        }
    }

    arena_free_all(&a);
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
