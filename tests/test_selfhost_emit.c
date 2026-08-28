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
     * This fixture's own negative case has moved TWICE now, each time
     * to a genuinely different shape as this file's own real call-
     * argument support widens: first from "a nested call as an
     * argument" (`(some-call (other-call a))`, no longer unsupported
     * as of the same day's nested-call-argument widening -- moved to
     * real, positive coverage below), then from "a nested `alloc` call
     * as an argument" (also no longer unsupported as of the SAME day's
     * later alloc-as-argument widening, see every-call-arg-symbol-or-
     * number?'s own 4th header-comment entry -- also moved to real,
     * positive coverage below). A genuinely different shape now
     * exercises the still-crash-free, still-honest #error path this
     * fixture originally existed to prove: a raw STRING LITERAL as a
     * call argument, which no shape guard in this file accepts yet
     * (every-call-arg-symbol-or-number? never checks kind NString/5 at
     * all) -- a real, separate, not-yet-attempted gap. */
    {
        char *snippet =
            "(defn f [(a : Arena @ :region/buffer)]\n"
            "  (let [x (some-call \"literal\")]\n"
            "    x))";
        Result pr2 = parse_program(snippet, &a);
        CHECK(pr2.tag == 1, "a real unsupported let-binding (a raw string-literal argument) parses fine");
        if (pr2.tag == 1) {
            Node program2 = *(Node *)pr2.value;
            /* the real regression: this used to segfault the whole
             * process instead of returning. */
            char *generated2 = emit_program(&program2, &a);
            CHECK(generated2 != NULL, "emit-program returns cleanly (does not crash) on an unsupported let-binding");
            CHECK(generated2 != NULL && strstr(generated2, "#error") != NULL,
                  "the real generated C carries a clean, honest #error line for the still-unsupported shape "
                  "(a raw string-literal argument), instead of silently guessing at wrong C");
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

    arena_free_all(&a);
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
