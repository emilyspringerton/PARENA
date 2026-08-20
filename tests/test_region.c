/* test_region.c — VS0 DoD domain 2's own verification method:
 * "Automated CLI test suite, 1 positive + 1 negative case." Both real
 * cases are test.prn's own two functions, verified together (one file,
 * one parse, one region_analyze call) exactly as `parena analyze` runs
 * them for real, plus a handful of real edge cases the DoD's minimum
 * bar doesn't itself require but this analyzer's own logic needs
 * covered honestly (nested with-arena, an unconstrained non-alloc let
 * binding not falsely flagged, a same-rank assignment not falsely
 * flagged).
 */
#include "../src/arena.h"
#include "../src/ast.h"
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

static const char *analyze_src(Arena *arena, const char *src) {
    arena_init(arena);
    const char *parse_err = NULL;
    Node *program = parse_program(arena, src, strlen(src), &parse_err);
    if (!program) {
        printf("       (unexpected parse error: %s)\n", parse_err);
        return "PARSE ERROR";
    }
    return region_analyze(arena, program);
}

int main(void) {
    /* --- DoD's own required positive case: test.prn's load-config --- */
    {
        Arena arena;
        const char *err = analyze_src(&arena,
            "(defn load-config [(buf-arena : Arena @ :region/buffer)]\n"
            "  (with-arena [scratch :region/scratch 1024]\n"
            "    (let [temp-str (alloc scratch String \"config.json\")\n"
            "          real-buf (alloc buf-arena String \"parsed_data\")]\n"
            "      real-buf)))");
        CHECK(err == NULL, "valid scratch-to-buffer promotion (test.prn's load-config) analyzes clean");
        arena_free_all(&arena);
    }

    /* --- DoD's own required negative case: test.prn's break-safety,
     * checked against the exact literal error format NORTHSTAR.md's own
     * DoD table specifies. --- */
    {
        Arena arena;
        const char *err = analyze_src(&arena,
            "(defn break-safety [(buf-arena : Arena @ :region/buffer)]\n"
            "  (with-arena [scratch :region/scratch 1024]\n"
            "    (let [bad-str (alloc scratch String \"escaped_memory\")]\n"
            "      (buffer/set-data buf-arena bad-str))))");
        CHECK(err != NULL, "invalid scratch-into-buffer escape (test.prn's break-safety) is rejected");
        if (err) {
            CHECK(strcmp(err, "Compile Error: Escaping region pointer from :region/scratch to "
                              ":region/buffer at line 4") == 0,
                  "error message matches NORTHSTAR.md's own DoD table exactly, including line number");
        }
        arena_free_all(&arena);
    }

    /* --- both functions in one file, as the real test.prn ships them:
     * the first (valid) function must not somehow poison analysis of
     * the second. --- */
    {
        Arena arena;
        const char *err = analyze_src(&arena,
            "(defn load-config [(buf-arena : Arena @ :region/buffer)]\n"
            "  (with-arena [scratch :region/scratch 1024]\n"
            "    (let [real-buf (alloc buf-arena String \"parsed_data\")]\n"
            "      real-buf)))\n"
            "(defn break-safety [(buf-arena : Arena @ :region/buffer)]\n"
            "  (with-arena [scratch :region/scratch 1024]\n"
            "    (let [bad-str (alloc scratch String \"escaped_memory\")]\n"
            "      (buffer/set-data buf-arena bad-str))))");
        CHECK(err != NULL, "a valid function followed by an invalid one still catches the real violation");
        arena_free_all(&arena);
    }

    /* --- edge case: same-rank assignment (buffer into buffer) is not a
     * false positive -- Region(Source) >= Region(Destination) holds
     * when they're equal, not just when Source is strictly longer-lived. --- */
    {
        Arena arena;
        const char *err = analyze_src(&arena,
            "(defn same-rank [(a : Arena @ :region/buffer) (b : Arena @ :region/buffer)]\n"
            "  (buffer/set-data a b))");
        CHECK(err == NULL, "same-rank assignment (buffer into buffer) is not a false positive");
        arena_free_all(&arena);
    }

    /* --- edge case: promoting a longer-lived value into a shorter-lived
     * slot is always safe and must not be flagged -- only the reverse
     * direction is the real invariant violation. --- */
    {
        Arena arena;
        const char *err = analyze_src(&arena,
            "(defn safe-direction [(buf-arena : Arena @ :region/buffer)]\n"
            "  (with-arena [scratch :region/scratch 1024]\n"
            "    (buffer/set-data scratch buf-arena)))");
        CHECK(err == NULL, "assigning a longer-lived (buffer) value into a shorter-lived (scratch) slot is safe");
        arena_free_all(&arena);
    }

    /* --- edge case: a non-alloc let binding (unconstrained rank) must
     * not be treated as an automatic violation just because it has no
     * known region -- real limitation, not a false alarm. --- */
    {
        Arena arena;
        const char *err = analyze_src(&arena,
            "(defn plain-let [(buf-arena : Arena @ :region/buffer)]\n"
            "  (let [n 42]\n"
            "    (buffer/set-data buf-arena n)))");
        CHECK(err == NULL, "an unconstrained (non-alloc) let binding is not falsely flagged as an escape");
        arena_free_all(&arena);
    }

    /* --- edge case: nested with-arena, escape from the innermost scope
     * into the outermost still gets caught through two scope levels. --- */
    {
        Arena arena;
        const char *err = analyze_src(&arena,
            "(defn nested [(buf-arena : Arena @ :region/buffer)]\n"
            "  (with-arena [scratch :region/scratch 1024]\n"
            "    (with-arena [inner :region/scratch 512]\n"
            "      (let [bad (alloc inner String \"x\")]\n"
            "        (buffer/set-data buf-arena bad)))))");
        CHECK(err != NULL, "an escape from a nested with-arena scope is still caught");
        arena_free_all(&arena);
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
