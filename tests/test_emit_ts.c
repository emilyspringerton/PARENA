/* test_emit_ts.c — real, v0 verification for the new TypeScript emitter (src/emit_ts.c). Same
 * real "check emit_ts()'s own success/failure behavior directly, verify actual tsc acceptance
 * separately" split test_emit.c's own header comment already establishes for the C emitter --
 * this file checks the emitted TEXT (substring checks, matching test_emit.c's own real
 * discipline: "a C string equality check here would be too brittle to whitespace/formatting
 * choices to be the real acceptance bar"); the CI-level "does a real `tsc --noEmit` actually
 * accept this" claim is verified separately (MISHRI's own real `bezierInterp` integration, the
 * proving-ground case this emitter was built against, see PARENA/stdlib/mishri/
 * bezier_interp.prn's own doc comment).
 */
#include "../src/arena.h"
#include "../src/ast.h"
#include "../src/emit_ts.h"
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

static const char *build_ts(Arena *arena, const char *src, const char **out_error) {
    const char *parse_err = NULL;
    Node *program = parse_program(arena, src, strlen(src), &parse_err);
    if (!program) {
        *out_error = parse_err;
        return NULL;
    }
    const char *region_err = region_analyze(arena, program);
    if (region_err) {
        *out_error = region_err;
        return NULL;
    }
    return emit_ts(arena, program, out_error);
}

int main(void) {
    /* --- real, zero-arg I32 constant, the same shape xp_award_mod.prn's own real, first-ever
       PARENA mod uses --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn xp-award [] : I32 60)";
        const char *err = NULL;
        const char *ts = build_ts(&arena, src, &err);
        CHECK(ts != NULL, "zero-arg I32 constant emits successfully");
        if (ts) {
            CHECK(strstr(ts, "export function xpAward(): number {") != NULL, "zero-arg defn name camelCased, typed number, exported");
            CHECK(strstr(ts, "return 60;") != NULL, "zero-arg defn body returns the real literal");
        }
        arena_free_all(&arena);
    }

    /* --- real, scalar-param, if/else + binop shape, the same shape item_drop_mod.prn's own real
       PAPERCRAFT mod uses --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn material-paper [] : I32 0)\n"
            "(defn on-item-for-object-destroyed [(material : I32)] : I32\n"
            "  (if (= material (material-paper)) 1 0))";
        const char *err = NULL;
        const char *ts = build_ts(&arena, src, &err);
        CHECK(ts != NULL, "scalar param + if/else + binop + nested call emits successfully");
        if (ts) {
            CHECK(strstr(ts, "export function materialPaper(): number {") != NULL, "first defn camelCased correctly");
            CHECK(strstr(ts, "export function onItemForObjectDestroyed(material: number): number {") != NULL,
                  "second defn's own scalar param typed number");
            CHECK(strstr(ts, "(material === materialPaper())") != NULL, "= binop lowers to === and the nested zero-arg call is camelCased");
            CHECK(strstr(ts, "? 1 : 0") != NULL, "if/else lowers to a real ternary");
        }
        arena_free_all(&arena);
    }

    /* --- real F64 arithmetic + math/random FFI lowering, the exact real shape
       stdlib/mishri/bezier_interp.prn uses (this emitter's own real proving-ground case) --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn bezier-interp [(start : F64) (end : F64) (t : F64)] : F64\n"
            "  (+ (* (- 1 t) start) (* t (+ end (math/random)))))";
        const char *err = NULL;
        const char *ts = build_ts(&arena, src, &err);
        CHECK(ts != NULL, "F64 params + math/random call emits successfully");
        if (ts) {
            CHECK(strstr(ts, "export function bezierInterp(start: number, end: number, t: number): number {") != NULL,
                  "multi-param F64 defn signature correct");
            CHECK(strstr(ts, "Math.random()") != NULL, "math/random lowers to the real host call Math.random()");
        }
        arena_free_all(&arena);
    }

    /* --- real, grown math-primitive table (2026-08-30, "continue rewriting MISHRI using parena
       using parena mods") -- the exact real shape stdlib/mishri/humanness.prn's own
       randInt/addNoise use --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn rand-int [(lo : F64) (hi : F64)] : F64\n"
            "  (math/floor (+ lo (* (math/random) (+ (- hi lo) 1)))))\n"
            "(defn full-turn [] : F64 (* 2 math/pi))\n"
            "(defn gauss [(u1 : F64) (u2 : F64)] : F64\n"
            "  (* (math/sqrt (* -2 (math/log u1))) (math/cos (* 2 (* math/pi u2)))))";
        const char *err = NULL;
        const char *ts = build_ts(&arena, src, &err);
        CHECK(ts != NULL, "math/floor + math/pi + math/sqrt + math/log + math/cos all emit successfully");
        if (ts) {
            CHECK(strstr(ts, "Math.floor(") != NULL, "math/floor lowers to Math.floor");
            CHECK(strstr(ts, "Math.PI") != NULL, "math/pi lowers to the real Math.PI constant, not a camelCased identifier");
            CHECK(strstr(ts, "Math.sqrt(") != NULL, "math/sqrt lowers to Math.sqrt");
            CHECK(strstr(ts, "Math.log(") != NULL, "math/log lowers to Math.log");
            CHECK(strstr(ts, "Math.cos(") != NULL, "math/cos lowers to Math.cos");
        }
        arena_free_all(&arena);
    }

    /* --- real, honest failure case: a math primitive called with the wrong arity --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn f [] : F64 (math/floor 1 2))"; /* math/floor takes exactly 1 arg */
        const char *err = NULL;
        const char *ts = build_ts(&arena, src, &err);
        CHECK(ts == NULL, "a math primitive called with the wrong arity is a real, honest error, not silently accepted");
        arena_free_all(&arena);
    }

    /* --- real, honest failure case: an Arena/region-annotated parameter is NOT understood by
       this v0 (TypeScript is garbage-collected -- see emit_ts.h's own doc comment for why this
       is a deliberate scope boundary, not a bug) --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn f [(buf : Arena @ :region/scratch)] : I32 0)";
        const char *err = NULL;
        const char *ts = build_ts(&arena, src, &err);
        CHECK(ts == NULL, "Arena/region-annotated parameter is a real, honest unsupported error, not silently guessed");
        CHECK(err != NULL && strstr(err, "emit_ts") != NULL, "the real error message is attributed to emit_ts, not a generic failure");
        arena_free_all(&arena);
    }

    /* --- real, honest failure case: a `let`-block body isn't understood by this v0 (single-
       expression bodies only, matching every real mod call site this emitter has been proven
       against so far) --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn f [] : I32 (let [x 1] x))";
        const char *err = NULL;
        const char *ts = build_ts(&arena, src, &err);
        CHECK(ts == NULL, "a let-block body is a real, honest unsupported error, not silently guessed");
        arena_free_all(&arena);
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
