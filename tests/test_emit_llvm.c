/* test_emit_llvm.c — real, v0 verification for the new LLVM IR emitter (src/emit_llvm.c). Same
 * real "check emit_llvm()'s own success/failure behavior directly, verify actual llc acceptance
 * separately" split test_emit_ts.c/test_emit_java.c's own header comments already establish --
 * this file checks the emitted TEXT (substring checks, same real discipline: exact-string
 * equality would be too brittle to SSA register-numbering/whitespace choices); the "does a real
 * `llc` actually accept this and lower it to correct AVR machine code" claim is verified
 * separately (see PARENA/docs/LLVM_BACKEND_NORTHSTAR.md's own Phase 3 write-up: examples/avr/
 * blink.prn's own real next-led-state, disassembled end to end against real llc + avr-ld output).
 */
#include "../src/arena.h"
#include "../src/ast.h"
#include "../src/emit_llvm.h"
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

static const char *build_llvm(Arena *arena, const char *src, const char **out_error) {
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
    return emit_llvm(arena, program, out_error);
}

int main(void) {
    /* --- real, zero-arg I32 constant --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn xp-award [] : I32 60)";
        const char *err = NULL;
        const char *ir = build_llvm(&arena, src, &err);
        CHECK(ir != NULL, "zero-arg I32 constant emits successfully");
        if (ir) {
            CHECK(strstr(ir, "define i32 @xp_award() {") != NULL, "defn name mangled to snake_case, typed i32");
            CHECK(strstr(ir, "ret i32 60") != NULL, "zero-arg defn body returns the real literal, no fabricated instructions");
        } else {
            printf("  error: %s\n", err);
        }
        arena_free_all(&arena);
    }

    /* --- the exact real shape examples/avr/blink.prn uses: Bool param, `not` --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn next-led-state [(current : Bool)] : Bool\n  (not current))";
        const char *err = NULL;
        const char *ir = build_llvm(&arena, src, &err);
        CHECK(ir != NULL, "Bool param + not emits successfully (the real examples/avr/blink.prn shape)");
        if (ir) {
            CHECK(strstr(ir, "define i1 @next_led_state(i1 %current) {") != NULL,
                  "Bool lowers to i1 for both param and return type");
            CHECK(strstr(ir, "= xor i1 %current, true") != NULL,
                  "not lowers to LLVM's own real xor-against-true idiom, not a fabricated `not` opcode");
            CHECK(strstr(ir, "ret i1 %0") != NULL, "returns the real SSA register the xor produced");
        } else {
            printf("  error: %s\n", err);
        }
        arena_free_all(&arena);
    }

    /* --- scalar param + if/else (-> select) + binop + nested call --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn material-paper [] : I32 0)\n"
            "(defn on-item-for-object-destroyed [(material : I32)] : I32\n"
            "  (if (= material (material-paper)) 1 0))";
        const char *err = NULL;
        const char *ir = build_llvm(&arena, src, &err);
        CHECK(ir != NULL, "scalar param + if/else + binop + nested call emits successfully");
        if (ir) {
            CHECK(strstr(ir, "define i32 @material_paper() {") != NULL, "first defn mangled correctly");
            CHECK(strstr(ir, "define i32 @on_item_for_object_destroyed(i32 %material) {") != NULL,
                  "second defn's own scalar param uses real LLVM `type %name` order");
            CHECK(strstr(ir, "call i32 @material_paper()") != NULL,
                  "nested zero-arg call correctly typed from the two-pass sig table (forward-safe)");
            CHECK(strstr(ir, "icmp eq i32") != NULL, "= lowers to a real icmp eq (i1 result, not a fabricated == token)");
            CHECK(strstr(ir, "select i1") != NULL, "if/else lowers to a real select instruction, not a branch/phi pair");
        } else {
            printf("  error: %s\n", err);
        }
        arena_free_all(&arena);
    }

    /* --- F64 arithmetic: proves the real add/fadd opcode split LLVM (unlike C/TS/Java) needs --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn add-half [(x : F64)] : F64 (+ x 0.5))";
        const char *err = NULL;
        const char *ir = build_llvm(&arena, src, &err);
        CHECK(ir != NULL, "F64 arithmetic emits successfully");
        if (ir) {
            CHECK(strstr(ir, "define double @add_half(double %x) {") != NULL, "F64 lowers to double");
            CHECK(strstr(ir, "= fadd double %x, 0.5") != NULL,
                  "+ on double operands lowers to fadd (not add -- the real, distinct LLVM float opcode)");
        } else {
            printf("  error: %s\n", err);
        }
        arena_free_all(&arena);
    }

    /* --- I32 arithmetic + a bare integer literal needing top-down type disambiguation --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn add-one [(x : I32)] : I32 (+ x 1))";
        const char *err = NULL;
        const char *ir = build_llvm(&arena, src, &err);
        CHECK(ir != NULL, "I32 arithmetic with a bare literal operand emits successfully");
        if (ir) {
            CHECK(strstr(ir, "= add i32 %x, 1") != NULL,
                  "+ on i32 operands lowers to add (the integer opcode, distinct from double's fadd)");
        } else {
            printf("  error: %s\n", err);
        }
        arena_free_all(&arena);
    }

    /* --- real, honest error path: undeclared symbol reference --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn broken [] : I32 undeclared-name)";
        const char *err = NULL;
        const char *ir = build_llvm(&arena, src, &err);
        CHECK(ir == NULL, "reference to an undeclared symbol is a real, honest compile error, not silently-wrong IR");
        CHECK(err != NULL && strstr(err, "undeclared") != NULL, "error message names the real problem");
        arena_free_all(&arena);
    }

    /* --- real, honest error path: an i32-expected literal that looks like a float --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn broken [] : I32 2.0)";
        const char *err = NULL;
        const char *ir = build_llvm(&arena, src, &err);
        CHECK(ir == NULL, "a float-looking literal in I32 context is a real, honest compile error, not invalid IR");
        arena_free_all(&arena);
    }

    /* --- real, honest error path: type mismatch across if branches (a nested call's OWN real,
       independently-computed F64 return type genuinely conflicts with the I32 then-branch --
       proves the mismatch is caught even when neither branch is a bare literal expected_type
       could silently coerce) --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn half [] : F64 0.5)\n"
            "(defn broken [(b : Bool)] : I32 (if b 1 (half)))";
        const char *err = NULL;
        const char *ir = build_llvm(&arena, src, &err);
        CHECK(ir == NULL, "mismatched if-branch types (I32 vs a call's own real F64 return type) is a real, honest compile error");
        arena_free_all(&arena);
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
