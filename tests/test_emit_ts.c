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
#include <stdlib.h>
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
            "  (+ (* (- 1 t) start) (* t (+ end (math/random-f64)))))";
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
            "  (math/floor (+ lo (* (math/random-f64) (+ (- hi lo) 1)))))\n"
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

    /* --- real regression: `(not x)` must lower to TypeScript's `!`, not a bogus call to a
       never-defined `not(...)` function -- the same real gap already fixed in emit.c/emit_java.c/
       BURROW, found here live dogfooding against card_rules.prn's own fx-cond-ok. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn negate [(a : Bool)] : Bool (not a))";
        const char *err = NULL;
        const char *ts = build_ts(&arena, src, &err);
        CHECK(ts != NULL, "(not x) emits successfully");
        CHECK(ts && strstr(ts, "(!(a))") != NULL, "(not x) lowers to TypeScript's ! negation, not a bogus not(...) call");
        arena_free_all(&arena);
    }

    /* --- real regression: I32/I32 division must truncate toward zero (Math.trunc), matching
       C's and Java's own `/` on int operands -- found live dogfooding this emitter against
       DEADWEIGHT's card_rules.prn (2026-09-21), whose packed-bitfield decode chain silently broke
       without this. F64 division must stay plain `/` (bezier_interp.prn's own real, live use). */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn div-i32 [(a : I32) (b : I32)] : I32 (/ a b))";
        const char *err = NULL;
        const char *ts = build_ts(&arena, src, &err);
        CHECK(ts != NULL, "I32/I32 division emits successfully");
        CHECK(ts && strstr(ts, "Math.trunc(a / b)") != NULL, "I32/I32 division lowers to Math.trunc, not bare / (matches C/Java toward-zero truncation)");
        arena_free_all(&arena);
    }
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn div-f64 [(a : F64) (b : F64)] : F64 (/ a b))";
        const char *err = NULL;
        const char *ts = build_ts(&arena, src, &err);
        CHECK(ts != NULL, "F64/F64 division emits successfully");
        CHECK(ts && strstr(ts, "Math.trunc") == NULL, "F64/F64 division stays plain real-number division, no Math.trunc");
        arena_free_all(&arena);
    }

    /* --- real regression: a single expression whose emitted text exceeds 511 characters must not
       be silently truncated -- found live the same session (2026-09-21), the exact same bug
       already found and fixed in emit_java.c's own jb_appendf (PARENA 8141f8f) but never ported
       here. A long chain of nested `if`s (card_rules.prn's own fx-amount shape) reproduces it. */
    {
        Arena arena;
        arena_init(&arena);
        char *src = malloc(8192);
        size_t off = (size_t)snprintf(src, 8192, "(defn long-chain [(id : I32)] : I32 ");
        for (int i = 0; i < 40; i++) {
            off += (size_t)snprintf(src + off, 8192 - off, "(if (= id %d) %d ", i, i * 1000 + 7);
        }
        off += (size_t)snprintf(src + off, 8192 - off, "-1");
        for (int i = 0; i < 40; i++) {
            off += (size_t)snprintf(src + off, 8192 - off, ")");
        }
        off += (size_t)snprintf(src + off, 8192 - off, ")");
        const char *err = NULL;
        const char *ts = build_ts(&arena, src, &err);
        CHECK(ts != NULL, "a long (>511 char) nested expression emits successfully, not a parse/emit error");
        /* The real symptom of the truncation bug: the export keyword for this defn (or the whole
           function) goes missing/garbled because a prior tb_appendf call silently cut off mid-string
           and the next append landed on top of it. A clean, complete function has both its own
           `export function longChain` header AND its closing `}\n\n` -- both survive truncation
           corruption checks that substring search alone might miss individually. */
        CHECK(ts && strstr(ts, "export function longChain") != NULL, "long-expression defn's own function header is intact, not corrupted by buffer truncation");
        CHECK(ts && strlen(ts) > 600, "emitted output is not silently truncated to (or near) the old 512-byte buffer bound");
        free(src);
        arena_free_all(&arena);
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
