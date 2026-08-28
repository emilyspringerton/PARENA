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
     * errors) toward selfhost/lexer.prn actually compiling. This
     * fixture now exercises the still-crash-free, still-honest #error
     * path via a shape plain-call-shaped? deliberately still excludes
     * (a nested call as an argument -- every-call-arg-symbol?'s own
     * header comment explains why), so the original regression this
     * test existed for stays covered. */
    {
        char *snippet =
            "(defn f [(a : Arena @ :region/buffer)]\n"
            "  (let [x (some-call (other-call a))]\n"
            "    x))";
        Result pr2 = parse_program(snippet, &a);
        CHECK(pr2.tag == 1, "a real unsupported let-binding (a nested-call argument) parses fine");
        if (pr2.tag == 1) {
            Node program2 = *(Node *)pr2.value;
            /* the real regression: this used to segfault the whole
             * process instead of returning. */
            char *generated2 = emit_program(&program2, &a);
            CHECK(generated2 != NULL, "emit-program returns cleanly (does not crash) on an unsupported let-binding");
            CHECK(generated2 != NULL && strstr(generated2, "#error") != NULL,
                  "the real generated C carries a clean, honest #error line for the still-unsupported shape "
                  "(a nested-call argument), instead of silently guessing at wrong C");
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

    arena_free_all(&a);
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
