/* test_emit_lua.c — real verification for the Lua/Luau emitter (src/emit_lua.c).
 *
 * Same discipline test_emit_ts.c/test_emit_java.c already establish: check emit_lua()'s own success and
 * failure behaviour directly, and assert on the emitted TEXT rather than on an exact string (formatting
 * choices are not the contract). The "does a real interpreter accept this" half is checked separately, by
 * NOOBTOPIA: `scripts/gen_deadweight.sh` emits DEADWEIGHT's rules and citizen policy to Luau and
 * `tests/deadweight_test.lua` runs the real numbers under a real Lua.
 *
 * The cases below are the ones that were actually needed, in order, to port DEADWEIGHT: a rules file of
 * pure integer ladders and a policy file that binds intermediates in a `let`.
 */
#include "../src/arena.h"
#include "../src/ast.h"
#include "../src/emit_lua.h"
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

static const char *build_lua(Arena *arena, const char *src, const char **out_error) {
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
    return emit_lua(arena, program, out_error);
}

int main(void) {
    /* --- the base shape: a module of camelCased functions returning one expression --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *err = NULL;
        const char *lua = build_lua(&arena, "(defn start-hull [] : I32 20)", &err);
        CHECK(lua != NULL, "a zero-arg I32 constant emits successfully");
        CHECK(lua && strstr(lua, "local M = {}") != NULL, "the module table is declared");
        CHECK(lua && strstr(lua, "function M.startHull()") != NULL, "the defn is camelCased and attached to M");
        CHECK(lua && strstr(lua, "return 20") != NULL, "the body returns the literal");
        CHECK(lua && strstr(lua, "return M") != NULL, "the module returns its table");
        arena_free_all(&arena);
    }

    /* --- the target-specific lowerings: `/` is floor division, `=` is equality, `and` stays Lua --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *err = NULL;
        const char *lua = build_lua(&arena,
            "(defn half [(n : I32)] : I32 (/ n 2))\n"
            "(defn same [(a : I32) (b : I32)] : Bool (= a b))\n"
            "(defn both [(a : I32) (b : I32)] : Bool (and (> a 0) (> b 0)))", &err);
        CHECK(lua != NULL, "division, equality and `and` all emit");
        CHECK(lua && strstr(lua, "__idiv(n, 2)") != NULL,
              "`/` lowers to the truncating helper, not Lua's flooring `//`");
        CHECK(lua && strstr(lua, "local function __idiv") != NULL,
              "the truncating helper is defined once in the prologue");
        CHECK(lua && strstr(lua, "q < 0 and q * b ~= a") != NULL,
              "the helper corrects a floored quotient only when the signs disagree");
        CHECK(lua && strstr(lua, "(a == b)") != NULL, "`=` lowers to `==` (Lua's `=` is assignment)");
        CHECK(lua && strstr(lua, "and") != NULL, "`and` is already Lua and is not renamed");
        arena_free_all(&arena);
    }

    /* --- `/` must mean what C means, including for negative dividends -------------------------------
     * `//` floors and C truncates: -70/50 is -2 versus -1. The rating ladder (`rating.prn`) divides a
     * difference that is negative half the time, so the two worlds printed different leaderboard numbers
     * for the same server. The shape is asserted above; the VALUES are asserted against a real interpreter
     * in NOOBTOPIA's tests/deadweight_board_test.lua, which pins ratingGain(1000, 930) == 15 (a -1 term). */
    {
        Arena arena;
        arena_init(&arena);
        const char *err = NULL;
        const char *lua = build_lua(&arena, "(defn term [(mine : I32) (theirs : I32)] : I32 (/ (- theirs mine) 50))", &err);
        CHECK(lua != NULL, "a division of a negative difference emits");
        CHECK(lua && strstr(lua, "__idiv((theirs - mine), 50)") != NULL,
              "the difference is handed to the truncating helper, not to `//`");
        arena_free_all(&arena);
    }

/* --- `let`: the form DEADWEIGHT's citizen policy needed, and the reason this test exists ---
     * A sequence of scoped bindings around one expression has no direct Lua equivalent, so it lowers to
     * an immediately-invoked function. That keeps it ONE expression (so it still nests inside the
     * `(cond and then or else)` an `if` becomes) and keeps `local`'s sequential scope. */
    {
        Arena arena;
        arena_init(&arena);
        const char *err = NULL;
        const char *lua = build_lua(&arena,
            "(defn winrate [(wins : I32) (games : I32)] : I32\n"
            "  (let [w (if (< wins 0) 0 wins)\n"
            "        g (if (< w games) games w)]\n"
            "    (/ (* (+ w 1) 1000) (+ g 2))))", &err);
        CHECK(lua != NULL, "a let with two bindings emits successfully");
        CHECK(lua && strstr(lua, "(function() ") != NULL, "let lowers to an immediately-invoked function");
        CHECK(lua && strstr(lua, "local w = ") != NULL && strstr(lua, "local g = ") != NULL,
              "each binding becomes a local");
        CHECK(lua && strstr(lua, "return ") != NULL && strstr(lua, "end)()") != NULL,
              "the body is returned and the function is invoked");
        /* order matters: `g` must be emitted AFTER `w`, because it reads it */
        if (lua) {
            const char *pw = strstr(lua, "local w = ");
            const char *pg = strstr(lua, "local g = ");
            CHECK(pw && pg && pw < pg, "bindings keep source order, so a later binding can read an earlier one");
        }
    }

    /* --- N-ARY operands: rules modules are written n-ary, and Lua's operators are n-ary too ------------
     * stdlib/shankpit/lighting_rules.prn sums four coverage terms in ONE `+`; refusing that made the whole
     * environment matrix unauthorable for this target -- the file the Roblox half of the merge needed most.
     * Operands fold LEFT, which is C's own associativity, and the emitted text is parenthesised rather
     * than relying on precedence, so the two worlds cannot disagree about which addition happens first. */
    {
        Arena arena;
        arena_init(&arena);
        const char *err = NULL;
        const char *lua = build_lua(&arena,
            "(defn cover-total [(a : I32) (b : I32) (c : I32) (d : I32)] : I32 (+ a b c d))", &err);
        CHECK(lua != NULL, "a four-operand + emits (it used to be a hard refusal)");
        CHECK(lua && strstr(lua, "(((a + b) + c) + d)") != NULL,
              "n-ary + folds left, exactly like C's own associativity");
        arena_free_all(&arena);
    }
    {
        Arena arena;
        arena_init(&arena);
        const char *err = NULL;
        const char *lua = build_lua(&arena, "(defn third [(a : I32) (b : I32) (c : I32)] : I32 (/ a b c))", &err);
        CHECK(lua != NULL, "a three-operand / emits");
        CHECK(lua && strstr(lua, "__idiv(__idiv(a, b), c)") != NULL,
              "n-ary / nests the truncating helper, so arity cannot change the rounding rule");
        arena_free_all(&arena);
    }
    {
        Arena arena;
        arena_init(&arena);
        const char *err = NULL;
        const char *lua = build_lua(&arena,
            "(defn all [(a : I32) (b : I32) (c : I32)] : Bool (and (> a 0) (> b 0) (> c 0)))", &err);
        CHECK(lua != NULL, "a three-operand and emits");
        CHECK(lua && strstr(lua, "((a > 0) and (b > 0))") != NULL, "n-ary and folds left as well");
        arena_free_all(&arena);
    }
    /* --- reserved words: a real .prn binds a variable called `local`, and `local local = ...` is invalid Lua
     * that only an interpreter rejects -- the emitter reported success and wrote it. material_rules.prn's voxel
     * lightmap does exactly this (`let [local ...]`), so this is a case a real file had, not a hypothetical. */
    {
        Arena arena;
        arena_init(&arena);
        const char *err = NULL;
        const char *lua = build_lua(&arena,
            "(defn pick [(end : I32)] : I32 (let [local (+ end 1)] local))", &err);
        CHECK(lua != NULL, "a binding named `local` and a parameter named `end` emit");
        CHECK(lua && strstr(lua, "local local_ = ") != NULL,
              "a reserved binding name gets a trailing underscore");
        CHECK(lua && strstr(lua, "return local_") != NULL && strstr(lua, "return local)") == NULL,
              "and every reference uses the SAME escaped name");
        CHECK(lua && strstr(lua, "end_") != NULL, "so does a reserved parameter name");
        arena_free_all(&arena);
    }

    /* A chained comparison means a<b<c in the source language, and a left fold would mean (a<b)<c -- a
     * comparison against a boolean. Refused BY NAME: nothing in the stdlib writes one, and a refusal that
     * says why beats plausible-looking Lua that answers a different question. */
    {
        Arena arena;
        arena_init(&arena);
        const char *err = NULL;
        const char *lua = build_lua(&arena, "(defn between [(a : I32) (b : I32) (c : I32)] : Bool (< a b c))", &err);
        CHECK(lua == NULL, "a chained comparison is refused rather than silently misfolded");
        CHECK(err && strstr(err, "chained comparison") != NULL, "and the refusal names the reason");
        arena_free_all(&arena);
        if (!lua) printf("  error: %s\n", err ? err : "(none)");
        arena_free_all(&arena);
    }

    /* --- let inside an if, and an if inside a let: the composition the policy actually uses --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *err = NULL;
        const char *lua = build_lua(&arena,
            "(defn lean [(winrate : I32) (meta : I32)] : I32\n"
            "  (if (< meta 0) 0\n"
            "    (let [l (- 1000 winrate)]\n"
            "      (if (>= l 600) 2 (if (<= l 350) 1 0)))))", &err);
        CHECK(lua != NULL, "an if wrapping a let wrapping an if emits successfully");
        CHECK(lua && strstr(lua, "(function() if (meta < 0) then return 0 else return (function() ") != NULL,
              "the let sits inside the if without spilling statements");
        arena_free_all(&arena);
    }

    /* --- an if whose branch is FALSE must stay false --------------------------------------------------
     * This is the bug the DEADWEIGHT match parity vectors found, and the reason `if` is not lowered to
     * `(cond and then or else)` any more. A Bool-returning if-ladder (`fx-cond-ok` is 21 branches) had
     * every false branch fall through into the next `or`, so a condition that answered false reached the
     * ladder's final `roll >= PP` test and answered true -- silently changing the rules in the Luau port
     * only, while the C was correct. The old lowering emits `((ok == 2) and false or true)`, which is
     * ALWAYS true. */
    {
        Arena arena;
        arena_init(&arena);
        const char *err = NULL;
        const char *lua = build_lua(&arena,
            "(defn is-two [(ok : I32)] : Bool (if (= ok 2) false true))", &err);
        CHECK(lua != NULL, "a Bool if with a false then-branch emits successfully");
        CHECK(lua && strstr(lua, "and false or") == NULL,
              "a false branch is NOT folded into Lua's and/or (which would discard it)");
        CHECK(lua && strstr(lua, "if (ok == 2) then return false else return true") != NULL,
              "the if keeps both branches as real branches, so false is a value");
        arena_free_all(&arena);
    }

    /* --- the FFI hatch every other target has: #target {:lua (inline-lua "...")} --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *err = NULL;
        const char *lua = build_lua(&arena,
            "(defn seed-mix [(s : I32)] : I32 #target {:lua (inline-lua \"return bit32.bxor(s, 2654435761)\")})",
            &err);
        CHECK(lua != NULL, "a #target :lua body emits successfully");
        CHECK(lua && strstr(lua, "bit32.bxor") != NULL, "the inline Lua body is passed through verbatim");
        arena_free_all(&arena);
    }

    /* --- a long nested if-ladder must not be truncated (the bug that hit the Java emitter once) --- */
    {
        Arena arena;
        arena_init(&arena);
        char *src = malloc(8192);
        size_t off = (size_t)snprintf(src, 8192, "(defn long-chain [(id : I32)] : I32 ");
        for (int i = 0; i < 40; i++) off += (size_t)snprintf(src + off, 8192 - off, "(if (= id %d) %d ", i, i * 100 + 7);
        off += (size_t)snprintf(src + off, 8192 - off, "-1");
        for (int i = 0; i < 40; i++) off += (size_t)snprintf(src + off, 8192 - off, ")");
        off += (size_t)snprintf(src + off, 8192 - off, ")");
        const char *err = NULL;
        const char *lua = build_lua(&arena, src, &err);
        CHECK(lua != NULL, "a >511 char nested expression emits successfully");
        CHECK(lua && strstr(lua, "function M.longChain(") != NULL, "the long defn's header survives intact");
        CHECK(lua && strstr(lua, "return M") != NULL, "the module still closes correctly after a long body");
        free(src);
        arena_free_all(&arena);
    }

    /* --- honest failures: an unsupported form is a real error naming the emitter, never partial Lua --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *err = NULL;
        const char *lua = build_lua(&arena, "(defn f [(buf : Arena @ :region/scratch)] : I32 0)", &err);
        CHECK(lua == NULL, "a region-annotated parameter is refused, not guessed at");
        CHECK(err != NULL && strstr(err, "emit_lua") != NULL, "the error is attributed to emit_lua");
        arena_free_all(&arena);
    }
    {
        Arena arena;
        arena_init(&arena);
        const char *err = NULL;
        /* a loop form this target still cannot express is refused, not emitted as `M.while(...)`. (This check
         * used `do` until 2026-11-13, when `do` gained a real lowering -- see the next block.) */
        const char *lua = build_lua(&arena, "(defn f [] : I32 (while 1 2))", &err);
        CHECK(lua == NULL, "an unsupported body form is refused rather than partially emitted");
        arena_free_all(&arena);
    }
    {
        Arena arena;
        arena_init(&arena);
        const char *err = NULL;
        /* `do` sequences in order and yields its last expression; a non-call entry becomes `local _ = e` because
         * Lua only accepts calls as bare statements, and dropping it would lose its effects. */
        const char *lua = build_lua(&arena, "(defn f [] : I32 (do 1 2))", &err);
        CHECK(lua && strstr(lua, "(function() local _ = 1; return 2 end)()") != NULL,
              "do lowers to a sequencing IIFE that returns its last expression");
        arena_free_all(&arena);
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
