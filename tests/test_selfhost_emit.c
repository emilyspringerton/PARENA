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
     * was the real, in-scope shape that first exposed the crash --
     * fixed first by turning it into a clean #error (this test's own
     * original fixture), then, same day, by ACTUALLY SUPPORTING that
     * exact shape (emit-plain-call, below) since it turned out to be
     * the single dominant real blocker (35 of ~57 real self-compile
     * errors) toward selfhost/lexer.prn actually compiling.
     *
     * This fixture's own negative case has moved FOUR times now, each
     * time to a genuinely different shape as this file's own real
     * call-argument support widens: first from "a nested call as an
     * argument" (`(some-call (other-call a))`, no longer unsupported
     * as of the same day's nested-call-argument widening -- moved to
     * real, positive coverage below), then from "a nested `alloc` call
     * as an argument" (also no longer unsupported as of the SAME day's
     * later alloc-as-argument widening), then from "a raw STRING
     * LITERAL as a call argument" (also no longer unsupported as of
     * the SAME day's later string-literal-as-argument widening), then
     * from "a real `or`/`and` boolean expression as a call argument"
     * (also no longer unsupported as of the SAME day's later
     * bool-expr-as-argument widening, see every-call-arg-symbol-or-
     * number?'s own 6th header-comment entry -- also moved to real,
     * positive coverage below). A genuinely different shape now
     * exercises the still-crash-free, still-honest #error path this
     * fixture originally existed to prove: a real `cond` expression as
     * a call argument, which no shape guard in this file accepts yet
     * (cond-call-shaped?/emit-cond are wired into emit-form only,
     * never emit-call-arg -- and, unlike or/and/not, `cond` is
     * documented elsewhere in this file as TAIL-POSITION-ONLY by real
     * design, so this isn't expected to close the same way the others
     * did). */
    {
        char *snippet =
            "(defn f [(a : Arena @ :region/buffer) (b : I32)]\n"
            "  (let [x (some-call (cond (b 1) (true 2)))]\n"
            "    x))";
        Result pr2 = parse_program(snippet, &a);
        CHECK(pr2.tag == 1, "a real unsupported let-binding (a cond-as-argument) parses fine");
        if (pr2.tag == 1) {
            Node program2 = *(Node *)pr2.value;
            /* the real regression: this used to segfault the whole
             * process instead of returning. */
            char *generated2 = emit_program(&program2, &a);
            CHECK(generated2 != NULL, "emit-program returns cleanly (does not crash) on an unsupported let-binding");
            CHECK(generated2 != NULL && strstr(generated2, "#error") != NULL,
                  "the real generated C carries a clean, honest #error line for the still-unsupported shape "
                  "(a cond-as-argument), instead of silently guessing at wrong C");
        }
    }

    /* --- real new coverage, added 2026-08-28: a real `or`/`and`/`not`
     * boolean expression as a call argument (`(f (and a b))`), the
     * exact shape the fixture above used to prove was honestly
     * unsupported before this round -- closes the gap directly, AND
     * fixes a real, live, silently-wrong-C bug found along the way
     * (confirmed via a direct probe, not guessed): `(f (and b b))`
     * used to silently emit `f(and(b, b))`, a call to a NEVER-DEFINED
     * C function named `and`, since plain-call-shaped? never excluded
     * `or`/`and`/`not` from its own name checks the way `alloc` and
     * every binary-op-symbol? name already are -- reachable only since
     * the earlier same-day nested-call-argument widening made
     * plain-call-shaped? itself decide whether a NESTED argument is
     * supported, a position or-and-shaped?/not-shaped? were never
     * wired into. New bool-op-symbol? exclusion fixes the false
     * positive; this new positive coverage proves the REAL fix (real
     * boolean composition, not just a clean #error). `report`'s own
     * body is a trivial `(+ v 0)` binary-op, the same real, already-
     * supported/boxed shape `twice` used in the earlier nested-call-
     * argument work -- deliberately NOT a bare `v` tail, which would
     * hit a real, separate, unrelated, untested gap this narrow
     * emitter's own emit-tail-symbol has: it never boxes a raw I32
     * param passed straight through unchanged, unlike every OTHER real
     * I32-producing shape in this file; not attempted or exercised
     * here. No `if` needed either, sidestepping that real, separate,
     * unrelated gap too. */
    {
        char *snippet =
            "(defn report [(v : I32)] : I32\n"
            "  (+ v 0))\n"
            "(defn check-pair [(x : I32) (y : I32)] : I32\n"
            "  (report (and (> x 0) (> y 0))))";
        Result pr2d = parse_program(snippet, &a);
        CHECK(pr2d.tag == 1, "a real 2-function program, the second passing a real `and` boolean "
                              "expression as an argument to the first, parses fine");
        if (pr2d.tag == 1) {
            Node program2d = *(Node *)pr2d.value;
            char *generated2d = emit_program(&program2d, &a);
            CHECK(generated2d != NULL && strstr(generated2d, "#error") == NULL,
                  "no #error is emitted -- the bool-expr argument is now a real, supported shape, "
                  "not silently falling back to the honest-failure path");
            CHECK(generated2d != NULL &&
                  strstr(generated2d, "report(((x > 0) && (y > 0)))") != NULL,
                  "check-pair's own body emits a real, genuinely composed C expression -- the real "
                  "(x > 0) && (y > 0) boolean composed INLINE as report's own one argument, not "
                  "hoisted into a separate statement, not a bogus call to a function named `and`");

            if (generated2d) {
                char c_path2d[300];
                snprintf(c_path2d, sizeof c_path2d, "/tmp/parena_selfhost_emit_bool_arg_test_%d.c",
                         (int)getpid());
                FILE *out2d = fopen(c_path2d, "w");
                CHECK(out2d != NULL, "a real temp file opens to write the bool-arg generated C into");
                if (out2d) {
                    fputs(generated2d, out2d);
                    fclose(out2d);

                    char bin_path2d[310];
                    snprintf(bin_path2d, sizeof bin_path2d, "%s.bin", c_path2d);
                    char cmd2d[1024];
                    snprintf(cmd2d, sizeof cmd2d,
                             "gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -o %s "
                             "tests/integration/driver_bool_arg.c %s runtime/parena_runtime.c 2>&1",
                             bin_path2d, c_path2d);
                    int compile_status2d = system(cmd2d);
                    CHECK(compile_status2d == 0,
                          "the real bool-arg generated C compiles clean under gcc -std=c99 -Wall "
                          "-Wextra -pedantic -Werror, linked against driver_bool_arg.c");
                    if (compile_status2d == 0) {
                        int run_status2d = system(bin_path2d);
                        CHECK(run_status2d == 0,
                              "the real compiled check-pair genuinely computes the correct boolean "
                              "result on every real input -- driver_bool_arg.c's own internal "
                              "asserts all pass, not just compiles clean");
                    }
                    remove(c_path2d);
                    remove(bin_path2d);
                }
            }
        }
    }

    /* --- real new coverage, added 2026-08-28: real Ok/Err/Some/None
     * construction as a call argument (`(f (Some x))`, `(f None)`),
     * closing TWO more real, live, silently-wrong-C bugs of the exact
     * same class found via the identical direct-probe technique while
     * building this: `(f (Some s))` used to silently emit `f(Some(s))`
     * (a call to a NEVER-DEFINED function `Some`, since `Ok`/`Err`/
     * `Some` were ALSO never excluded from plain-call-shaped?'s own
     * name checks); a bare `(f None)` used to silently emit `f(None)`
     * (referencing a NEVER-DECLARED identifier `None`, since a bare
     * `None` symbol already satisfied the generic bare-symbol-argument
     * fallback with no special-casing at all). New
     * result-option-ctor-symbol? exclusion plus 2 new emit-call-arg
     * dispatch clauses (checked BEFORE the generic bare-symbol
     * fallback, since bare `None` would otherwise still match it)
     * fix both at once with the real, correct runtime constructor
     * calls. */
    {
        char *snippet =
            "(defn wrap-opt [(o : Option)] : I32\n"
            "  (match o ((Some s) 1) (None 0)))\n"
            "(defn make-and-wrap [(s : String @ Region)] : I32\n"
            "  (wrap-opt (Some s)))\n"
            "(defn make-and-wrap-none [] : I32\n"
            "  (wrap-opt None))";
        Result pr2e = parse_program(snippet, &a);
        CHECK(pr2e.tag == 1, "a real 3-function program, the 2nd/3rd each passing a real "
                              "Ok/Err/Some construction or bare None as an argument to the 1st, "
                              "parses fine");
        if (pr2e.tag == 1) {
            Node program2e = *(Node *)pr2e.value;
            char *generated2e = emit_program(&program2e, &a);
            CHECK(generated2e != NULL && strstr(generated2e, "#error") == NULL,
                  "no #error is emitted for either shape -- both are now real, supported call "
                  "arguments, not silently falling back to the honest-failure path");
            CHECK(generated2e != NULL && strstr(generated2e, "wrap_opt(option_some(s))") != NULL,
                  "make-and-wrap's own body emits the real option_some(s) runtime constructor "
                  "call, composed INLINE as wrap-opt's own argument -- not a bogus call to a "
                  "function literally named Some");
            CHECK(generated2e != NULL && strstr(generated2e, "wrap_opt(option_none())") != NULL,
                  "make-and-wrap-none's own body emits the real option_none() runtime constructor "
                  "call for its bare None argument -- not a bogus reference to an undeclared "
                  "identifier named None");

            if (generated2e) {
                char c_path2e[300];
                snprintf(c_path2e, sizeof c_path2e, "/tmp/parena_selfhost_emit_optctor_arg_test_%d.c",
                         (int)getpid());
                FILE *out2e = fopen(c_path2e, "w");
                CHECK(out2e != NULL, "a real temp file opens to write the ctor-as-argument generated C into");
                if (out2e) {
                    fputs(generated2e, out2e);
                    fclose(out2e);

                    char bin_path2e[310];
                    snprintf(bin_path2e, sizeof bin_path2e, "%s.bin", c_path2e);
                    char cmd2e[1024];
                    snprintf(cmd2e, sizeof cmd2e,
                             "gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -o %s "
                             "tests/integration/driver_ctor_as_arg.c %s runtime/parena_runtime.c 2>&1",
                             bin_path2e, c_path2e);
                    int compile_status2e = system(cmd2e);
                    CHECK(compile_status2e == 0,
                          "the real ctor-as-argument generated C compiles clean under gcc -std=c99 "
                          "-Wall -Wextra -pedantic -Werror, linked against driver_ctor_as_arg.c");
                    if (compile_status2e == 0) {
                        int run_status2e = system(bin_path2e);
                        CHECK(run_status2e == 0,
                              "the real compiled make-and-wrap and make-and-wrap-none both "
                              "genuinely construct AND consume real Option values correctly, "
                              "entirely as call arguments -- driver_ctor_as_arg.c's own internal "
                              "asserts all pass, not just compiles clean");
                    }
                    remove(c_path2e);
                    remove(bin_path2e);
                }
            }
        }
    }

    /* --- real new coverage, added 2026-08-28: a raw STRING LITERAL as
     * a call argument (`(f "lit")`), the exact shape the fixture above
     * used to prove was honestly unsupported before this round --
     * closes the "no literals" half of a real, repeatedly-named gap
     * this file's own header comments have carried since the very
     * first binary-op/plain-call work (numbers were closed same-day
     * back then; strings stayed open until now). emit-call-arg's new
     * emit-string-literal branch simply re-wraps the literal's own
     * real, already-decoded :text in C double-quotes -- the SAME real,
     * honest, no-re-escaping convention emit-alloc-call's own literal
     * argument already relies on (see emit-string-literal's own header
     * comment). */
    {
        char *snippet =
            "(defn greet [(name : String @ Region)] : String @ Region\n"
            "  name)\n"
            "(defn greet-world [] : String @ Region\n"
            "  (greet \"world\"))";
        Result pr2c = parse_program(snippet, &a);
        CHECK(pr2c.tag == 1, "a real 2-function program, the second passing a raw string literal "
                              "as an argument to the first, parses fine");
        if (pr2c.tag == 1) {
            Node program2c = *(Node *)pr2c.value;
            char *generated2c = emit_program(&program2c, &a);
            CHECK(generated2c != NULL && strstr(generated2c, "#error") == NULL,
                  "no #error is emitted -- a raw string-literal argument is now a real, supported "
                  "shape, not silently falling back to the honest-failure path");
            CHECK(generated2c != NULL && strstr(generated2c, "greet(\"world\")") != NULL,
                  "greet-world's own body emits a real, correctly re-quoted C string literal "
                  "passed straight through to greet, not dropped or mangled");

            if (generated2c) {
                char c_path2c[300];
                snprintf(c_path2c, sizeof c_path2c, "/tmp/parena_selfhost_emit_string_lit_test_%d.c",
                         (int)getpid());
                FILE *out2c = fopen(c_path2c, "w");
                CHECK(out2c != NULL, "a real temp file opens to write the string-literal generated C into");
                if (out2c) {
                    fputs(generated2c, out2c);
                    fclose(out2c);

                    char bin_path2c[310];
                    snprintf(bin_path2c, sizeof bin_path2c, "%s.bin", c_path2c);
                    char cmd2c[1024];
                    snprintf(cmd2c, sizeof cmd2c,
                             "gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -o %s "
                             "tests/integration/driver_string_literal_arg.c %s runtime/parena_runtime.c 2>&1",
                             bin_path2c, c_path2c);
                    int compile_status2c = system(cmd2c);
                    CHECK(compile_status2c == 0,
                          "the real string-literal generated C compiles clean under gcc -std=c99 "
                          "-Wall -Wextra -pedantic -Werror, linked against "
                          "driver_string_literal_arg.c");
                    if (compile_status2c == 0) {
                        int run_status2c = system(bin_path2c);
                        CHECK(run_status2c == 0,
                              "the real compiled greet-world genuinely returns the correct real "
                              "string, passed as a real string-literal call argument -- "
                              "driver_string_literal_arg.c's own internal asserts all pass, not "
                              "just compiles clean");
                    }
                    remove(c_path2c);
                    remove(bin_path2c);
                }
            }
        }
    }

    /* --- real new coverage, added 2026-08-28: a nested `alloc` call as
     * a call argument (`(f (alloc dest String "lit"))`), the exact
     * shape the fixture just above used to prove was honestly
     * unsupported -- closes the one real exclusion the earlier same-day
     * nested-call-argument widening's own header comment explicitly
     * named as still open. `alloc-call-shaped?`/`emit-alloc-call`
     * already take the same `scope`/`dest` every other expression
     * emitter here does, and the target Arena is always named
     * EXPLICITLY in `alloc`'s own syntax -- nothing about a call-
     * argument position changes that. */
    {
        char *snippet =
            /* wrap-buf defined FIRST, matching this file's own
             * pre-existing, real, separate "no forward prototypes
             * emitted" limitation (emit-program has no prototype pre-
             * pass -- unrelated to this feature, not attempted here;
             * a callee must textually precede its own caller for the
             * generated C to compile). */
            "(defn wrap-buf [(s : String @ Region)] : String @ Region\n"
            "  s)\n"
            "(defn make-buf [(dest : Arena @ :region/buffer)] : String @ Region\n"
            "  (wrap-buf (alloc dest String \"hello\")))";
        Result pr2b = parse_program(snippet, &a);
        CHECK(pr2b.tag == 1, "a real 2-function program, the first passing a nested `alloc` call as "
                              "an argument to the second, parses fine");
        if (pr2b.tag == 1) {
            Node program2b = *(Node *)pr2b.value;
            char *generated2b = emit_program(&program2b, &a);
            CHECK(generated2b != NULL && strstr(generated2b, "#error") == NULL,
                  "no #error is emitted -- a nested alloc-call argument is now a real, supported "
                  "shape, not silently falling back to the honest-failure path");
            CHECK(generated2b != NULL &&
                  strstr(generated2b, "wrap_buf(arena_strdup(dest, \"hello\", 5))") != NULL,
                  "make-buf's own body emits a real, genuinely nested C expression -- the real "
                  "arena_strdup(...) call composed INLINE as wrap_buf's own one argument, not "
                  "hoisted into a separate statement or silently dropped");

            if (generated2b) {
                char c_path2b[300];
                snprintf(c_path2b, sizeof c_path2b, "/tmp/parena_selfhost_emit_nested_alloc_test_%d.c",
                         (int)getpid());
                FILE *out2b = fopen(c_path2b, "w");
                CHECK(out2b != NULL, "a real temp file opens to write the nested-alloc generated C into");
                if (out2b) {
                    fputs(generated2b, out2b);
                    fclose(out2b);

                    char bin_path2b[310];
                    snprintf(bin_path2b, sizeof bin_path2b, "%s.bin", c_path2b);
                    char cmd2b[1024];
                    snprintf(cmd2b, sizeof cmd2b,
                             "gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -o %s "
                             "tests/integration/driver_nested_alloc.c %s runtime/parena_runtime.c 2>&1",
                             bin_path2b, c_path2b);
                    int compile_status2b = system(cmd2b);
                    CHECK(compile_status2b == 0,
                          "the real nested-alloc generated C compiles clean under gcc -std=c99 -Wall "
                          "-Wextra -pedantic -Werror, linked against driver_nested_alloc.c");
                    if (compile_status2b == 0) {
                        int run_status2b = system(bin_path2b);
                        CHECK(run_status2b == 0,
                              "the real compiled make-buf genuinely allocates and returns the "
                              "correct real string, round-tripped through a nested alloc-call "
                              "argument -- driver_nested_alloc.c's own internal asserts all pass, "
                              "not just compiles clean");
                    }
                    remove(c_path2b);
                    remove(bin_path2b);
                }
            }
        }
    }

    /* --- real new coverage, added 2026-08-28: a NESTED call as a call
     * argument (`(f (g x))`), the exact shape the fixture just above
     * used to prove was honestly unsupported -- closes the real,
     * repeatedly-named gap (this file's own prior "no nested calls, no
     * literals" header comments, NORTHSTAR.md's own Self-hosting
     * section). every-call-arg-symbol-or-number? now also accepts a
     * nested plain-call-shaped?/binary-op-call-shaped? argument,
     * emitted through the SAME real emit-plain-call/emit-binary-op
     * this file already trusts for a whole function body. */
    {
        char *snippet =
            "(defn twice [(n : I32)] : I32\n"
            "  (* n 2))\n"
            "(defn add-doubled [(a : I32) (b : I32)] : I32\n"
            "  (+ (twice a) b))";
        Result pr2a = parse_program(snippet, &a);
        CHECK(pr2a.tag == 1, "a real 2-function program, the second calling the first with a "
                              "NESTED call as one argument of a real binary-op, parses fine");
        if (pr2a.tag == 1) {
            Node program2a = *(Node *)pr2a.value;
            char *generated2a = emit_program(&program2a, &a);
            CHECK(generated2a != NULL && strstr(generated2a, "#error") == NULL,
                  "no #error is emitted -- the nested call argument is now a real, supported shape, "
                  "not silently falling back to the honest-failure path");
            CHECK(generated2a != NULL && strstr(generated2a, "(twice(a) + b)") != NULL,
                  "add-doubled's own body emits a real, genuinely nested C expression -- "
                  "twice(a) called INLINE as one operand of the real (... + ...) binary-op, not "
                  "hoisted into a separate statement or silently dropped");

            if (generated2a) {
                char c_path2a[300];
                snprintf(c_path2a, sizeof c_path2a, "/tmp/parena_selfhost_emit_nested_call_test_%d.c",
                         (int)getpid());
                FILE *out2a = fopen(c_path2a, "w");
                CHECK(out2a != NULL, "a real temp file opens to write the nested-call generated C into");
                if (out2a) {
                    fputs(generated2a, out2a);
                    fclose(out2a);

                    char bin_path2a[310];
                    snprintf(bin_path2a, sizeof bin_path2a, "%s.bin", c_path2a);
                    char cmd2a[1024];
                    snprintf(cmd2a, sizeof cmd2a,
                             "gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -o %s "
                             "tests/integration/driver_nested_call.c %s runtime/parena_runtime.c 2>&1",
                             bin_path2a, c_path2a);
                    int compile_status2a = system(cmd2a);
                    CHECK(compile_status2a == 0,
                          "the real nested-call generated C compiles clean under gcc -std=c99 -Wall "
                          "-Wextra -pedantic -Werror, linked against driver_nested_call.c");
                    if (compile_status2a == 0) {
                        int run_status2a = system(bin_path2a);
                        CHECK(run_status2a == 0,
                              "the real compiled add-doubled genuinely computes the correct value on "
                              "every real input (including a negative operand) -- "
                              "driver_nested_call.c's own internal asserts all pass, proving the "
                              "nested call argument's own boxed-int-through-char* value round-trips "
                              "correctly through real C pointer arithmetic, not just compiles clean");
                    }
                    remove(c_path2a);
                    remove(bin_path2a);
                }
            }
        }
    }

    /* --- real new coverage, added 2026-08-27: a plain function-call
     * let-binding with bare-symbol arguments -- the dominant real
     * blocker found via the real self-compile attempt of
     * selfhost/lexer.prn (35 of ~57 real errors were exactly this
     * shape, e.g. lexer.prn's own real
     * `(let [lx0 (selfhost/lexer/new-lexer src)] ...)`) -- is now a
     * real, supported emission, not just a clean #error. --- */
    {
        char *snippet =
            "(defn f [(a : Arena @ :region/buffer) (s : String @ Region)]\n"
            "  (let [x (some-call a s)]\n"
            "    x))";
        Result pr6 = parse_program(snippet, &a);
        CHECK(pr6.tag == 1, "a real plain-call let-binding with bare-symbol args parses fine");
        if (pr6.tag == 1) {
            Node program6 = *(Node *)pr6.value;
            char *generated6 = emit_program(&program6, &a);
            CHECK(generated6 != NULL && strstr(generated6, "#error") == NULL,
                  "a real plain-call let-binding no longer produces a #error -- it's now a real, "
                  "supported emission");
            CHECK(generated6 != NULL && strstr(generated6, "char *x __attribute__((unused)) = some_call(a, s);") != NULL,
                  "the real generated C is a real, correctly-mangled call expression, with the real "
                  "Arena-typed arg 'a' referenced bare (matching the real function-param convention "
                  "resolve-arena-ref already established) and the real String-typed arg 's' also bare");
        }
    }

    /* --- real new coverage: the SAME plain-call shape, but calling a
     * real `/`-qualified cross-module name (selfhost/lexer.prn's own
     * exact real shape) -- proves mangle-call-name's own real
     * strip-to-final-segment logic, not just a bare, unqualified
     * call name. --- */
    {
        char *snippet =
            "(defn f [(s : String @ Region)]\n"
            "  (let [x (selfhost/lexer/new-lexer s)]\n"
            "    x))";
        Result pr7 = parse_program(snippet, &a);
        CHECK(pr7.tag == 1, "a real /-qualified plain-call let-binding parses fine");
        if (pr7.tag == 1) {
            Node program7 = *(Node *)pr7.value;
            char *generated7 = emit_program(&program7, &a);
            CHECK(generated7 != NULL && strstr(generated7, "char *x __attribute__((unused)) = new_lexer(s);") != NULL,
                  "a real /-qualified call name (selfhost/lexer/new-lexer) is correctly stripped down to "
                  "its own real final segment and mangled, matching the real, unprefixed C name the full "
                  "compiler itself already gives every top-level defn");
        }
    }

    /* --- real, honest exclusion coverage: a `vec/`-qualified call as
     * a let-binding value stays a real, clean #error (is-vec-call?'s
     * own header comment explains the real collision-risk reasoning
     * this deliberately, permanently excludes, not a temporary gap). */
    {
        char *snippet =
            "(defn f [(a : Arena @ :region/buffer)]\n"
            "  (let [x (vec/new a)]\n"
            "    x))";
        Result pr8 = parse_program(snippet, &a);
        CHECK(pr8.tag == 1, "a real vec/-qualified let-binding parses fine");
        if (pr8.tag == 1) {
            Node program8 = *(Node *)pr8.value;
            char *generated8 = emit_program(&program8, &a);
            CHECK(generated8 != NULL && strstr(generated8, "#error") != NULL,
                  "a real vec/-qualified let-binding is deliberately, permanently left as a clean #error, "
                  "not guessed at");
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

    {
        /* real gap found and closed 2026-08-28 (verifying build-files):
         * a bare `alloc` call as a defn's ENTIRE body (no with-arena/
         * let wrapper) used to fall through emit-form's own catch-all
         * to emit-tail-symbol, which only correctly handles a bare
         * symbol -- silently emitting an empty `return ;` for anything
         * else, confirmed via direct instrumentation. Now dispatches to
         * emit-tail-expr(emit-alloc-call ...), the same real expression
         * emit-let-value already produces for the identical shape in a
         * let-binding's own value position. */
        char *snippet =
            "(defn bare-alloc-fn [(dest : Arena @ Region)]\n"
            "  (alloc dest String \"hi\"))";
        Result pr6 = parse_program(snippet, &a);
        CHECK(pr6.tag == 1, "a real defn with a bare alloc call as its whole body parses fine");
        if (pr6.tag == 1) {
            Node program6 = *(Node *)pr6.value;
            char *generated6 = emit_program(&program6, &a);
            CHECK(generated6 != NULL && strstr(generated6, "return arena_strdup(dest, \"hi\", 2);") != NULL,
                  "a bare alloc call as the whole body now emits a real return statement, "
                  "not the old silent empty 'return ;'");
        }
    }
    {
        /* same real gap, the plain-function-call half: a bare call to
         * another defn (not alloc, every arg a bare symbol) as a
         * defn's ENTIRE body now dispatches to
         * emit-tail-expr(emit-plain-call ...), the same real
         * expression emit-let-value already produces for the identical
         * shape in a let-binding's own value position. */
        char *snippet =
            "(defn bare-call-fn [(dest : Arena @ Region)]\n"
            "  (bare-alloc-fn dest))";
        Result pr7 = parse_program(snippet, &a);
        CHECK(pr7.tag == 1, "a real defn with a bare plain-call as its whole body parses fine");
        if (pr7.tag == 1) {
            Node program7 = *(Node *)pr7.value;
            char *generated7 = emit_program(&program7, &a);
            CHECK(generated7 != NULL && strstr(generated7, "return bare_alloc_fn(dest);") != NULL,
                  "a bare plain-call as the whole body now emits a real return statement, "
                  "not the old silent empty 'return ;'");
        }
    }

    {
        /* real gap found and closed 2026-08-28 (continuing "removing c
         * ffi when possible"): a bare arithmetic/comparison call
         * (`+`/`-`/`*`/`/`/`=`/`>=`/`<=`/`>`/`<`) with a real number-
         * literal argument used to fail every-call-arg-symbol?'s own
         * "every arg must be a bare symbol" gate entirely (a real,
         * separate, previously-flagged part of the same NORTHSTAR.md
         * gap this whole domain's own commit history already named:
         * "no nested calls, no literals"). Now a real, dedicated
         * binary-op-call-shaped? dispatch emits real C infix syntax,
         * boxed into this emitter's own uniform char* convention via
         * emit-i32-boxed -- verified both structurally (this block)
         * and behaviorally (the real compile+run below, since a type
         * that merely COMPILES isn't proof the real value round-trips
         * correctly through the (char *)(intptr_t) reinterpretation). */
        char *snippet =
            "(defn add-ten [(n : I32)] : I32\n"
            "  (+ n 10))";
        Result pr8 = parse_program(snippet, &a);
        CHECK(pr8.tag == 1, "a real defn with a bare binary-op call as its whole body parses fine");
        if (pr8.tag == 1) {
            Node program8 = *(Node *)pr8.value;
            char *generated8 = emit_program(&program8, &a);
            CHECK(generated8 != NULL && strstr(generated8, "return (char *)(intptr_t)(n + 10);") != NULL,
                  "a bare binary-op call as the whole body emits a real, correctly-boxed C return "
                  "statement -- real infix syntax, not a mangled function call, and not the old "
                  "silent empty 'return ;'");
        }
    }
    {
        /* real, separate gap found while verifying the above: an
         * explicit `: ReturnType` annotation on a defn (the exact same
         * `] : I32` shape kind-code/is-close-token?/etc all already use
         * throughout this very codebase's own real source) used to have
         * its own colon-and-type-symbol children wrongly walked as
         * extra, bogus body statements by emit-body-forms, which always
         * started its own walk at a hard-coded index 3 -- confirmed via
         * direct instrumentation: add-ten's own real body used to emit
         * THREE return statements (an empty one, then 'return I32;',
         * THEN the real one) before body-start-index's own fix. The
         * add-ten check above already proves this is fixed for a simple
         * `] : I32` annotation (only ONE real return statement in its
         * own generated text); this block proves the wider, even more
         * common `] : String @ Region` shape (4 extra children: colon,
         * type, at, region) is fixed too. */
        char *snippet =
            "(defn identity-str [(s : String @ Region)] : String @ Region\n"
            "  s)";
        Result pr9 = parse_program(snippet, &a);
        CHECK(pr9.tag == 1, "a real defn with a ': Type @ Region' return annotation parses fine");
        if (pr9.tag == 1) {
            Node program9 = *(Node *)pr9.value;
            char *generated9 = emit_program(&program9, &a);
            CHECK(generated9 != NULL && strstr(generated9, "{\n    return s;\n}\n") != NULL,
                  "a ': Type @ Region' return annotation's own colon/type/at/region children are "
                  "correctly skipped -- exactly one real return statement, not four extra bogus ones");
        }
    }
    {
        /* real, live compile+run: the add-ten snippet above, actually
         * compiled and called, checking the real returned VALUE (not
         * just that gcc accepts the types) -- see driver_arith.c's own
         * header comment. */
        char *snippet =
            "(defn add-ten [(n : I32)] : I32\n"
            "  (+ n 10))";
        Result pr10 = parse_program(snippet, &a);
        if (pr10.tag == 1) {
            Node program10 = *(Node *)pr10.value;
            char *generated10 = emit_program(&program10, &a);
            if (generated10) {
                char c_path2[] = "/tmp/parena_selfhost_emit_arith_test_XXXXXX.c";
                snprintf(c_path2, sizeof c_path2, "/tmp/parena_selfhost_emit_arith_test_%d.c", (int)getpid());
                FILE *out2 = fopen(c_path2, "w");
                CHECK(out2 != NULL, "a real temp file opens to write the binary-op generated C into");
                if (out2) {
                    fputs(generated10, out2);
                    fclose(out2);

                    char bin_path2[300];
                    snprintf(bin_path2, sizeof bin_path2, "%s.bin", c_path2);
                    char cmd2[1024];
                    snprintf(cmd2, sizeof cmd2,
                             "gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -o %s "
                             "tests/integration/driver_arith.c %s runtime/parena_runtime.c 2>&1",
                             bin_path2, c_path2);
                    int compile_status2 = system(cmd2);
                    CHECK(compile_status2 == 0,
                          "add-ten's own real generated C compiles clean under gcc -std=c99 -Wall "
                          "-Wextra -pedantic -Werror, linked against driver_arith.c");
                    if (compile_status2 == 0) {
                        int run_status2 = system(bin_path2);
                        CHECK(run_status2 == 0,
                              "the real compiled add-ten actually returns the real, correct value 15 "
                              "-- driver_arith.c's own internal assert(result == 15) passes, proving "
                              "emit-i32-boxed's own (char *)(intptr_t) reinterpretation genuinely "
                              "round-trips the value, not just compiles clean");
                    }
                    remove(c_path2);
                    remove(bin_path2);
                }
            }
        }
    }
    {
        /* real gap found and closed 2026-08-28, the single largest
         * remaining gap this domain's own commit history had named:
         * a real, TAIL-POSITION-ONLY `cond` (see body-start-index and
         * cond-call-shaped?'s own header comments in selfhost/
         * emit.prn for the full real reasoning -- a NON-tail cond is a
         * real, separate, architecturally harder problem, not
         * attempted here). Verified both structurally and, more
         * importantly, behaviorally: a real compile+run+assert against
         * all THREE branches, since a cond that merely COMPILES isn't
         * proof the real if/else chain actually branches correctly. */
        char *snippet =
            "(defn classify [(n : I32)] : I32\n"
            "  (cond\n"
            "    ((= n 1) 10)\n"
            "    ((>= n 2) 20)\n"
            "    (true 0)))";
        Result pr11 = parse_program(snippet, &a);
        CHECK(pr11.tag == 1, "a real defn with a tail-position cond as its whole body parses fine");
        if (pr11.tag == 1) {
            Node program11 = *(Node *)pr11.value;
            char *generated11 = emit_program(&program11, &a);
            CHECK(generated11 != NULL && strstr(generated11, "if ((n == 1)) {") != NULL,
                  "the first cond clause emits a real 'if' with the real comparison test, "
                  "not a mangled function call");
            CHECK(generated11 != NULL && strstr(generated11, "} else if ((n >= 2)) {") != NULL,
                  "the second cond clause chains as a real 'else if', not a duplicated/nested 'else'");
            CHECK(generated11 != NULL && strstr(generated11, "} else {\n    return (char *)(intptr_t)0;\n    }\n") != NULL,
                  "the final (true ...) clause emits a real, bare 'else' block -- no redundant "
                  "condition check, and its own literal result is correctly boxed");

            if (generated11) {
                char c_path3[] = "/tmp/parena_selfhost_emit_cond_test_XXXXXX.c";
                snprintf(c_path3, sizeof c_path3, "/tmp/parena_selfhost_emit_cond_test_%d.c", (int)getpid());
                FILE *out3 = fopen(c_path3, "w");
                CHECK(out3 != NULL, "a real temp file opens to write the cond generated C into");
                if (out3) {
                    fputs(generated11, out3);
                    fclose(out3);

                    char bin_path3[300];
                    snprintf(bin_path3, sizeof bin_path3, "%s.bin", c_path3);
                    char cmd3[1024];
                    snprintf(cmd3, sizeof cmd3,
                             "gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -o %s "
                             "tests/integration/driver_cond.c %s runtime/parena_runtime.c 2>&1",
                             bin_path3, c_path3);
                    int compile_status3 = system(cmd3);
                    CHECK(compile_status3 == 0,
                          "classify's own real generated C compiles clean under gcc -std=c99 -Wall "
                          "-Wextra -pedantic -Werror, linked against driver_cond.c");
                    if (compile_status3 == 0) {
                        int run_status3 = system(bin_path3);
                        CHECK(run_status3 == 0,
                              "the real compiled classify actually branches correctly on all THREE "
                              "real inputs -- driver_cond.c's own internal asserts (10/20/0) all "
                              "pass, proving the real if/else-if/else chain genuinely works, not "
                              "just compiles clean");
                    }
                    remove(c_path3);
                    remove(bin_path3);
                }
            }
        }
    }
    {
        /* real, honest negative case: a cond WITHOUT a trailing
         * (true ...) clause is deliberately NOT treated as
         * cond-call-shaped? (this narrow v0's own real requirement,
         * needed to guarantee the emitted if/else chain is provably
         * exhaustive) -- falls back to the pre-existing, honest
         * emit-tail-symbol fallback rather than emitting a real but
         * non-exhaustive if/else chain that could fall off the end of
         * a non-void function. */
        char *snippet =
            "(defn bad-cond [(n : I32)] : I32\n"
            "  (cond\n"
            "    ((= n 1) 10)\n"
            "    ((= n 2) 20)))";
        Result pr12 = parse_program(snippet, &a);
        CHECK(pr12.tag == 1, "a real cond with no trailing (true ...) clause parses fine");
        if (pr12.tag == 1) {
            Node program12 = *(Node *)pr12.value;
            char *generated12 = emit_program(&program12, &a);
            CHECK(generated12 != NULL && strstr(generated12, "if (") == NULL,
                  "a cond with no trailing (true ...) clause is NOT treated as cond-call-shaped? -- "
                  "no real 'if' is emitted, the same honest (if unhelpful-looking) fallback this "
                  "narrow v0 already gives every other unsupported shape, not a silent wrong guess");
        }
    }
    {
        /* real gap found and closed 2026-08-28, continuing the same
         * day's cond work: `or`/`and`/`not` compound tests, one of
         * cond-call-shaped?'s own explicitly-named "not attempted
         * here" exclusions. New, real, RECURSIVE bool-expr-supported?/
         * emit-bool-expr sub-language -- or/and (binary, this
         * codebase's own real "nest pairs, not N-ary" convention,
         * confirmed via is-close-token?'s own real body) and not
         * (unary), each recursively composing further real
         * comparisons or bool-expr sub-expressions. Verified both
         * structurally and, more importantly, behaviorally: a real
         * compile+run+assert against all 7 real branches (both sides
         * of the or, both sides of the and, the not, and the final
         * true fallback), since generated C that merely compiles is
         * not proof the real boolean logic branches correctly. */
        char *snippet =
            "(defn classify2 [(n : I32)] : I32\n"
            "  (cond\n"
            "    ((or (= n 1) (= n 3)) 100)\n"
            "    ((and (>= n 4) (<= n 6)) 200)\n"
            "    ((not (= n 0)) 300)\n"
            "    (true 0)))";
        Result pr13 = parse_program(snippet, &a);
        CHECK(pr13.tag == 1, "a real cond with or/and/not tests parses fine");
        if (pr13.tag == 1) {
            Node program13 = *(Node *)pr13.value;
            char *generated13 = emit_program(&program13, &a);
            CHECK(generated13 != NULL && strstr(generated13, "if (((n == 1) || (n == 3))) {") != NULL,
                  "a real 'or' test emits real C '||', both operands their own real comparisons, "
                  "not a mangled function call");
            CHECK(generated13 != NULL && strstr(generated13, "} else if (((n >= 4) && (n <= 6))) {") != NULL,
                  "a real 'and' test emits real C '&&', chained as a real 'else if'");
            CHECK(generated13 != NULL && strstr(generated13, "} else if ((!(n == 0))) {") != NULL,
                  "a real 'not' test emits real C '!', its own operand a real comparison");

            if (generated13) {
                char c_path4[] = "/tmp/parena_selfhost_emit_boolexpr_test_XXXXXX.c";
                snprintf(c_path4, sizeof c_path4, "/tmp/parena_selfhost_emit_boolexpr_test_%d.c", (int)getpid());
                FILE *out4 = fopen(c_path4, "w");
                CHECK(out4 != NULL, "a real temp file opens to write the or/and/not generated C into");
                if (out4) {
                    fputs(generated13, out4);
                    fclose(out4);

                    char bin_path4[300];
                    snprintf(bin_path4, sizeof bin_path4, "%s.bin", c_path4);
                    char cmd4[1024];
                    snprintf(cmd4, sizeof cmd4,
                             "gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -o %s "
                             "tests/integration/driver_bool_expr.c %s runtime/parena_runtime.c 2>&1",
                             bin_path4, c_path4);
                    int compile_status4 = system(cmd4);
                    CHECK(compile_status4 == 0,
                          "classify2's own real generated C compiles clean under gcc -std=c99 -Wall "
                          "-Wextra -pedantic -Werror, linked against driver_bool_expr.c");
                    if (compile_status4 == 0) {
                        int run_status4 = system(bin_path4);
                        CHECK(run_status4 == 0,
                              "the real compiled classify2 actually branches correctly on all 7 real "
                              "or/and/not inputs -- driver_bool_expr.c's own internal asserts all "
                              "pass, proving the real boolean logic genuinely works, not just "
                              "compiles clean");
                    }
                    remove(c_path4);
                    remove(bin_path4);
                }
            }
        }
    }
    {
        /* real gap found and closed 2026-08-28, closing the second (of
         * two) real exclusions the same-day cond work explicitly
         * named: a plain-call-shaped? predicate function call (e.g.
         * `(is-zero n)`) as a cond test. Needs no new unboxing helper
         * at the CALL site -- a Bool-returning function's own body
         * already goes through this file's real, existing boxing
         * convention on the CALLEE side (emit-i32-boxed makes 0/1 a
         * real NULL/non-NULL char*), and a real C `if
         * (some_char_star_expr)` already treats a non-NULL pointer as
         * truthy. Verified both structurally and behaviorally: a real
         * two-function program (a real predicate PLUS a real cond
         * dispatching to it), compiled and run against both a zero and
         * a non-zero input. */
        char *snippet =
            "(defn is-zero [(n : I32)] : Bool\n"
            "  (= n 0))\n"
            "(defn classify [(n : I32)] : I32\n"
            "  (cond\n"
            "    ((is-zero n) 1)\n"
            "    (true 0)))";
        Result pr14 = parse_program(snippet, &a);
        CHECK(pr14.tag == 1, "a real predicate defn plus a real cond dispatching to it parses fine");
        if (pr14.tag == 1) {
            Node program14 = *(Node *)pr14.value;
            char *generated14 = emit_program(&program14, &a);
            CHECK(generated14 != NULL && strstr(generated14, "if (is_zero(n)) {") != NULL,
                  "a plain-call-shaped predicate test emits a real, direct function call as the "
                  "'if' condition -- no mangled-wrong call, no bogus unboxing cast needed at the "
                  "call site");

            if (generated14) {
                char c_path5[] = "/tmp/parena_selfhost_emit_predcond_test_XXXXXX.c";
                snprintf(c_path5, sizeof c_path5, "/tmp/parena_selfhost_emit_predcond_test_%d.c", (int)getpid());
                FILE *out5 = fopen(c_path5, "w");
                CHECK(out5 != NULL, "a real temp file opens to write the predicate-cond generated C into");
                if (out5) {
                    fputs(generated14, out5);
                    fclose(out5);

                    char bin_path5[300];
                    snprintf(bin_path5, sizeof bin_path5, "%s.bin", c_path5);
                    char cmd5[1024];
                    snprintf(cmd5, sizeof cmd5,
                             "gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -o %s "
                             "tests/integration/driver_predicate_cond.c %s runtime/parena_runtime.c 2>&1",
                             bin_path5, c_path5);
                    int compile_status5 = system(cmd5);
                    CHECK(compile_status5 == 0,
                          "the real predicate-plus-cond generated C compiles clean under gcc "
                          "-std=c99 -Wall -Wextra -pedantic -Werror, linked against "
                          "driver_predicate_cond.c");
                    if (compile_status5 == 0) {
                        int run_status5 = system(bin_path5);
                        CHECK(run_status5 == 0,
                              "the real compiled classify actually dispatches correctly to the "
                              "real predicate on both a zero and a non-zero input -- driver_"
                              "predicate_cond.c's own internal asserts pass, proving the real "
                              "boxed-Bool-as-truthy-pointer convention genuinely round-trips "
                              "correctly across a real function call, not just compiles clean");
                    }
                    remove(c_path5);
                    remove(bin_path5);
                }
            }
        }
    }
    {
        /* real gap found and closed 2026-08-28: top-level defstruct
         * support (a real typedef, plus a real by-value struct-typed
         * param, matching the reference compiler's own confirmed
         * generated shape) and get-field as a real struct field-read
         * expression -- both in tail position (a real accessor
         * function's own natural shape) and as a call/binary-op
         * argument (via the widened every-call-arg-symbol-or-number?/
         * emit-call-arg). Also closes a real, separate, pre-existing,
         * already-flagged gap found along the way: mangle() only ever
         * handled a hyphen, so `is-zero-x?` emitted as the literal,
         * INVALID C identifier `is_zero_x?` -- widened to match the
         * reference compiler's own real mangle() (also converts '/',
         * '!', '?', and strips a leading '!' entirely). Verified both
         * structurally and, more importantly, behaviorally: a real
         * three-form program (a real defstruct, a real accessor, and a
         * real ?-suffixed predicate using get-field inside a
         * comparison), compiled and run against real Point values. */
        char *snippet =
            "(defstruct Point\n"
            "  (x : I32)\n"
            "  (y : I32))\n"
            "(defn get-x [(p : Point)] : I32\n"
            "  (get-field p :x))\n"
            "(defn is-zero-x? [(p : Point)] : Bool\n"
            "  (= (get-field p :x) 0))";
        Result pr15 = parse_program(snippet, &a);
        CHECK(pr15.tag == 1, "a real defstruct plus a real accessor plus a real ?-suffixed predicate parses fine");
        if (pr15.tag == 1) {
            Node program15 = *(Node *)pr15.value;
            char *generated15 = emit_program(&program15, &a);
            CHECK(generated15 != NULL &&
                  strstr(generated15, "typedef struct {\n    int x;\n    int y;\n} Point;\n") != NULL,
                  "the real defstruct emits a real, correctly-shaped C typedef");
            CHECK(generated15 != NULL && strstr(generated15, "char * get_x(Point p") != NULL,
                  "get-x's own struct-typed param passes BY VALUE ('Point p', not 'Point *p'), "
                  "matching the reference compiler's own real, confirmed generated shape");
            CHECK(generated15 != NULL && strstr(generated15, "return (char *)(intptr_t)(p).x;") != NULL,
                  "get-x's own real body reads the real field via a real '(p).x' expression, "
                  "correctly boxed for its own tail position");
            CHECK(generated15 != NULL && strstr(generated15, "char * is_zero_x_(Point p") != NULL,
                  "the real ?-suffixed predicate name mangles to a real, valid C identifier "
                  "(is_zero_x_), not the old invalid 'is_zero_x?'");
            CHECK(generated15 != NULL && strstr(generated15, "((p).x == 0)") != NULL,
                  "get-field composes correctly as a real comparison argument, not just in tail "
                  "position alone");

            if (generated15) {
                char c_path6[] = "/tmp/parena_selfhost_emit_defstruct_test_XXXXXX.c";
                snprintf(c_path6, sizeof c_path6, "/tmp/parena_selfhost_emit_defstruct_test_%d.c", (int)getpid());
                FILE *out6 = fopen(c_path6, "w");
                CHECK(out6 != NULL, "a real temp file opens to write the defstruct generated C into");
                if (out6) {
                    fputs(generated15, out6);
                    fclose(out6);

                    char bin_path6[300];
                    snprintf(bin_path6, sizeof bin_path6, "%s.bin", c_path6);
                    char cmd6[1024];
                    snprintf(cmd6, sizeof cmd6,
                             "gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -o %s "
                             "tests/integration/driver_defstruct.c %s runtime/parena_runtime.c 2>&1",
                             bin_path6, c_path6);
                    int compile_status6 = system(cmd6);
                    CHECK(compile_status6 == 0,
                          "the real defstruct-plus-accessors generated C compiles clean under gcc "
                          "-std=c99 -Wall -Wextra -pedantic -Werror, linked against "
                          "driver_defstruct.c");
                    if (compile_status6 == 0) {
                        int run_status6 = system(bin_path6);
                        CHECK(run_status6 == 0,
                              "the real compiled get-x and is-zero-x? both return the real, correct "
                              "values against real Point instances -- driver_defstruct.c's own "
                              "internal asserts all pass, proving the real struct/get-field/mangle "
                              "machinery genuinely works, not just compiles clean");
                    }
                    remove(c_path6);
                    remove(bin_path6);
                }
            }
        }
    }
    {
        /* real gap found and closed 2026-08-28, honestly flagged as
         * NOT fixed at the end of the defstruct/get-field work above:
         * a bare `or`/`and`/`not` expression at the TOP LEVEL / tail
         * position (a defn whose entire body is `(and ...)`, or a
         * let-binding's own value -- NOT inside a cond's own test
         * position, the only place bool-expr-supported?/emit-bool-expr
         * were reachable from before this). Found while trying to
         * write is-origin? as a real, natural predicate-function body
         * and discovering it silently emitted an empty 'return ;'.
         * Verified both structurally and behaviorally: a real program
         * combining BOTH new positions -- is-origin?'s own top-level
         * `and` (itself composing get-field inside a comparison) and
         * is-boring's own `or` used as a LET-BINDING value, not tail
         * position -- compiled and run against every real branch. */
        char *snippet =
            "(defstruct Point\n"
            "  (x : I32)\n"
            "  (y : I32))\n"
            "(defn is-origin? [(p : Point)] : Bool\n"
            "  (and (= (get-field p :x) 0) (= (get-field p :y) 0)))\n"
            "(defn is-boring [(n : I32)] : Bool\n"
            "  (let [b (or (= n 0) (= n 1))]\n"
            "    b))";
        Result pr16 = parse_program(snippet, &a);
        CHECK(pr16.tag == 1, "a real top-level 'and' body plus a real 'or' let-value both parse fine");
        if (pr16.tag == 1) {
            Node program16 = *(Node *)pr16.value;
            char *generated16 = emit_program(&program16, &a);
            CHECK(generated16 != NULL &&
                  strstr(generated16, "return (char *)(intptr_t)(((p).x == 0) && ((p).y == 0));") != NULL,
                  "is-origin?'s own top-level 'and' body emits real, boxed C -- get-field composed "
                  "inside a comparison composed inside the and, not the old silent empty 'return ;'");
            CHECK(generated16 != NULL &&
                  strstr(generated16, "= (char *)(intptr_t)((n == 0) || (n == 1));") != NULL,
                  "is-boring's own 'or' used as a LET-BINDING value (not tail position) also emits "
                  "real, boxed C, not a clean #error");

            if (generated16) {
                char c_path7[] = "/tmp/parena_selfhost_emit_boolbody_test_XXXXXX.c";
                snprintf(c_path7, sizeof c_path7, "/tmp/parena_selfhost_emit_boolbody_test_%d.c", (int)getpid());
                FILE *out7 = fopen(c_path7, "w");
                CHECK(out7 != NULL, "a real temp file opens to write the bool-body generated C into");
                if (out7) {
                    fputs(generated16, out7);
                    fclose(out7);

                    char bin_path7[300];
                    snprintf(bin_path7, sizeof bin_path7, "%s.bin", c_path7);
                    char cmd7[1024];
                    snprintf(cmd7, sizeof cmd7,
                             "gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -o %s "
                             "tests/integration/driver_bool_body.c %s runtime/parena_runtime.c 2>&1",
                             bin_path7, c_path7);
                    int compile_status7 = system(cmd7);
                    CHECK(compile_status7 == 0,
                          "the real bool-body generated C compiles clean under gcc -std=c99 -Wall "
                          "-Wextra -pedantic -Werror, linked against driver_bool_body.c");
                    if (compile_status7 == 0) {
                        int run_status7 = system(bin_path7);
                        CHECK(run_status7 == 0,
                              "the real compiled is-origin? and is-boring both branch correctly on "
                              "every real input -- driver_bool_body.c's own internal asserts all "
                              "pass, proving the real top-level or/and/not machinery genuinely "
                              "works in BOTH tail position and let-value position, not just "
                              "compiles clean");
                    }
                    remove(c_path7);
                    remove(bin_path7);
                }
            }
        }
    }

    {
        /* real, narrow, TAIL-POSITION-ONLY Result/Option `match`
         * support (2026-08-28): this file's own earlier header comment
         * (right before bool-expr-supported?'s own section) explicitly
         * flagged match as "needs a real tag registry this emitter
         * doesn't have yet" and NOT attempted for cond's own v0. This
         * hardcodes the one real tag mapping (Ok/Some = 1, Err/None =
         * 0) essentially every real match in this whole stdlib already
         * relies on, rather than a general user-defenum registry. */
        char *snippet =
            "(defn describe-result [(r : Result)] : I32\n"
            "  (match r\n"
            "    ((Ok x) 1)\n"
            "    ((Err e) 0)))\n"
            "(defn describe-option [(o : Option)] : I32\n"
            "  (match o\n"
            "    ((Some s) 1)\n"
            "    (None 0)))";
        Result pr17 = parse_program(snippet, &a);
        CHECK(pr17.tag == 1, "a real defn whose whole body is a Result match, plus a second whose "
                              "whole body is an Option match, both parse fine");
        if (pr17.tag == 1) {
            Node program17 = *(Node *)pr17.value;
            char *generated17 = emit_program(&program17, &a);
            CHECK(generated17 != NULL && strstr(generated17, "Result __match_scrutinee = r;") != NULL,
                  "describe-result's own bare-symbol scrutinee emits a real 'Result "
                  "__match_scrutinee = r;' local, not a re-evaluated expression");
            CHECK(generated17 != NULL &&
                  strstr(generated17, "if (__match_scrutinee.tag == 1) {") != NULL,
                  "the real tag-1 (Ok/Some) branch is emitted as a real C 'if', not a mangled call");
            CHECK(generated17 != NULL &&
                  strstr(generated17, "void *x __attribute__((unused)) = __match_scrutinee.value;") !=
                      NULL,
                  "the Ok clause's own payload binding 'x' is a real, block-scoped 'void *' local "
                  "bound to __match_scrutinee.value");
            CHECK(generated17 != NULL && strstr(generated17, "Option __match_scrutinee = o;") != NULL,
                  "describe-option's own bare-symbol scrutinee emits a real 'Option "
                  "__match_scrutinee = o;' local, using the real, correct Option C type (not "
                  "Result) for an Option-typed scrutinee");
            CHECK(generated17 != NULL &&
                  strstr(generated17,
                         "void *e __attribute__((unused)) = __match_scrutinee.value;") != NULL,
                  "the Err clause's own payload binding 'e' is a real, block-scoped 'void *' local");

            if (generated17) {
                char c_path8[] = "/tmp/parena_selfhost_emit_match_test_XXXXXX.c";
                snprintf(c_path8, sizeof c_path8, "/tmp/parena_selfhost_emit_match_test_%d.c",
                         (int)getpid());
                FILE *out8 = fopen(c_path8, "w");
                CHECK(out8 != NULL, "a real temp file opens to write the match generated C into");
                if (out8) {
                    fputs(generated17, out8);
                    fclose(out8);

                    char bin_path8[300];
                    snprintf(bin_path8, sizeof bin_path8, "%s.bin", c_path8);
                    char cmd8[1024];
                    snprintf(cmd8, sizeof cmd8,
                             "gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -o %s "
                             "tests/integration/driver_match.c %s runtime/parena_runtime.c 2>&1",
                             bin_path8, c_path8);
                    int compile_status8 = system(cmd8);
                    CHECK(compile_status8 == 0,
                          "the real match generated C compiles clean under gcc -std=c99 -Wall "
                          "-Wextra -pedantic -Werror, linked against driver_match.c");
                    if (compile_status8 == 0) {
                        int run_status8 = system(bin_path8);
                        CHECK(run_status8 == 0,
                              "the real compiled describe-result and describe-option both "
                              "dispatch correctly on every real tag -- driver_match.c's own "
                              "internal asserts all pass, proving the real if/else tag-dispatch "
                              "chain and payload binding genuinely work, not just compile clean");
                    }
                    remove(c_path8);
                    remove(bin_path8);
                }
            }
        }
    }

    {
        /* real, narrow Result/Option CONSTRUCTION support (2026-08-28):
         * the direct complement to match support above, closing the
         * "constructing an Ok/Err/Some/None value from PARENA itself"
         * gap driver_match.c's own header comment explicitly flagged
         * as not-yet-started the moment match landed. */
        char *snippet =
            "(defn make-ok [(s : String @ Region)] : Result\n"
            "  (Ok s))\n"
            "(defn make-err [(s : String @ Region)] : Result\n"
            "  (Err s))\n"
            "(defn make-some [(s : String @ Region)] : Option\n"
            "  (Some s))\n"
            "(defn make-none [] : Option\n"
            "  None)\n"
            "(defn round-trip-result [(r : Result)] : I32\n"
            "  (match r\n"
            "    ((Ok x) 1)\n"
            "    ((Err e) 0)))\n"
            "(defn round-trip-option [(o : Option)] : I32\n"
            "  (match o\n"
            "    ((Some s) 1)\n"
            "    (None 0)))";
        Result pr18 = parse_program(snippet, &a);
        CHECK(pr18.tag == 1, "a real program with 4 real Ok/Err/Some/None constructor defns plus "
                              "2 real match-based consumer defns parses fine");
        if (pr18.tag == 1) {
            Node program18 = *(Node *)pr18.value;
            char *generated18 = emit_program(&program18, &a);
            CHECK(generated18 != NULL && strstr(generated18, "Result make_ok(") != NULL,
                  "make-ok's own declared 'Result' return type is emitted as the real, concrete "
                  "C return type, not this file's own pre-existing hardcoded 'char *' default");
            CHECK(generated18 != NULL && strstr(generated18, "return result_ok(s);") != NULL,
                  "make-ok's own body emits a real 'return result_ok(s);', the real runtime "
                  "constructor call, not a bogus call to a never-defined function named 'Ok'");
            CHECK(generated18 != NULL && strstr(generated18, "Result make_err(") != NULL,
                  "make-err's own declared 'Result' return type is emitted as the real, concrete "
                  "C return type");
            CHECK(generated18 != NULL && strstr(generated18, "return result_err(s);") != NULL,
                  "make-err's own body emits a real 'return result_err(s);'");
            CHECK(generated18 != NULL && strstr(generated18, "Option make_some(") != NULL,
                  "make-some's own declared 'Option' return type is emitted as the real, concrete "
                  "C return type");
            CHECK(generated18 != NULL && strstr(generated18, "return option_some(s);") != NULL,
                  "make-some's own body emits a real 'return option_some(s);'");
            CHECK(generated18 != NULL && strstr(generated18, "Option make_none(") != NULL,
                  "make-none's own declared 'Option' return type is emitted as the real, concrete "
                  "C return type, even with zero real parameters");
            CHECK(generated18 != NULL && strstr(generated18, "return option_none();") != NULL,
                  "make-none's own bare 'None' body emits a real 'return option_none();', not a "
                  "bogus reference to an undeclared local named 'None'");
            /* Every OTHER already-tested declared return type (I32 on
             * round-trip-result/round-trip-option, String @ Region
             * elsewhere in this whole test file, no annotation at all
             * on driver_valid_only's own load-config) must keep this
             * file's own pre-existing, uniform 'char *' default
             * completely unchanged -- a real, direct regression guard
             * on defn-c-return-type's own fallback branch. */
            CHECK(generated18 != NULL && strstr(generated18, "char * round_trip_result(") != NULL,
                  "round-trip-result's own declared 'I32' return type still gets this file's own "
                  "pre-existing, uniform 'char *' default, unchanged by the new Result/Option "
                  "return-type recognition");
            CHECK(generated18 != NULL && strstr(generated18, "char * round_trip_option(") != NULL,
                  "round-trip-option's own declared 'I32' return type still gets the same "
                  "pre-existing 'char *' default too");

            if (generated18) {
                char c_path9[300];
                snprintf(c_path9, sizeof c_path9, "/tmp/parena_selfhost_emit_ctor_test_%d.c",
                         (int)getpid());
                FILE *out9 = fopen(c_path9, "w");
                CHECK(out9 != NULL, "a real temp file opens to write the ctor generated C into");
                if (out9) {
                    fputs(generated18, out9);
                    fclose(out9);

                    char bin_path9[310];
                    snprintf(bin_path9, sizeof bin_path9, "%s.bin", c_path9);
                    char cmd9[1024];
                    snprintf(cmd9, sizeof cmd9,
                             "gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -o %s "
                             "tests/integration/driver_result_ctor.c %s runtime/parena_runtime.c 2>&1",
                             bin_path9, c_path9);
                    int compile_status9 = system(cmd9);
                    CHECK(compile_status9 == 0,
                          "the real ctor generated C compiles clean under gcc -std=c99 -Wall "
                          "-Wextra -pedantic -Werror, linked against driver_result_ctor.c");
                    if (compile_status9 == 0) {
                        int run_status9 = system(bin_path9);
                        CHECK(run_status9 == 0,
                              "the real compiled make-ok/make-err/make-some/make-none and "
                              "round-trip-result/round-trip-option all genuinely construct AND "
                              "consume real Result/Option values correctly, tag and payload both "
                              "surviving intact -- driver_result_ctor.c's own internal asserts all "
                              "pass, closing the real round trip, not just compiling clean");
                    }
                    remove(c_path9);
                    remove(bin_path9);
                }
            }
        }
    }

    {
        /* real forward-prototype support (2026-08-28): closes a real,
         * honest, repeatedly-worked-around gap -- this file used to
         * emit NO prototypes at all, so a callee had to textually
         * PRECEDE its own caller for the generated C to compile
         * (several of this same day's own earlier test fixtures had
         * to reorder their own functions specifically because of
         * this). Deliberately CALLER-BEFORE-CALLEE order here -- the
         * exact ordering that used to fail. */
        char *snippet =
            "(defn make-buf [(dest : Arena @ :region/buffer)] : String @ Region\n"
            "  (wrap-buf (alloc dest String \"hello\")))\n"
            "(defn wrap-buf [(s : String @ Region)] : String @ Region\n"
            "  s)";
        Result pr19 = parse_program(snippet, &a);
        CHECK(pr19.tag == 1, "a real 2-function program, the CALLER (make-buf) defined BEFORE its "
                              "own callee (wrap-buf), parses fine");
        if (pr19.tag == 1) {
            Node program19 = *(Node *)pr19.value;
            char *generated19 = emit_program(&program19, &a);
            CHECK(generated19 != NULL && strstr(generated19, "char * make_buf(Arena *dest") != NULL &&
                  strstr(generated19, ");\n") != NULL,
                  "a real forward prototype for make-buf is emitted");
            CHECK(generated19 != NULL && strstr(generated19, "char * wrap_buf(char *s") != NULL,
                  "a real forward prototype for wrap-buf is emitted too, even though its own "
                  "definition already textually precedes no one -- every top-level defn gets one "
                  "unconditionally, matching the C reference emitter's own real, established "
                  "convention");

            if (generated19) {
                char c_path10[300];
                snprintf(c_path10, sizeof c_path10, "/tmp/parena_selfhost_emit_forward_ref_test_%d.c",
                         (int)getpid());
                FILE *out10 = fopen(c_path10, "w");
                CHECK(out10 != NULL, "a real temp file opens to write the forward-ref generated C into");
                if (out10) {
                    fputs(generated19, out10);
                    fclose(out10);

                    char bin_path10[310];
                    snprintf(bin_path10, sizeof bin_path10, "%s.bin", c_path10);
                    char cmd10[1024];
                    snprintf(cmd10, sizeof cmd10,
                             "gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -o %s "
                             "tests/integration/driver_forward_ref.c %s runtime/parena_runtime.c 2>&1",
                             bin_path10, c_path10);
                    int compile_status10 = system(cmd10);
                    CHECK(compile_status10 == 0,
                          "the real caller-before-callee generated C compiles clean under gcc "
                          "-std=c99 -Wall -Wextra -pedantic -Werror, linked against "
                          "driver_forward_ref.c -- the exact real gcc 'implicit declaration' error "
                          "this ordering used to trigger is now genuinely gone");
                    if (compile_status10 == 0) {
                        int run_status10 = system(bin_path10);
                        CHECK(run_status10 == 0,
                              "the real compiled make-buf/wrap-buf pair, in caller-before-callee "
                              "order, still computes the correct real value -- "
                              "driver_forward_ref.c's own internal asserts all pass, not just "
                              "compiles clean");
                    }
                    remove(c_path10);
                    remove(bin_path10);
                }
            }
        }
    }

    {
        /* real struct-typed struct field support (2026-08-28): closes
         * one of the two real gaps this file's own defstruct section
         * header comment named the moment defstruct support first
         * landed -- "a field typed as ANOTHER registered struct ...
         * not attempted here". A struct-typed field's own struct must
         * already be registered (declared earlier in the file) --
         * the same real requirement plain C itself imposes on by-value
         * struct nesting (an incomplete type can't be embedded by
         * value), not an arbitrary scope choice. */
        char *snippet =
            "(defstruct Point (x : I32) (y : I32))\n"
            "(defstruct Line (start : Point) (end : Point))\n"
            "(defn point-x [(p : Point)] : I32\n"
            "  (get-field p :x))\n"
            "(defn line-start-x [(l : Line)] : I32\n"
            "  (point-x (get-field l :start)))";
        Result pr20 = parse_program(snippet, &a);
        CHECK(pr20.tag == 1, "a real 2-struct, 2-defn program -- Line's own 'start'/'end' fields "
                              "typed as the already-registered Point struct -- parses fine");
        if (pr20.tag == 1) {
            Node program20 = *(Node *)pr20.value;
            char *generated20 = emit_program(&program20, &a);
            CHECK(generated20 != NULL && strstr(generated20, "    Point start;\n") != NULL,
                  "Line's own 'start' field is emitted as a real 'Point start;' -- correctly typed "
                  "by the already-registered struct name, with exactly ONE space (no double-space "
                  "regression from struct-field-c-type's own new struct-lookup branch)");
            CHECK(generated20 != NULL && strstr(generated20, "    Point end;\n") != NULL,
                  "Line's own 'end' field is emitted correctly too, same real struct type");
            CHECK(generated20 != NULL && strstr(generated20, "point_x((l).start)") != NULL,
                  "line-start-x's own body emits a real, correctly-composed C expression -- the "
                  "real (l).start struct-field read passed straight through as point-x's own "
                  "Point-typed argument, no boxing (a real struct value can't be reinterpreted "
                  "through emit-i32-boxed's own intptr_t trick the way a scalar field already is)");

            if (generated20) {
                char c_path11[300];
                snprintf(c_path11, sizeof c_path11, "/tmp/parena_selfhost_emit_struct_field_test_%d.c",
                         (int)getpid());
                FILE *out11 = fopen(c_path11, "w");
                CHECK(out11 != NULL, "a real temp file opens to write the struct-field generated C into");
                if (out11) {
                    fputs(generated20, out11);
                    fclose(out11);

                    char bin_path11[310];
                    snprintf(bin_path11, sizeof bin_path11, "%s.bin", c_path11);
                    char cmd11[1024];
                    snprintf(cmd11, sizeof cmd11,
                             "gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -o %s "
                             "tests/integration/driver_struct_field_type.c %s runtime/parena_runtime.c 2>&1",
                             bin_path11, c_path11);
                    int compile_status11 = system(cmd11);
                    CHECK(compile_status11 == 0,
                          "the real struct-field generated C compiles clean under gcc -std=c99 "
                          "-Wall -Wextra -pedantic -Werror, linked against "
                          "driver_struct_field_type.c");
                    if (compile_status11 == 0) {
                        int run_status11 = system(bin_path11);
                        CHECK(run_status11 == 0,
                              "the real compiled line-start-x genuinely reads the correct real "
                              "value through a real nested by-value struct composition -- "
                              "driver_struct_field_type.c's own internal asserts all pass, not "
                              "just compiles clean");
                    }
                    remove(c_path11);
                    remove(bin_path11);
                }
            }
        }
    }

    {
        /* real nested/chained get-field support (2026-08-28): closes
         * the "get-field on a NESTED expression" gap get-field-shaped?
         * 's own header comment named the moment get-field support
         * first landed -- real friction found firsthand this same day
         * while verifying struct-typed struct field support (a
         * genuinely natural shape once a struct can itself CONTAIN
         * another struct). */
        char *snippet =
            "(defstruct Point (x : I32) (y : I32))\n"
            "(defstruct Line (start : Point) (end : Point))\n"
            "(defn line-start-x [(l : Line)] : I32\n"
            "  (get-field (get-field l :start) :x))";
        Result pr21 = parse_program(snippet, &a);
        CHECK(pr21.tag == 1, "a real defn whose whole body is a chained, doubly-nested get-field "
                              "(line.start.x-style) parses fine");
        if (pr21.tag == 1) {
            Node program21 = *(Node *)pr21.value;
            char *generated21 = emit_program(&program21, &a);
            CHECK(generated21 != NULL && strstr(generated21, "#error") == NULL,
                  "no #error is emitted -- the chained get-field is now a real, supported shape, "
                  "not silently falling back to the honest-failure path");
            CHECK(generated21 != NULL &&
                  strstr(generated21, "return (char *)(intptr_t)((l).start).x;") != NULL,
                  "line-start-x's own body emits a real, correctly nested C expression -- "
                  "((l).start).x, the inner get-field composed as the outer's own real target, "
                  "boxed via emit-i32-boxed since the innermost field (x) is I32-typed");

            if (generated21) {
                char c_path12[300];
                snprintf(c_path12, sizeof c_path12, "/tmp/parena_selfhost_emit_nested_gf_test_%d.c",
                         (int)getpid());
                FILE *out12 = fopen(c_path12, "w");
                CHECK(out12 != NULL, "a real temp file opens to write the nested-get-field generated C into");
                if (out12) {
                    fputs(generated21, out12);
                    fclose(out12);

                    char bin_path12[310];
                    snprintf(bin_path12, sizeof bin_path12, "%s.bin", c_path12);
                    char cmd12[1024];
                    snprintf(cmd12, sizeof cmd12,
                             "gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -o %s "
                             "tests/integration/driver_nested_get_field.c %s runtime/parena_runtime.c 2>&1",
                             bin_path12, c_path12);
                    int compile_status12 = system(cmd12);
                    CHECK(compile_status12 == 0,
                          "the real nested-get-field generated C compiles clean under gcc "
                          "-std=c99 -Wall -Wextra -pedantic -Werror, linked against "
                          "driver_nested_get_field.c");
                    if (compile_status12 == 0) {
                        int run_status12 = system(bin_path12);
                        CHECK(run_status12 == 0,
                              "the real compiled line-start-x genuinely computes the correct real "
                              "value through the chained field read -- "
                              "driver_nested_get_field.c's own internal asserts all pass, not "
                              "just compiles clean");
                    }
                    remove(c_path12);
                    remove(bin_path12);
                }
            }
        }
    }

    {
        /* real struct-return-type support PLUS a non-get-field nested
         * call as a get-field target (2026-08-28): two of this same
         * day's own remaining still-open gaps, closed together since
         * they're naturally paired -- a function returning a struct
         * BY VALUE is the real, motivating reason a get-field target
         * needs to accept a plain-call in the first place. */
        char *snippet =
            "(defstruct Point (x : I32) (y : I32))\n"
            "(defn identity-point [(p : Point)] : Point\n"
            "  p)\n"
            "(defn point-x-via-identity [(p : Point)] : I32\n"
            "  (get-field (identity-point p) :x))";
        Result pr22 = parse_program(snippet, &a);
        CHECK(pr22.tag == 1, "a real 2-defn program -- identity-point returning its own Point "
                              "param straight through, point-x-via-identity reading a field off "
                              "identity-point's own real call result -- parses fine");
        if (pr22.tag == 1) {
            Node program22 = *(Node *)pr22.value;
            char *generated22 = emit_program(&program22, &a);
            CHECK(generated22 != NULL && strstr(generated22, "#error") == NULL,
                  "no #error is emitted for either shape -- both are now real, supported, not "
                  "silently falling back to the honest-failure path");
            CHECK(generated22 != NULL && strstr(generated22, "Point identity_point(") != NULL,
                  "identity-point's own declared 'Point' return type is emitted as the real, "
                  "concrete C return type, not this file's own pre-existing hardcoded 'char *' "
                  "default -- the same by-value struct-return recognition Result/Option already "
                  "established, now covering a registered struct name too");
            CHECK(generated22 != NULL && strstr(generated22, "return p;") != NULL,
                  "identity-point's own bare-symbol-param body emits a real, unboxed 'return p;' "
                  "-- correct since both p's own real C type and the function's own real C return "
                  "type are now the identical 'Point', no boxing needed or possible");
            CHECK(generated22 != NULL &&
                  strstr(generated22, "(identity_point(p)).x") != NULL,
                  "point-x-via-identity's own body emits a real, genuinely composed C expression "
                  "-- the real identity_point(p) call result read straight through via get-field, "
                  "not a bogus reference to an undeclared bare symbol");

            if (generated22) {
                char c_path13[300];
                snprintf(c_path13, sizeof c_path13, "/tmp/parena_selfhost_emit_struct_ret_test_%d.c",
                         (int)getpid());
                FILE *out13 = fopen(c_path13, "w");
                CHECK(out13 != NULL, "a real temp file opens to write the struct-return generated C into");
                if (out13) {
                    fputs(generated22, out13);
                    fclose(out13);

                    char bin_path13[310];
                    snprintf(bin_path13, sizeof bin_path13, "%s.bin", c_path13);
                    char cmd13[1024];
                    snprintf(cmd13, sizeof cmd13,
                             "gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -o %s "
                             "tests/integration/driver_struct_return_type.c %s runtime/parena_runtime.c 2>&1",
                             bin_path13, c_path13);
                    int compile_status13 = system(cmd13);
                    CHECK(compile_status13 == 0,
                          "the real struct-return generated C compiles clean under gcc -std=c99 "
                          "-Wall -Wextra -pedantic -Werror, linked against "
                          "driver_struct_return_type.c");
                    if (compile_status13 == 0) {
                        int run_status13 = system(bin_path13);
                        CHECK(run_status13 == 0,
                              "the real compiled point-x-via-identity genuinely computes the "
                              "correct real value on every real input, round-tripping a real "
                              "struct through a real by-value function return -- "
                              "driver_struct_return_type.c's own internal asserts all pass, not "
                              "just compiles clean");
                    }
                    remove(c_path13);
                    remove(bin_path13);
                }
            }
        }
    }

    {
        /* real struct-typed-field-as-a-tail-position-return support
         * (2026-08-28): closes the one gap struct-return-type
         * support's own landing explicitly left open the same day --
         * emit-form's own get-field-shaped? dispatch always boxes via
         * emit-i32-boxed, correct only for a scalar field. Special-
         * cased directly in emit-defn (the function's own declared
         * return type is already known there) rather than threading
         * struct-type-awareness through emit-form's own general
         * recursion. */
        char *snippet =
            "(defstruct Point (x : I32) (y : I32))\n"
            "(defstruct Line (start : Point) (end : Point))\n"
            "(defn line-start [(l : Line)] : Point\n"
            "  (get-field l :start))";
        Result pr23 = parse_program(snippet, &a);
        CHECK(pr23.tag == 1, "a real defn whose whole body is a struct-typed get-field tail, "
                              "declared to return that same struct type, parses fine");
        if (pr23.tag == 1) {
            Node program23 = *(Node *)pr23.value;
            char *generated23 = emit_program(&program23, &a);
            CHECK(generated23 != NULL && strstr(generated23, "Point line_start(") != NULL,
                  "line-start's own declared 'Point' return type is emitted as the real, "
                  "concrete C return type");
            CHECK(generated23 != NULL && strstr(generated23, "return (l).start;") != NULL,
                  "line-start's own body emits a real, UNBOXED 'return (l).start;' -- no "
                  "emit-i32-boxed intptr_t cast, which would be invalid C on a real struct value");
            CHECK(generated23 != NULL && strstr(generated23, "intptr_t)(l).start") == NULL,
                  "confirmed the old, wrong boxed form is genuinely gone, not just that the "
                  "correct form happens to also be present");

            if (generated23) {
                char c_path14[300];
                snprintf(c_path14, sizeof c_path14, "/tmp/parena_selfhost_emit_struct_gf_tail_test_%d.c",
                         (int)getpid());
                FILE *out14 = fopen(c_path14, "w");
                CHECK(out14 != NULL, "a real temp file opens to write the struct-gf-tail generated C into");
                if (out14) {
                    fputs(generated23, out14);
                    fclose(out14);

                    char bin_path14[310];
                    snprintf(bin_path14, sizeof bin_path14, "%s.bin", c_path14);
                    char cmd14[1024];
                    snprintf(cmd14, sizeof cmd14,
                             "gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -o %s "
                             "tests/integration/driver_struct_field_tail.c %s runtime/parena_runtime.c 2>&1",
                             bin_path14, c_path14);
                    int compile_status14 = system(cmd14);
                    CHECK(compile_status14 == 0,
                          "the real struct-gf-tail generated C compiles clean under gcc -std=c99 "
                          "-Wall -Wextra -pedantic -Werror, linked against "
                          "driver_struct_field_tail.c");
                    if (compile_status14 == 0) {
                        int run_status14 = system(bin_path14);
                        CHECK(run_status14 == 0,
                              "the real compiled line-start genuinely returns the correct, "
                              "complete struct value (both fields) on every real input -- "
                              "driver_struct_field_tail.c's own internal asserts all pass, not "
                              "just compiles clean");
                    }
                    remove(c_path14);
                    remove(bin_path14);
                }
            }
        }

        /* real, direct regression guard: the pre-existing SCALAR-field
         * get-field tail case (point-x, used throughout this file's
         * own earlier struct-field tests) must still box via
         * emit-i32-boxed exactly as before -- struct-returning-get-
         * field-body?'s own new special case must never fire for an
         * I32-returning function, only a struct-returning one. */
        char *snippet2 =
            "(defstruct Point (x : I32) (y : I32))\n"
            "(defn point-x [(p : Point)] : I32\n"
            "  (get-field p :x))";
        Result pr24 = parse_program(snippet2, &a);
        CHECK(pr24.tag == 1, "a real defn whose whole body is a SCALAR-typed get-field tail "
                              "still parses fine");
        if (pr24.tag == 1) {
            Node program24 = *(Node *)pr24.value;
            char *generated24 = emit_program(&program24, &a);
            CHECK(generated24 != NULL &&
                  strstr(generated24, "return (char *)(intptr_t)(p).x;") != NULL,
                  "point-x's own body still emits the real, correctly BOXED "
                  "'(char *)(intptr_t)(p).x' -- the pre-existing scalar-field behavior, "
                  "genuinely unchanged by this round's new struct-field special case");
        }
    }

    arena_free_all(&a);
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
