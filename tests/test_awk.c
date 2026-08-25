/* tests/test_awk.c -- real end-to-end verification for stdlib/awk.prn's
 * `expr/eval` and `awk/run`, continuing PARENA's own self-hosting/dogfooding
 * thread (turbogrep and turbosed are real, working, PARENA-compiled tools --
 * turboawk was the missing third, blocked on expr.prn's own `parse-expr`
 * never having been defined, plus several further real bugs found and fixed
 * along the way: see stdlib/expr.prn's and stdlib/awk.prn's own header
 * comments for the full list -- `deref`-vs-pointer-representable Vec
 * elements, the `Map` stdlib module having no real backing implementation at
 * all, several missing `&`/dest-arg call sites, and a real nested
 * let-inside-when-inside-let-inside-when scoping bug, worked around by
 * flattening rather than fixed at the compiler level -- see this file's own
 * final comment for that one).
 *
 * Honest scope, same "test what's actually there" discipline test_json.c/
 * test_yaml.c already establish: `awk.prn`'s own `run-rules` evaluates each
 * matching rule's action via `expr/eval` and discards the result -- there is
 * no `print` statement or any other side-effecting/output primitive
 * anywhere in expr.prn's own expression language yet (it's a pure
 * expression EVALUATOR, not a full awk STATEMENT language). Building a
 * genuinely output-producing `turboawk` CLI would mean designing and adding
 * that missing piece -- real, separate, unstarted follow-up work, not
 * invented here. This test verifies what's ACTUALLY there: expr/eval's own
 * arithmetic/string/variable-binding correctness (in isolation, with real
 * asserted values), and that awk/run's own real read-split-match-evaluate
 * pipeline runs a real multi-line file to completion without error (Ok, not
 * a crash or an Err) -- the strongest honest claim available without an
 * output primitive to assert against.
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "test_awk_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static double expr_num_of(ExprValue v) { return *(double *)v.value; }
static const char *expr_str_of(ExprValue v) { return (const char *)v.value; }

int main(void) {
    Arena a;
    arena_init(&a);

    /* --- expr/eval: arithmetic, string concat, coercion --- */
    {
        Bindings b = bindings_new(&a);
        Result r = eval("3 + 4", &b, &a);
        CHECK(r.tag == 1, "'3 + 4' evaluates as Ok");
        ExprValue v = *(ExprValue *)r.value;
        CHECK(v.tag == ExprValue_TAG_Num && expr_num_of(v) == 7.0, "'3 + 4' == 7");
    }
    {
        Bindings b = bindings_new(&a);
        Result r = eval("10 - 3 * 2", &b, &a);
        ExprValue v = *(ExprValue *)r.value;
        /* No precedence climbing (expr.prn's own header comment states this
         * honestly) -- single-precedence, strict left-to-right, so this is
         * (10 - 3) * 2 = 14, not real arithmetic's 10 - (3*2) = 4. */
        CHECK(r.tag == 1 && v.tag == ExprValue_TAG_Num && expr_num_of(v) == 14.0,
              "'10 - 3 * 2' == 14 (left-to-right, no precedence -- honest, documented scope)");
    }
    {
        Bindings b = bindings_new(&a);
        Result r = eval("\"hello\" . \" world\"", &b, &a);
        ExprValue v = *(ExprValue *)r.value;
        CHECK(r.tag == 1 && v.tag == ExprValue_TAG_Str && strcmp(expr_str_of(v), "hello world") == 0,
              "'\"hello\" . \" world\"' == \"hello world\" (awk's own '.' concat operator)");
    }
    {
        Bindings b = bindings_new(&a);
        bindings_set_(&b, "x", ExprValue_Num(&(double){12.0}), &a);
        Result r = eval("x + 1", &b, &a);
        ExprValue v = *(ExprValue *)r.value;
        CHECK(r.tag == 1 && v.tag == ExprValue_TAG_Num && expr_num_of(v) == 13.0,
              "a bound variable ('x') resolves correctly through Bindings, not just literals");
    }
    {
        Bindings b = bindings_new(&a);
        Result r = eval("unbound_var", &b, &a);
        ExprValue v = *(ExprValue *)r.value;
        CHECK(r.tag == 1 && v.tag == ExprValue_TAG_Num && expr_num_of(v) == 0.0,
              "an unbound variable reads as 0, real awk's own convention");
    }
    {
        /* real coercion: a numeric string literal coerces via '+' */
        Bindings b = bindings_new(&a);
        Result r = eval("\"3\" + 4", &b, &a);
        ExprValue v = *(ExprValue *)r.value;
        CHECK(r.tag == 1 && v.tag == ExprValue_TAG_Num && expr_num_of(v) == 7.0,
              "'\"3\" + 4' == 7 (string-to-number coercion, real awk's own rule)");
    }
    {
        Bindings b = bindings_new(&a);
        Result r = eval("3.5 + 0.5", &b, &a);
        ExprValue v = *(ExprValue *)r.value;
        CHECK(r.tag == 1 && v.tag == ExprValue_TAG_Num && expr_num_of(v) == 4.0,
              "decimal literals parse correctly ('3.5 + 0.5' == 4)");
    }

    /* --- awk/run: the real read-split-match-evaluate pipeline, end to end --- */
    {
        const char *path = "/tmp/test_awk_input.txt";
        FILE *f = fopen(path, "w");
        fputs("alpha beta\ngamma delta epsilon\n", f);
        fclose(f);

        AwkRule *rule = (AwkRule *)arena_alloc(&a, sizeof(AwkRule));
        *rule = AwkRule_new(option_none(), "NR");
        Vec rules = vec_new(&a);
        vec_push_(&rules, rule);
        AwkProgram program = AwkProgram_new(rules);

        Result open_result = file_open((char *)path, OpenMode_Read(), &a);
        CHECK(open_result.tag == 1, "test input file opens");
        FileHandle fh = *(FileHandle *)open_result.value;

        Result run_result = run(fh, &program, &a);
        file_close(fh, &a);
        CHECK(run_result.tag == 1,
              "awk/run processes a real 2-line file end to end without error "
              "(read-line -> split -> rule-match -> bindings -> expr/eval, all real, not stubbed)");

        remove(path);
    }

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}

/* Real, narrow compiler bug found and worked around while writing
 * stdlib/awk.prn's own run-rules (not fixed at the compiler level -- flagged
 * here for whoever picks this up next): a `(let [b (bindings-for rec dest)]
 * (expr/eval ... &b dest))` NESTED two levels inside an outer `(let [rule
 * ...] (when ... <nested-let-here>))` failed with `unknown identifier 'b'`
 * at the inner let's own binding line -- the inner let's own binding never
 * resolved even though `b` is plainly in scope. Flattened to a single `(let
 * [rule ... b ...] (when ... (expr/eval ... &b ...)))` instead (see
 * run-rules' own body) -- works correctly, but the underlying scope-chaining
 * gap for a let-inside-when-inside-let-inside-when shape specifically is
 * real and still open. */
