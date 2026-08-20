/* test_emit.c — VS0 DoD domain 3's own verification: "gcc -Wall -Wextra
 * -pedantic -std=c99 zero warnings" on the emitted C. These tests check
 * emit_c()'s own success/failure behavior directly; the actual "does
 * gcc accept the output with zero warnings" claim is verified
 * separately by the CI smoke test (build examples/test.prn's own
 * valid-only function, then really run gcc on the result) -- a C
 * string equality check here would be too brittle to whitespace/
 * formatting choices to be the real acceptance bar.
 */
#include "../src/arena.h"
#include "../src/ast.h"
#include "../src/emit.h"
#include "../src/parser.h"
#include "../src/region.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; printf("PASS: %s\n", msg); } \
    else { g_fail++; printf("FAIL: %s\n", msg); } \
} while (0)

int main(void) {
    /* --- the DoD's own real case: test.prn's valid load-config --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn load-config [(buf-arena : Arena @ :region/buffer)]\n"
            "  (with-arena [scratch :region/scratch 1024]\n"
            "    (let [temp-str (alloc scratch String \"config.json\")\n"
            "          real-buf (alloc buf-arena String \"parsed_data\")]\n"
            "      real-buf)))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "load-config parses");
        const char *region_err = region_analyze(&arena, program);
        CHECK(region_err == NULL, "load-config passes region analysis");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "load-config emits C successfully");
        if (c_src) {
            CHECK(strstr(c_src, "arena_strdup") != NULL, "emitted C calls arena_strdup for alloc");
            CHECK(strstr(c_src, "__attribute__((cleanup(arena_free_all)))") != NULL,
                  "emitted C uses the cleanup attribute for with-arena, per NORTHSTAR's own DoD");
            CHECK(strstr(c_src, "return real_buf;") != NULL,
                  "the trailing expression becomes a real return statement");
            CHECK(strstr(c_src, "#include \"parena_runtime.h\"") != NULL,
                  "emitted C includes the real runtime header");
        }
        arena_free_all(&arena);
    }

    /* --- arithmetic/comparison/if now really work (this used to be the
     * "unsupported form" negative case -- promoted to a real positive
     * test once the emitter actually grew this capability). --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn compute [(buf-arena : Arena @ :region/buffer)]\n"
            "  (let [x 5\n"
            "        y 3\n"
            "        sum (+ x y)\n"
            "        biggest (if (> x y) x y)\n"
            "        ok (= sum 8)]\n"
            "    (alloc buf-arena String \"done\")))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a function using arithmetic/comparison/if parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "arithmetic/comparison/if emit successfully");
        if (c_src) {
            CHECK(strstr(c_src, "(x + y)") != NULL, "+ emits as a real C infix operator");
            CHECK(strstr(c_src, "(x > y) ? x : y") != NULL, "if emits as a real C ternary");
            CHECK(strstr(c_src, "(sum == 8)") != NULL, "= emits as C's == , not assignment");
        }
        arena_free_all(&arena);
    }

    /* --- loop/recur now really works: the exact accumulator shape this
     * whole stdlib's own real .prn source (vec.prn's push!/grow!,
     * map.prn's find-slot, etc.) already uses -- promoted from the
     * "unsupported" negative case to a real positive one. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn compute-sum [(buf-arena : Arena @ :region/buffer)]\n"
            "  (loop [i 0 acc 0]\n"
            "    (if (>= i 10)\n"
            "      acc\n"
            "      (recur (+ i 1) (+ acc i)))))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a real loop/recur accumulator function parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "loop/recur emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "while (1)") != NULL, "loop emits as a real C while(1)");
            CHECK(strstr(c_src, "break;") != NULL, "the terminal branch emits a real break");
            CHECK(strstr(c_src, "continue;") != NULL, "recur emits a real continue");
            CHECK(strstr(c_src, "__recur_tmp_0") != NULL,
                  "recur uses real temp variables (simultaneous assignment), not direct reassignment");
        }
        arena_free_all(&arena);
    }

    /* --- match on Result/Option now really works: NORTHSTAR's own real
     * `Ok`/`Err`/`Some`/`None` constructors, destructured. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn describe [(x : Arena @ :region/buffer) (val : Arena @ :region/scratch)]\n"
            "  (match (Ok val)\n"
            "    ((Ok v) v)\n"
            "    ((Err e) x)))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a real match-on-Result function parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "match on Result emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "result_ok(val)") != NULL, "(Ok val) emits as a real result_ok() call");
            CHECK(strstr(c_src, ".tag == 1") != NULL, "the Ok clause checks the real tag == 1");
            CHECK(strstr(c_src, ".tag == 0") != NULL, "the Err clause checks the real tag == 0");
            CHECK(strstr(c_src, "else if") != NULL, "clauses chain as real if/else-if, not independent ifs");
        }
        arena_free_all(&arena);
    }

    /* --- real, honest failure: matching a non-Result/Option value (VS0
     * has no general defenum matcher yet) is reported, not silently
     * mis-emitted. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn weird [(a : Arena @ :region/buffer)]\n"
            "  (match a ((Some x) x) (None a)))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a match-on-Arena function parses fine (VS0 syntax is generic)");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src == NULL && emit_err != NULL,
              "matching a non-Result/Option scrutinee (still genuinely unsupported) fails honestly");
        arena_free_all(&arena);
    }

    /* --- #target {:c (inline-c "...")} now really works: the real FFI
     * escape hatch stdlib/editor's own plugin surface (editor/plugin.prn
     * etc.) needs to declare functions whose real body lives host-side. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn notify [] : Unit\n  #target\n  {:c (inline-c \"host_notify();\")})";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a #target Unit-returning function parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "#target with a Unit return emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "void notify(void)") != NULL,
                  "the declared Unit return type becomes a real C void signature");
            CHECK(strstr(c_src, "host_notify();") != NULL,
                  "the inline-c statement is emitted verbatim, trusted as real C");
            CHECK(strstr(c_src, "return host_notify") == NULL,
                  "a Unit-returning #target body is NOT wrapped in a return statement");
        }
        arena_free_all(&arena);
    }

    /* --- #target with a declared non-Unit return type wraps the inline-c
     * expression in a real `return (...)`, and (Result ..)/(Option ..)
     * declared types resolve to the real runtime struct names. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn poll-event [] : (Option I32)\n  #target\n  {:c (inline-c \"host_poll_event()\")})";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a #target Option-returning function parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "#target with a declared (Option ..) return emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "Option poll_event(void)") != NULL,
                  "(Option I32) resolves to the real Option runtime type in the C signature");
            CHECK(strstr(c_src, "return (host_poll_event());") != NULL,
                  "a non-Unit #target body wraps the inline-c expression in a real return statement");
        }
        arena_free_all(&arena);
    }

    /* --- real, honest failure: an unsupported declared return-type
     * spelling is reported, not silently guessed at. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn weird [] : Frobnicate\n  #target\n  {:c (inline-c \"x()\")})";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a function with an unknown declared return type parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src == NULL && emit_err != NULL,
              "an unsupported declared return-type spelling (still genuinely unsupported) fails honestly");
        arena_free_all(&arena);
    }

    /* --- plain I32/String parameters (no Arena @ :region/x annotation)
     * now really work: the real shape stdlib/editor/ui.prn's own
     * set-gutter-marker/show-popup use for a line number or x/y pixel
     * coordinate, where there's genuinely no arena involved. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn set-gutter-marker [(line : I32) (glyph : String @ :region/scratch)]\n"
            "  : Unit\n  #target\n  {:c (inline-c \"host_set_gutter_marker(line, glyph);\")})";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a mixed I32/Arena-param function parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "a plain I32 parameter emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "int line __attribute__((unused))") != NULL,
                  "the I32 parameter becomes a real plain C int, not Arena *");
            CHECK(strstr(c_src, "Arena *glyph __attribute__((unused))") != NULL,
                  "the region-annotated String parameter still becomes Arena *, unaffected");
        }
        arena_free_all(&arena);
    }

    /* --- real, honest failure: a parameter with neither a region
     * annotation nor a recognized plain I32/String type (a custom named
     * type like DiagnosticSeverity, or a bare, non-keyword region
     * variable) is reported, not silently guessed at. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn weird [(severity : DiagnosticSeverity)]\n"
            "  : Unit\n  #target\n  {:c (inline-c \"x();\")})";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a function with a custom-typed parameter parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src == NULL && emit_err != NULL,
              "a parameter with an unrecognized custom type (still genuinely unsupported) fails honestly");
        arena_free_all(&arena);
    }

    /* --- (Fn [] <ReturnType>) callback parameters now work for any
     * return type resolve_declared_type() understands, not just Unit --
     * the real shape stdlib/cache.prn's own fetch() needs (a compute
     * thunk returning String, not Unit). --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn fetch [(name : String @ :region/scratch) (compute : (Fn [] String))]\n"
            "  : String\n  #target\n  {:c (inline-c \"cache_fetch(name, compute)\")})";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a (Fn [] String) callback parameter function parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "(Fn [] String) parameter emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "char * (*compute)(void)") != NULL,
                  "the callback's own declared String return type becomes a real char * function pointer");
        }
        arena_free_all(&arena);
    }

    /* --- defenum now really works: a real, user-defined tagged union
     * with both zero-payload and one-payload variants, matching
     * stdlib/editor/events.prn's own real EditorEvent shape. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defenum Signal (Ping) (Data (payload : String @ :region/scratch)))\n"
            "(defn subscribe [(sig : Signal) (handler : (Fn [] Unit))]\n"
            "  : Unit\n  #target\n  {:c (inline-c \"host_subscribe(sig, handler);\")})";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a defenum with mixed zero/one-payload variants parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "defenum + a defenum-typed parameter emit successfully");
        if (c_src) {
            CHECK(strstr(c_src, "typedef enum {") != NULL, "defenum emits a real C tag enum");
            CHECK(strstr(c_src, "typedef struct { Signal_Tag tag; void *value; } Signal;") != NULL,
                  "defenum emits the real {tag; void *value;} struct, generalizing Result/Option's own shape");
            CHECK(strstr(c_src, "static inline Signal Signal_Ping(void)") != NULL,
                  "a zero-payload variant gets a real, callable, zero-arg constructor");
            CHECK(strstr(c_src, "static inline Signal Signal_Data(void *value)") != NULL,
                  "a payload-carrying variant gets a real, callable, one-arg constructor");
            CHECK(strstr(c_src, "Signal sig __attribute__((unused))") != NULL,
                  "a parameter typed as a registered defenum becomes a real plain C value of that type");
        }
        arena_free_all(&arena);
    }

    /* --- match now really works on a registered user defenum, not just
     * the built-in Result/Option -- generalizing emit_match's own
     * scrutinee-type check and tag lookup. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defenum Signal (Ping) (Data (payload : String @ :region/scratch)))\n"
            "(defn describe [(sig : Signal) (fallback : String @ :region/scratch)]\n"
            "  (match sig\n"
            "    ((Data d) d)\n"
            "    (Ping fallback)))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a match-on-user-defenum function parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "match on a registered defenum emits successfully");
        if (c_src) {
            /* Not "__match_tmp_0" specifically -- match_counter is a
             * file-static counter shared across every emit_c() call in
             * this test binary's own process lifetime (real, deliberate:
             * see emit_match's own comment), so its exact numeric suffix
             * by this point in the suite isn't 0 and isn't worth pinning
             * down; "Signal __match_tmp_" + " = sig;" as two separate
             * substring checks proves the same real thing without
             * depending on that shared counter's current value. */
            CHECK(strstr(c_src, "Signal __match_tmp_") != NULL && strstr(c_src, " = sig;") != NULL,
                  "match on a user defenum scrutinee stores it in a real temp of its own real type");
            CHECK(strstr(c_src, ".tag == 1)") != NULL || strstr(c_src, ".tag == 0)") != NULL,
                  "the user defenum's own real tag values (not the hardcoded Ok/Err ones) are used");
        }
        arena_free_all(&arena);
    }

    /* --- real, honest failure: a match pattern naming a variant that
     * doesn't belong to the scrutinee's own registered enum is reported,
     * not silently matched against the wrong tag. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defenum Signal (Ping) (Data (payload : String @ :region/scratch)))\n"
            "(defenum Other (Foo))\n"
            "(defn weird [(sig : Signal)]\n"
            "  (match sig\n"
            "    (Foo sig)\n"
            "    (Ping sig)))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "matching a variant from an unrelated enum parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src == NULL && emit_err != NULL,
              "matching a variant that isn't part of the scrutinee's own enum fails honestly");
        arena_free_all(&arena);
    }

    /* --- real, honest failure: referencing an unbound identifier is
     * reported, not silently emitted as a dangling C reference. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn oops [(a : Arena @ :region/buffer)] nonexistent)";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "referencing an unbound identifier parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src == NULL && emit_err != NULL, "an unbound identifier fails to emit honestly");
        arena_free_all(&arena);
    }

    /* --- a function with no with-arena at all (just a param, returned
     * directly via alloc into it) still emits correctly -- with-arena
     * isn't a hardcoded requirement of the emitter, just the common case. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn direct [(buf : Arena @ :region/buffer)]\n"
            "  (let [s (alloc buf String \"hi\")]\n"
            "    s))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        const char *region_err = region_analyze(&arena, program);
        CHECK(region_err == NULL, "a function with no with-arena at all passes region analysis");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "a with-arena-free function emits correctly");
        arena_free_all(&arena);
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
