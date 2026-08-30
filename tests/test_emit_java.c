/* test_emit_java.c — real, v0 verification for the new Java emitter (src/emit_java.c). Same real
 * "check emit_java()'s own success/failure behavior directly, verify actual javac acceptance
 * separately" split test_emit_ts.c's own header comment already establishes -- this file checks
 * the emitted TEXT (substring checks, same real discipline: a C string equality check here would
 * be too brittle to whitespace/formatting choices to be the real acceptance bar); the "does a
 * real `javac` actually accept this" claim is verified separately, against the real, unmodified
 * stdlib/mishri/bezier_interp.prn and humanness.prn source files already proven for the C and
 * TypeScript targets (see PARENA/CHANGELOG.md's own entry for this real, three-target proof).
 */
#include "../src/arena.h"
#include "../src/ast.h"
#include "../src/emit_java.h"
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

static const char *build_java(Arena *arena, const char *src, const char *class_name, const char **out_error) {
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
    return emit_java(arena, program, class_name, "", out_error);
}

int main(void) {
    /* --- real, zero-arg I32 constant, the same shape xp_award_mod.prn's own real, first-ever
       PARENA mod uses -- also proves the real class-wrapper shape (the one genuinely new,
       Java-specific structural piece over emit_ts.c) --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn xp-award [] : I32 60)";
        const char *err = NULL;
        const char *java = build_java(&arena, src, "XpAward", &err);
        CHECK(java != NULL, "zero-arg I32 constant emits successfully");
        if (java) {
            CHECK(strstr(java, "public final class XpAward {") != NULL, "wrapping class named from the caller-supplied class_name");
            CHECK(strstr(java, "public static int xpAward() {") != NULL, "zero-arg defn name camelCased, typed int, public static");
            CHECK(strstr(java, "return 60;") != NULL, "zero-arg defn body returns the real literal");
            CHECK(java[strlen(java) - 1] == '\n' && strstr(java, "}\n") != NULL, "output is well-formed (closing brace present)");
        }
        arena_free_all(&arena);
    }

    /* --- real, scalar-param, if/else + binop shape, the same shape item_drop_mod.prn's own real
       PAPERCRAFT mod uses -- also proves the real `=` -> `==` (not `===`) Java-specific binop
       difference, and the real `Type name` (not `name: Type`) parameter order --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn material-paper [] : I32 0)\n"
            "(defn on-item-for-object-destroyed [(material : I32)] : I32\n"
            "  (if (= material (material-paper)) 1 0))";
        const char *err = NULL;
        const char *java = build_java(&arena, src, "ItemDrop", &err);
        CHECK(java != NULL, "scalar param + if/else + binop + nested call emits successfully");
        if (java) {
            CHECK(strstr(java, "public static int materialPaper() {") != NULL, "first defn camelCased correctly");
            CHECK(strstr(java, "public static int onItemForObjectDestroyed(int material) {") != NULL,
                  "second defn's own scalar param uses real Java `Type name` order");
            CHECK(strstr(java, "(material == materialPaper())") != NULL,
                  "= binop lowers to Java's own == (not TypeScript's ===), nested zero-arg call camelCased");
            CHECK(strstr(java, "? 1 : 0") != NULL, "if/else lowers to a real ternary");
        }
        arena_free_all(&arena);
    }

    /* --- real F64 (-> double) arithmetic + math/random FFI lowering, the exact real shape
       stdlib/mishri/bezier_interp.prn uses -- proves the real java.lang.Math method-name overlap
       with JS's own Math this emitter deliberately leans on --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn bezier-interp [(start : F64) (end : F64) (t : F64)] : F64\n"
            "  (+ (* (- 1 t) start) (* t (+ end (math/random)))))";
        const char *err = NULL;
        const char *java = build_java(&arena, src, "BezierInterp", &err);
        CHECK(java != NULL, "F64 params + math/random call emits successfully");
        if (java) {
            CHECK(strstr(java, "public static double bezierInterp(double start, double end, double t) {") != NULL,
                  "multi-param F64 defn signature correct (F64 -> double)");
            CHECK(strstr(java, "Math.random()") != NULL, "math/random lowers to the real host call Math.random()");
        }
        arena_free_all(&arena);
    }

    /* --- real, full math-primitive table + math/pi, the exact real shape
       stdlib/mishri/humanness.prn's own randInt/addNoise use --- */
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
        const char *java = build_java(&arena, src, "Humanness", &err);
        CHECK(java != NULL, "math/floor + math/pi + math/sqrt + math/log + math/cos all emit successfully");
        if (java) {
            CHECK(strstr(java, "Math.floor(") != NULL, "math/floor lowers to Math.floor");
            CHECK(strstr(java, "Math.PI") != NULL, "math/pi lowers to the real Math.PI constant, not a camelCased identifier");
            CHECK(strstr(java, "Math.sqrt(") != NULL, "math/sqrt lowers to Math.sqrt");
            CHECK(strstr(java, "Math.log(") != NULL, "math/log lowers to Math.log");
            CHECK(strstr(java, "Math.cos(") != NULL, "math/cos lowers to Math.cos");
        }
        arena_free_all(&arena);
    }

    /* --- real, honest failure case: a math primitive called with the wrong arity --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn f [] : F64 (math/floor 1 2))"; /* math/floor takes exactly 1 arg */
        const char *err = NULL;
        const char *java = build_java(&arena, src, "F", &err);
        CHECK(java == NULL, "a math primitive called with the wrong arity is a real, honest error, not silently accepted");
        arena_free_all(&arena);
    }

    /* --- real, honest failure case: an Arena/region-annotated parameter is NOT understood by
       this v0 (Java is garbage-collected -- see emit_java.h's own doc comment for why this is a
       deliberate scope boundary, not a bug) --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn f [(buf : Arena @ :region/scratch)] : I32 0)";
        const char *err = NULL;
        const char *java = build_java(&arena, src, "F", &err);
        CHECK(java == NULL, "Arena/region-annotated parameter is a real, honest unsupported error, not silently guessed");
        CHECK(err != NULL && strstr(err, "emit_java") != NULL, "the real error message is attributed to emit_java, not a generic failure");
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
        const char *java = build_java(&arena, src, "F", &err);
        CHECK(java == NULL, "a let-block body is a real, honest unsupported error, not silently guessed");
        arena_free_all(&arena);
    }

    /* --- real, standard Maven/Gradle package-declaration convention: a caller passing a real
       package name gets a real `package X;` line; empty stays package-less (every test above
       already covers that default). --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn xp-award [] : I32 60)";
        const char *err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &err);
        CHECK(program != NULL, "package test: source parses");
        if (program) {
            const char *region_err = region_analyze(&arena, program);
            CHECK(region_err == NULL, "package test: region analysis OK");
            const char *java = emit_java(&arena, program, "XpAward", "industrial.einhorn.gta7.generated", &err);
            CHECK(java != NULL, "package test: emits successfully");
            if (java) {
                CHECK(strstr(java, "package industrial.einhorn.gta7.generated;\n\n") != NULL,
                      "real package declaration emitted verbatim when a package name is supplied");
            }
        }
        arena_free_all(&arena);
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
