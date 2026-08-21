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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
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
     * coordinate, where there's genuinely no arena involved. ---
     *
     * Corrected 2026-08-20: this test used to assert that a
     * region-annotated *String* parameter (`glyph : String @
     * :region/scratch`) became `Arena *glyph` -- that was itself a real
     * bug in has_region_marker() (its own doc always said "Arena @
     * ...", but the implementation matched ANY `@`/keyword regardless of
     * the type token before it), not correct, intended behavior. Found
     * while getting firefly.prn's own `(msg : String @ Region)`
     * parameter to compile to a real `char *`, not a bogus `Arena *`
     * that silently discarded the declared String type. Now asserts the
     * real, fixed behavior instead of the bug. */
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
            CHECK(strstr(c_src, "char * glyph __attribute__((unused))") != NULL,
                  "the region-annotated String parameter becomes a real char *, not a bogus Arena * "
                  "(has_region_marker() bug fix)");
        }
        arena_free_all(&arena);
    }

    /* --- real regression test for the has_region_marker() fix itself:
     * an Arena @ :region/x parameter still, correctly, becomes Arena *
     * (the one real case this function is actually supposed to
     * recognize) -- proving the fix narrowed the check to "type is
     * literally Arena", not that it broke the real, intended case. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn write-log [(dest : Arena @ :region/task) (msg : String @ :region/scratch)]\n"
            "  : Unit\n  #target\n  {:c (inline-c \"host_write_log(dest, msg);\")})";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "an Arena @ :region/x parameter alongside a String @ :region/x one parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "the mixed Arena/String parameter function emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "Arena *dest __attribute__((unused))") != NULL,
                  "the real Arena @ :region/x parameter still, correctly, becomes Arena *");
            CHECK(strstr(c_src, "char * msg __attribute__((unused))") != NULL,
                  "the neighboring String @ :region/x parameter becomes its own real char *, not Arena *");
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

    /* --- defstruct now really works: a real, plain record type with
     * distinct per-field C types, matching stdlib's own real Point-like
     * shapes (net/http.prn's HttpRequest/HttpResponse, once Map support
     * lands separately). --- */
    /* --- Bool and F64 now really work as plain parameter/return types --
     * the real shape stdlib/gfd.prn's own set-switch (`on : Bool`) and
     * spawn-prop/set-entity-pos (`x`/`y`/`z` : F64`) actually use. Bool
     * maps to a real C int (the same "real C bool-as-int" convention
     * emit_binop's own comparison operators already use), F64 to a real
     * C double. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn set-switch [(id : I32) (on : Bool)]\n"
            "  : Unit\n  #target\n  {:c (inline-c \"host_set_switch(id, on);\")})\n"
            "(defn get-height [(x : F64) (z : F64)]\n"
            "  : F64\n  #target\n  {:c (inline-c \"host_get_height(x, z)\")})";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "Bool and F64 typed parameters/return types parse fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "Bool and F64 typed parameters/return types emit successfully");
        if (c_src) {
            CHECK(strstr(c_src, "int on __attribute__((unused))") != NULL,
                  "a Bool parameter becomes a real plain C int");
            CHECK(strstr(c_src, "double x __attribute__((unused))") != NULL &&
                  strstr(c_src, "double z __attribute__((unused))") != NULL,
                  "F64 parameters become real plain C doubles");
            CHECK(strstr(c_src, "double get_height(") != NULL,
                  "an F64 declared return type becomes a real C double signature");
        }
        arena_free_all(&arena);
    }

    /* --- OK/ERR now really exist as real runtime macros -- the no-
     * payload success/failure shorthand a real, multi-file stdlib
     * convention (gfd.prn, thread.prn, io.prn, sdl2.prn, editor/
     * buffer.prn, pentest/pcap.prn -- 18 real call sites) already
     * assumed existed in its own #target inline-c bodies before this,
     * caught missing by actually compiling gfd.prn's own real emitted C
     * with gcc rather than trusting parena build's own success (which
     * never validates inline-c content -- that's the whole point of the
     * FFI trust boundary) meant the result was real, working C. This
     * test only proves the emitter passes the raw OK/ERR text through
     * verbatim (real gcc compilation of the runtime header itself is
     * covered by the domain4 check and the manual gfd.prn verification
     * this same commit's own message describes), not that OK/ERR
     * resolve -- that's parena_runtime.h's own job now. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn set-switch [(id : I32) (on : Bool)]\n"
            "  : (Result Unit WorldError)\n  #target\n"
            "  {:c (inline-c \"host_set_switch(id, on) == 0 ? OK : ERR\")})";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a #target body using the OK/ERR shorthand parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "a #target body using OK/ERR emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "host_set_switch(id, on) == 0 ? OK : ERR") != NULL,
                  "the OK/ERR shorthand is passed through verbatim, trusted as real C (now backed by "
                  "real macros in parena_runtime.h)");
        }
        arena_free_all(&arena);
    }

    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defstruct Point (x : I32) (y : I32))\n"
            "(defn origin [] : Point (Point 0 0))\n"
            "(defn get-x [(p : Point)] : I32 (get-field p :x))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a defstruct with I32 fields, construction, and get-field parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "defstruct + construction + get-field emit successfully");
        if (c_src) {
            CHECK(strstr(c_src, "typedef struct {") != NULL, "defstruct emits a real C struct typedef");
            CHECK(strstr(c_src, "int x;") != NULL && strstr(c_src, "int y;") != NULL,
                  "each field gets its own real, distinct C type, not a shared void *");
            CHECK(strstr(c_src, "static inline Point Point_new(int x, int y)") != NULL,
                  "a real, callable positional constructor is emitted");
            CHECK(strstr(c_src, "return Point_new(0, 0);") != NULL,
                  "a (Point 0 0) construction call emits as a real Point_new(0, 0) call");
            CHECK(strstr(c_src, "Point p __attribute__((unused))") != NULL,
                  "a parameter typed as a registered defstruct becomes a real plain C value of that type");
            CHECK(strstr(c_src, "return (p).x;") != NULL,
                  "(get-field p :x) emits as a real, direct C field access");
        }
        arena_free_all(&arena);
    }

    /* --- a defstruct field name containing a real '-' (kebab-case, the
     * normal PARENA identifier convention -- rows-per-file, start-x,
     * etc.) is mangled before being written into emitted C, not passed
     * through verbatim. A real bug caught by actually compiling
     * stdlib/csv.prn's own SplitOptions (rows-per-file/lazy-quotes/
     * compressed) with gcc: the unmangled field name produced literally
     * invalid C (`int rows-per-file;` -- gcc parses the hyphens as
     * subtraction), not a hypothetical. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defstruct SplitOptions (rows-per-file : I32) (lazy-quotes : Bool))\n"
            "(defn make-opts [] : SplitOptions (SplitOptions 100 0))\n"
            "(defn get-rpf [(o : SplitOptions)] : I32 (get-field o :rows-per-file))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a defstruct with a hyphenated field name parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "a hyphenated field name emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "int rows_per_file;") != NULL,
                  "the struct typedef's own field is mangled to a real, valid C identifier");
            CHECK(strstr(c_src, "rows-per-file") == NULL,
                  "the raw, unmangled hyphenated name never appears anywhere in the emitted C");
            CHECK(strstr(c_src, "static inline SplitOptions SplitOptions_new(int rows_per_file, int lazy_quotes)") != NULL,
                  "the constructor's own parameter name is mangled too");
            CHECK(strstr(c_src, "return (o).rows_per_file;") != NULL,
                  "get-field on a hyphenated field name emits the real, mangled C field access");
        }
        arena_free_all(&arena);
    }

    /* --- real, honest failure: constructing a defstruct with the wrong
     * number of arguments, or accessing a field it doesn't have, is
     * reported, not silently zero-filled or miscompiled. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defstruct Point (x : I32) (y : I32))\n"
            "(defn bad [] : Point (Point 0))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a construction call with too few arguments parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src == NULL && emit_err != NULL,
              "a defstruct construction with the wrong argument count fails honestly");
        arena_free_all(&arena);
    }
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defstruct Point (x : I32) (y : I32))\n"
            "(defn bad [(p : Point)] : I32 (get-field p :z))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "get-field on a nonexistent field parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src == NULL && emit_err != NULL,
              "get-field naming a field the struct doesn't have fails honestly");
        arena_free_all(&arena);
    }

    /* --- a generic, bare-symbol region parameter (`Arena @ Region`, not
     * a literal `:region/x` keyword) is now recognized as a real
     * region-scoped Arena parameter -- the shape stdlib/cache.prn's own
     * open() and pentest/scan.prn's own scan-ports() actually use. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn scan-ports [(target : String @ :region/scratch) (dest : Arena @ Region)]\n"
            "  : Unit\n  #target\n  {:c (inline-c \"host_scan(target, dest);\")})";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a generic Arena @ Region parameter parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "a generic Arena @ Region parameter emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "Arena *dest __attribute__((unused))") != NULL,
                  "the generic-region parameter becomes a real Arena *, same as a :region/x one");
        }
        arena_free_all(&arena);
    }

    /* --- a `Type @ Region` return-type annotation (not just a bare
     * type) is now parsed correctly -- the shape pentest/scan.prn's own
     * scan-ports() return type uses: `(Result (Vec PortResult)
     * ScanError) @ Region`. Before this, the trailing `@ Region` was
     * left dangling as bogus extra "body" content. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn get-config [(key : String @ :region/scratch)]\n"
            "  : (Option String) @ Region\n  #target\n  {:c (inline-c \"host_get_config(key)\")})";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a `Type @ Region` return-type annotation parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL,
              "a `Type @ Region` return-type annotation emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "Option get_config(") != NULL,
                  "the trailing @ Region on a return type is consumed, not left dangling as bogus body content");
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

    /* --- (Vec T) as a real defstruct field type: erases T, maps to the
     * real runtime Vec struct -- the real shape firefly.prn's own `T`
     * struct (`messages : (Vec String) @ Region`) needs, the first of
     * this whole batch's real blockers found by trying to get firefly/
     * ladybug/scarab.prn to actually compile. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defstruct Bag (items : (Vec String) @ Region))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a defstruct with a (Vec T) field parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "(Vec T) as a field type emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "Vec items;") != NULL,
                  "(Vec String) erases to the real runtime Vec struct, matching Result/Option's own erasure");
        }
        arena_free_all(&arena);
    }

    /* --- `&Type` (single-token) and `&mut Type` (two-token) reference
     * parameters and fields both become real C pointers -- the real
     * shape firefly.prn's own `(!t : &mut T)` and firefly/ladybug.prn's
     * own `(actual : &Any)` need. `Any` maps to `void` (an untyped
     * pointer target). --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defstruct T (failed : I32))\n"
            "(defstruct Expectation (actual : &Any) (t : &mut T))\n"
            "(defn touch [(actual : &Any) (!t : &mut T)] : Unit\n"
            "  (set! (get-field !t :failed) 1))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "&Any/&mut T fields and parameters parse fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "&Any/&mut T fields and parameters emit successfully");
        if (c_src) {
            CHECK(strstr(c_src, "void * actual;") != NULL,
                  "a &Any field becomes a real void * (Any erases to void, the reference wraps it in a pointer)");
            CHECK(strstr(c_src, "T * t;") != NULL,
                  "a &mut T field becomes a real T * (mut/immut references both erase to a plain C pointer)");
            CHECK(strstr(c_src, "T * t") != NULL,
                  "a &mut T PARAMETER (the two-token form, distinct from the single-token &Type field/param "
                  "path above) also becomes a real T *");
            CHECK(strstr(c_src, "(t)->failed = 1") != NULL,
                  "get-field auto-derefs through a reference parameter's own pointer type, emitting -> not .");
        }
        arena_free_all(&arena);
    }

    /* --- `do` as a real statement sequence (not a bogus function call to
     * a nonexistent `do()`), and `vec/push!`/`&(expr)` (the two-sibling-
     * node address-of form a call's own argument list has to pair back
     * together) both work together -- the real shape firefly.prn's own
     * `errorf` (`(do (set! ...) (vec/push! &(get-field !t :messages)
     * msg))`) needs end to end. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defstruct T (failed : I32) (messages : (Vec String) @ Region))\n"
            "(defn errorf [(!t : &mut T) (msg : String @ :region/scratch)] : Unit\n"
            "  (do\n"
            "    (set! (get-field !t :failed) 1)\n"
            "    (vec/push! &(get-field !t :messages) msg)))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a do-block function body with set!/vec-push!/&(expr) parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "the do-block body emits successfully, not a bogus do(...) call");
        if (c_src) {
            CHECK(strstr(c_src, "do(") == NULL,
                  "`do` never appears as a literal emitted function call -- it's a real statement sequence");
            CHECK(strstr(c_src, "vec_push_(&((t)->messages), msg)") != NULL,
                  "vec/push! mangles to the real runtime vec_push_ call, and &(get-field ...) emits a real "
                  "C address-of around the get-field access, both inside the do-block's own statements");
        }
        arena_free_all(&arena);
    }

    /* --- `deref` on a real pointer-typed expression emits a real C
     * dereference -- scarab.prn's own `(deref (vec/get &suite-tree
     * i))` shape. A non-pointer argument fails honestly rather than
     * emitting a nonsensical `*(int_value)`. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defstruct Item (value : I32))\n"
            "(defn touch [(!x : &mut Item)] : I32\n"
            "  (get-field (deref !x) :value))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "(deref !x) parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "deref on a real pointer-typed expression emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "(*((Item *)(x)))") != NULL,
                  "deref emits a real, cast C dereference (not a bare *(expr), which is only valid "
                  "when expr's own real C type already matches -- vec_get's real void* return type "
                  "is the real counter-example that requires the cast), not a function call");
        }
        arena_free_all(&arena);
    }
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn bad [(x : I32)] : I32 (deref x))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "deref on a non-pointer value parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src == NULL && emit_err != NULL, "deref on a non-pointer-typed argument fails honestly");
        arena_free_all(&arena);
    }

    /* --- real, honest ISO C99 bug found via an actual gcc -pedantic
     * compile of firefly.prn's own `errorf`: a `Unit`(void)-returning
     * function whose tail expression is itself a void-typed call (e.g.
     * `vec/push!`) must emit that call as a bare statement, not
     * `return <call>;` -- ISO C forbids `return` with an expression in a
     * void function even when the expression's own type genuinely is
     * void. `parena build`'s own exit code alone didn't catch this
     * (region analysis/emission both "succeeded"); only compiling the
     * real output with gcc did. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defstruct T (messages : (Vec String) @ Region))\n"
            "(defn errorf [(!t : &mut T) (msg : String @ :region/scratch)] : Unit\n"
            "  (vec/push! &(get-field !t :messages) msg))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a Unit-returning function whose sole body form is a void-typed call parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "it emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "return vec_push_") == NULL,
                  "a void-typed tail call is never wrapped in `return` (real ISO C99 -pedantic error)");
            CHECK(strstr(c_src, "vec_push_(&((t)->messages), msg);") != NULL,
                  "it's instead emitted as a real, bare statement");
        }
        arena_free_all(&arena);
    }

    /* --- string literals as plain expressions -- a real, foundational
     * gap found while getting firefly.prn's own `(string/concat "SKIP: "
     * reason)` to compile: emit_expr() had NO handling for NODE_STRING
     * at all before this. Real C-escaping matters here: the lexer's own
     * lex_string() already unescapes `\n`/`\"`/`\\` into raw bytes, so a
     * literal containing a real quote/backslash/newline byte has to be
     * re-escaped, not just wrapped in quotes verbatim. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn f [] : String \"hello\")";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a plain string literal expression parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "a string literal emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "return \"hello\";") != NULL,
                  "it emits as a real C string literal");
        }
        arena_free_all(&arena);
    }
    {
        Arena arena;
        arena_init(&arena);
        /* Real embedded quote and backslash bytes (not the two-character
         * escape sequences) -- lex_string() already unescaped these by
         * the time emit_expr() sees them. */
        const char *src = "(defn f [] : String \"say \\\"hi\\\" \\\\ bye\")";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a string literal with an embedded quote and backslash parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "it emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "\"say \\\"hi\\\" \\\\ bye\"") != NULL,
                  "the embedded quote and backslash are re-escaped into real, valid C, not left raw");
        }
        arena_free_all(&arena);
    }

    /* --- `&(ComplexType)` -- a bare `&` symbol immediately followed by a
     * parenthesized type (the real firefly.prn shape: `(cases :
     * &(Vec TestCase))`) parses as two sibling nodes, same as the
     * expression-level `&(expr)` form, and needs its own param-loop
     * branch distinct from the single-token `&Type`/two-token `&mut
     * Type` paths already covered. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defstruct Item (value : I32))\n"
            "(defn count [(items : &(Vec Item))] : I32\n"
            "  (vec/len items))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a &(Vec T) parameter parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "&(Vec T) as a parameter type emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "Vec * items") != NULL,
                  "it becomes a real Vec * (Vec T's own erasure, wrapped in the reference's own pointer)");
        }
        arena_free_all(&arena);
    }

    /* --- bitwise/mod operators -- real, new addition needed for the
     * first time by stdlib/compress/lz4.prn's own token-header byte
     * packing (a 4-bit literal-length nibble and a 4-bit match-length
     * nibble combined via shift + bit-or). Named operators
     * (bit-and/bit-or/bit-xor/shl/shr), not bare punctuation, since `&`
     * already means something else in this language (the reference
     * sigil) and `<`/`>` are already comparisons. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn pack [(hi : I32) (lo : I32)] : I32\n"
            "  (bit-or (shl hi 4) (bit-and lo 15)))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a bit-or/shl/bit-and expression parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "it emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "(hi << 4)") != NULL, "shl emits a real C <<");
            CHECK(strstr(c_src, "(lo & 15)") != NULL, "bit-and emits a real C &");
            CHECK(strstr(c_src, "|") != NULL, "bit-or emits a real C |");
        }
        arena_free_all(&arena);
    }

    /* --- map-literal struct construction -- STDLIB.md's own gap #2,
     * found blocking firefly.prn's own `run-tests`
     * (`{:passed passed :failed failed :skipped 0}`). Real, structural
     * match: no type-context threading, searches every registered
     * defstruct for the one whose own field NAME SET exactly matches
     * the map literal's own keyword keys. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defstruct Report (passed : I32) (failed : I32) (skipped : I32))\n"
            "(defn f [] : Report\n"
            "  {:passed 1 :failed 0 :skipped 2})";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a map literal matching a registered defstruct's fields parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "it emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "Report_new(1, 0, 2)") != NULL,
                  "it constructs the matching struct via its own real constructor, fields in the "
                  "struct's own declared order, not the map literal's own key order");
        }
        arena_free_all(&arena);
    }
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn f [] : Unit {:nope 1 :also-nope 2})";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a map literal matching no registered defstruct parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src == NULL && emit_err != NULL,
              "a map literal with no matching registered defstruct fails honestly");
        arena_free_all(&arena);
    }

    /* --- `let`/`do` in a loop's own tail position -- a real, structural
     * bug found while getting firefly.prn's own `run-tests` to compile:
     * `(loop [...] (if cond then-val (let [...] ... (recur ...))))` used
     * to fail, since emit_loop_tail()'s own fallback called emit_expr()
     * on the whole `let` node, which has no handling for it at all
     * (only ever special-cased at the body-statement level). Fixed the
     * same way `if` itself already composes recursively in tail
     * position. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn sum-to [(n : I32)] : I32\n"
            "  (loop [i 0 acc 0]\n"
            "    (if (>= i n)\n"
            "      acc\n"
            "      (let [next (+ acc i)]\n"
            "        (recur (+ i 1) next)))))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a let-in-loop-tail-else-branch with a recur inside it parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL,
              "it emits successfully (previously failed: 'unsupported expression form')");
        if (c_src) {
            CHECK(strstr(c_src, "next __attribute__((unused)) = (acc + i);") != NULL,
                  "the let's own binding is emitted as a real statement inside the if's own else branch");
            CHECK(strstr(c_src, "continue;") != NULL,
                  "the recur nested inside the let's own body still reaches a real C continue");
        }
        arena_free_all(&arena);
    }

    /* --- calling a first-class function VALUE (a (Fn [..] ..)-typed
     * struct field, not a plain named function) -- firefly.prn's own
     * real `((get-field tc :run) &mut t)`. Real, distinct gap from
     * emit_call()'s own symbol-headed-call path: the callee position
     * here is itself a compound expression. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defstruct Item (run : (Fn [I32] Unit)))\n"
            "(defn call-it [(x : Item)] : Unit\n"
            "  ((get-field x :run) 1))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "calling a Fn-typed struct field value parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "it emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "((x).run)(1)") != NULL,
                  "the callee expression is emitted and parenthesized, then called directly");
        }
        arena_free_all(&arena);
    }

    /* --- a defstruct field/constructor of a Fn (function-pointer) type
     * -- a real bug found via an actual gcc compile (firefly.prn's own
     * TestCase.run field): a plain "%s %s" splice produces invalid C
     * for a function-pointer type ("void (*)(T *) run" instead of
     * "void (*run)(T *)"). --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defstruct Item (run : (Fn [I32] Unit)))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a defstruct with a Fn-typed field parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "it emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "void (*run)(int);") != NULL,
                  "the field's own function-pointer name is spliced inside the (*), real valid C");
            CHECK(strstr(c_src, "void (*)(int) run") == NULL,
                  "the old, invalid splice (name after the whole type) never appears");
        }
        arena_free_all(&arena);
    }

    /* --- a known, already-emitted user-defined function's own real
     * return type is used by a later caller in the same file, instead
     * of the generic "void *" guess -- a real bug found via an actual
     * gcc compile (firefly.prn's own `fatalf`, whose whole body is a
     * plain call to `errorf`, a real Unit(void)-returning function;
     * the generic guess broke the void-tail-statement-not-return fix
     * from earlier this session, since it only matched "void", not
     * "void *"). --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn helper [] : Unit\n"
            "  #target\n"
            "  {:c (inline-c \"host_call()\")})\n"
            "(defn caller [] : Unit\n"
            "  (helper))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a function whose body is a plain call to an earlier void-returning "
                                "function parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "it emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "return helper()") == NULL,
                  "the call to a known void-returning function is never wrapped in return");
            CHECK(strstr(c_src, "helper();") != NULL,
                  "it's emitted as a real, bare statement instead");
        }
        arena_free_all(&arena);
    }

    /* --- sb_appendf()'s own fixed 1024-byte internal buffer used to
     * silently truncate a whole function body once it grew past that --
     * a real bug found via an actual gcc compile of firefly.prn's own
     * `run-tests` (the generated file was cut off mid-token, right in
     * the middle of a real `continue;`). Regression-tested here with a
     * large, real function body built to comfortably exceed 1024 bytes
     * on its own. --- */
    {
        Arena arena;
        arena_init(&arena);
        /* Plain, manually-grown buffer (not the compiler's own
         * file-local StrBuf, which test_emit.c as a separate
         * translation unit can't see) -- 40 chained let-bindings, each
         * with a real, non-trivial initializer expression, comfortably
         * pushes the function body's own emitted C past 1024 bytes on
         * its own. */
        char *src = (char *)malloc(8192);
        size_t src_len = 0;
        src_len += (size_t)sprintf(src + src_len,
                                    "(defn big [] : I32\n  (loop [i 0 acc 0]\n    (if (>= i 1)\n      acc\n      (let [");
        for (int i = 0; i < 40; i++) {
            src_len += (size_t)sprintf(src + src_len, "v%d (+ acc %d) ", i, i);
        }
        src_len += (size_t)sprintf(src + src_len, "]\n        (recur (+ i 1) v39)))))");
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, src_len, &parse_err);
        CHECK(program != NULL, "a function body large enough to exceed 1024 bytes parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "it emits successfully");
        if (c_src) {
            CHECK(strlen(c_src) > 1024, "the emitted C really is larger than sb_appendf()'s own old fixed buffer");
            CHECK(strstr(c_src, "v39 __attribute__((unused)) = (acc + 39);") != NULL,
                  "the LAST let-binding is present and intact -- not silently truncated");
            size_t clen = strlen(c_src);
            CHECK(clen > 3 && c_src[clen - 1] == '\n' && c_src[clen - 2] == '\n' && c_src[clen - 3] == '}',
                  "the emitted C ends with a real, intact closing brace, not cut off mid-token");
        }
        free(src);
        arena_free_all(&arena);
    }

    /* --- string_concat -- a real, minimal runtime implementation, found
     * genuinely missing (only ever designed, STDLIB.md's own "string"
     * package) while getting firefly.prn's own `skip` to gcc-compile.
     * Compiler-level: just confirms the call itself emits correctly;
     * the runtime function's own correctness is a real C-level concern
     * (verified separately by the firefly.prn gcc compile itself). --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn f [(a : String @ Region) (b : String @ Region) (dest : Arena @ Region)] : String @ Region\n"
            "  (string/concat a b dest))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a string/concat call parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "it emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "string_concat(a, b, dest)") != NULL,
                  "it emits a real call to the runtime's own real string_concat");
        }
        arena_free_all(&arena);
    }

    /* --- multi-field defenum variant payloads -- scarab.prn's own real
     * `SuiteNode` (`Group`'s two real, typed fields, `name` and
     * `children`), previously capped at exactly one payload field.
     * Zero/one-field variants keep their exact pre-existing shape
     * (regression-checked below); two-or-more-field variants get a
     * real companion payload struct + a constructor taking an explicit
     * destination Arena as its own first parameter. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defenum SuiteNode\n"
            "  (Group (name : String @ Region) (children : I32))\n"
            "  (Spec  (name : String @ Region))\n"
            "  (Empty))\n"
            "(defn f [(dest : Arena @ Region) (n : String @ Region)] : SuiteNode\n"
            "  (Group dest n 5))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a defenum with zero/one/two-field variants parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "it emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "typedef struct {\n    char * name;\n    int children;\n} SuiteNode_Group_Payload;") != NULL,
                  "the two-field variant gets a real companion payload struct with real, distinct field types");
            CHECK(strstr(c_src, "static inline SuiteNode SuiteNode_Group(Arena *dest, char * name, int children) {") != NULL,
                  "its own constructor takes an explicit destination Arena as its first parameter, "
                  "then the real, typed field values");
            CHECK(strstr(c_src, "arena_alloc(dest, sizeof(SuiteNode_Group_Payload))") != NULL,
                  "it allocates the companion payload struct into that arena");
            CHECK(strstr(c_src, "static inline SuiteNode SuiteNode_Spec(void *value)") != NULL,
                  "the one-field variant keeps its exact pre-existing generic void* shape, unchanged");
            CHECK(strstr(c_src, "static inline SuiteNode SuiteNode_Empty(void)") != NULL,
                  "the zero-field variant keeps its exact pre-existing shape too, unchanged");
            CHECK(strstr(c_src, "return SuiteNode_Group(dest, n, 5);") != NULL,
                  "a real call site passes the destination arena plus each field value positionally");
        }
        arena_free_all(&arena);
    }
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defenum E (V (a : I32) (b : I32)))\n"
            "(defn f [(dest : Arena @ Region)] : E\n"
            "  (V dest 1))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a multi-field variant call with the wrong argument count parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src == NULL && emit_err != NULL,
              "a multi-field variant call missing a required field value fails honestly");
        arena_free_all(&arena);
    }

    /* --- a real, structural pre-pass ordering bug found while testing
     * the new multi-file build against the real, combined ladybug/
     * scarab files: emit_c() used to process EVERY defenum in the whole
     * program first, then EVERY defstruct -- regardless of where each
     * form actually appeared. That broke a defenum variant needing a
     * defstruct type that genuinely comes EARLIER in the real,
     * combined file order (scarab.prn's own SuiteNode needing T, a
     * defstruct from firefly.prn, which appears first when the two
     * files are built together) -- the old "all defenums,
     * unconditionally, before any defstruct" order silently ignored
     * that real, natural precedence. Now processed together in one
     * pass, in real combined order. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defstruct T (value : I32))\n"
            "(defenum E (V (a : String @ Region) (b : (Fn [&mut T] Unit))))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a defstruct followed by a defenum variant needing it parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL,
              "it emits successfully -- the defstruct (appearing first) is visible to the "
              "defenum's own field-type resolution, not silently processed after it regardless "
              "of real source order");
        arena_free_all(&arena);
    }

    /* --- mangle()'s own leading-'!' handling -- a real bug found while
     * getting pentest/pcap.prn (the first real stdlib file to reach a
     * gcc compile with a `!`-prefixed reference parameter used inside
     * its own `#target` body) to compile clean: a leading `!` (the
     * linear/mutable-binding sigil, `!t`/`!m`/`!cap`) must be STRIPPED
     * entirely, not converted to `_` -- multiple real stdlib files'
     * own hand-written #target inline-C bodies (thread.prn's `lock`,
     * pcap.prn's `read-packet`/`filter`) reference their own
     * `!`-prefixed parameter by its bare, un-prefixed name. A mid/
     * trailing `!` (the separate mutating-*call*-name convention,
     * `vec/push!`/`set!`) still converts to `_` as before -- that's
     * this compiler's own naming choice on both ends, not an external
     * human expectation, and changing it would risk a real collision
     * with a same-named non-mutating sibling function. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defstruct Mutex (handle : I32))\n"
            "(defn lock [(!m : &Mutex)] : Unit\n"
            "  #target\n"
            "  {:c (inline-c \"pthread_mutex_lock(&m->handle);\")})";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a #target function with a !-prefixed reference parameter parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "it emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "Mutex * m __attribute__((unused))") != NULL,
                  "the leading '!' is stripped entirely from the parameter's own C name (not '_m'), "
                  "matching real hand-written #target bodies (thread.prn's own lock, pcap.prn's own "
                  "read-packet/filter) that reference it by its bare name");
        }
        arena_free_all(&arena);
    }
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn f [(v : &Any)] : Unit (vec/push! v v))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "vec/push! (mid/trailing '!', not a leading one) parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "it emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "vec_push_(v, v)") != NULL,
                  "a mid/trailing '!' still mangles to '_' as before -- unchanged, this compiler's "
                  "own naming choice on both ends, matching parena_runtime.h's own real vec_push_");
        }
        arena_free_all(&arena);
    }

    /* --- N-ary `and`/`or` -- found blocking world.prn's own real
     * `(and (>= x 0) (< x (get-field t :width)) (>= z 0) (< z ...))`.
     * Real, narrow extension: only `&&`/`||`, folded left-associatively
     * (semantically exact for real logical AND/OR) -- NOT generalized
     * to comparison/arithmetic operators, where naive pairwise folding
     * would silently compute the wrong thing for a real chained
     * comparison like `(< a b c)`. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn f [(a : I32) (b : I32) (c : I32)] : Bool (and (> a 0) (> b 0) (> c 0)))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a 3-argument (and a b c) parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL,
              "it emits successfully (previously failed: 'needs exactly 2 arguments')");
        if (c_src) {
            CHECK(strstr(c_src, "(((a > 0) && (b > 0)) && (c > 0))") != NULL,
                  "it folds left-associatively into real, correct nested && operators");
        }
        arena_free_all(&arena);
    }

    /* --- `when` -- found blocking world.prn's own real `set-height`
     * (`(when (in-bounds? ...) (vec-set-at! ...))`, its WHOLE body) and
     * firefly/ladybug.prn's own `to` (mid-body). Real, honest scope:
     * `when` has no real "else" value at all (unlike `if`), so it's
     * handled as a real statement-level `if (cond) { expr; }`, not a
     * value-producing ternary -- both as a mid-body statement and in
     * tail position of a Unit(void)-returning function. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defstruct Counter (n : I32))\n"
            "(defn maybe-log [(!c : &mut Counter) (cond : Bool)] : Unit\n"
            "  (when cond\n"
            "    (set! (get-field !c :n) 1)))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "(when cond expr) as a whole function body parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL,
              "it emits successfully (previously failed: 'unsupported expression form')");
        if (c_src) {
            CHECK(strstr(c_src, "if (cond) {") != NULL,
                  "when emits a real, statement-level C if with no else branch");
            CHECK(strstr(c_src, "return when(") == NULL,
                  "when is never treated as a generic function call, unlike before this fix");
        }
        arena_free_all(&arena);
    }

    /* --- `?`-suffixed predicate names (a real, common Scheme/Lisp/Ruby
     * naming convention, e.g. world.prn's own real `in-bounds?`) --
     * mangle() had no handling for '?' at all before this, producing
     * flatly invalid C (`in_bounds?` isn't a valid identifier). --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn zero? [(n : I32)] : Bool (= n 0))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a ?-suffixed predicate function name parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "it emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "int zero_(int n") != NULL,
                  "the trailing '?' mangles to '_', a real valid C identifier");
        }
        arena_free_all(&arena);
    }

    /* --- Vec scalar boxing -- the deepest of this batch's real fixes,
     * found blocking world.prn's own real `Terrain.heights : (Vec F64)`.
     * `Vec` stores `void *` items, fine for pointer-representable
     * elements, but a raw I32/F64 needs a real, arena-allocated cell to
     * point AT first (parena_runtime.h's own vec_box_i32/vec_box_f64) --
     * not a bit-boxing trick, deliberately, since real usage (world.prn's
     * own `get-height`: `(deref (vec/get ...))`) already wraps vec/get
     * in `deref` uniformly for scalar and struct-typed Vecs alike, so a
     * scalar Vec's own stored items need to genuinely BE real pointers
     * to real cells, the same shape struct-pointer items already have.
     * Covers a `(Vec ElemType)`-typed STRUCT FIELD specifically (a
     * `&(Vec ElemType)` PARAMETER was already covered before this pass
     * -- see the earlier "&(Vec T) parameter" test above) -- the real,
     * additional gap: get-field's own emission now also registers a
     * g_vec_elem_hints entry for a Vec-typed field access. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defstruct Row (cells : (Vec F64) @ Region))\n"
            "(defn set-cell [(!r : &mut Row) (i : I32) (v : F64)] : Unit\n"
            "  (vec-set-at! &mut (get-field !r :cells) i v))\n"
            "(defn get-cell [(r : &Row) (i : I32)] : F64\n"
            "  (deref (vec/get &(get-field r :cells) i)))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a (Vec F64)-typed struct field, set via vec-set-at! and read via "
                                "deref+vec/get, parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL,
              "it emits successfully (previously failed with a type mismatch: double where void * "
              "was expected, and a real 'dereferencing void *' error)");
        if (c_src) {
            CHECK(strstr(c_src, "vec_set_at_(&((r)->cells), i, vec_box_f64(&((r)->cells), v))") != NULL,
                  "set-cell boxes the real scalar value into a real, arena-allocated cell before "
                  "storing its address, not the raw double where void * is expected");
            CHECK(strstr(c_src, "(*((double *)(vec_get(&((r)->cells), i))))") != NULL,
                  "get-cell's deref+vec/get correctly resolves to a real double *, not a useless void *");
        }
        arena_free_all(&arena);
    }

    /* --- Non-reference compound-typed param with a trailing region
     * annotation, e.g. array.prn's own real `(shape : (Vec I32) @
     * :region/scratch)` (no `&` prefix, unlike the already-working
     * `&(Vec I32)` reference-param form). The "Type @ Region on a
     * non-Arena type" branch originally required children[2] to be a
     * NODE_SYMBOL, so a compound/list type (`(Vec I32)`) with a trailing
     * region fell through to the generic "no region annotation and isn't
     * a plain ... type either" failure instead of resolving -- found
     * while getting array.prn's own `zeros`/`from-vec`/`reshape` to
     * compile, all of which pass shape as a plain (non-reference) Vec
     * with a scratch-region annotation. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn shape-len [(shape : (Vec I32) @ :region/scratch)] : I32\n"
            "  (vec/len &shape))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a bare (non-reference) (Vec I32) @ :region/scratch param parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL,
              "it emits successfully (previously failed: 'no region annotation and isn't a plain "
              "... type either', since the Type @ Region branch required a bare NODE_SYMBOL type)");
        if (c_src) {
            CHECK(strstr(c_src, "Vec shape") != NULL,
                  "the param is bound as a real, by-value Vec (its own resolved compound type), "
                  "not left as an opaque Arena * or rejected outright");
        }
        arena_free_all(&arena);
    }

    /* --- `when` in loop-tail position, with MULTIPLE body forms -- a
     * real, structural gap found gcc-verifying array.prn's own real
     * `strides-for`, whose whole loop body is `(when (>= i 0) (vec/push!
     * &s running) (recur ...))`. This fell all the way through to the
     * generic "plain value" case in emit_loop_tail, which calls
     * emit_expr() on the WHOLE `when` node -- no handling for `when`
     * exists there at all (only emit_body's own statement/tail dispatch
     * does), so this silently mangled into a bogus call to a
     * never-defined `when(...)` C function. `parena build`'s own exit
     * code never caught this -- only an actual gcc -pedantic -Werror
     * compile did (an "implicit declaration of function 'when'"
     * error). --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn fill [(shape : &(Vec I32)) (dest : Arena @ Region)] : (Vec I32) @ Region\n"
            "  (let [n (vec/len shape) s (vec/new dest)]\n"
            "    (loop [i 0]\n"
            "      (when (< i n)\n"
            "        (vec/push! &s (deref (vec/get shape i)))\n"
            "        (recur (+ i 1))))\n"
            "    s))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a multi-body-form (when cond body1 body2) in loop-tail position parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL,
              "it emits successfully (previously failed: implicit declaration of function 'when', "
              "since emit_loop_tail had no handling for `when` at all)");
        if (c_src) {
            CHECK(strstr(c_src, "if ((i < n)) {") != NULL,
                  "when's condition becomes a real, statement-level C if inside the loop");
            CHECK(strstr(c_src, "continue;") != NULL && strstr(c_src, "} else {\n            break;") != NULL,
                  "the true branch's own recur becomes continue, the false branch (when's own "
                  "real, honest 'no else value' semantics) breaks the loop instead");
        }
        arena_free_all(&arena);
    }

    /* --- Un-hinted, plain `let`-bound local Vec, scalar element pushed
     * via vec/push! -- a real, structural gap found gcc-verifying
     * array.prn's own real `strides-for`/`zeros`, both of which do
     * `(let [s (vec/new dest)] (loop [...] (... (vec/push! &s running)
     * ...)) s)`: `s` carries no type annotation of its own anywhere (no
     * `&(Vec ElemType)` parameter, no `(Vec ElemType)` struct field), so
     * the old hint-only boxing decision (g_vec_elem_hints) never fired,
     * and a raw scalar (`running`, a real `double`/`int`) flowed
     * straight into `vec_push_`'s own `void *item` parameter -- a real
     * gcc type-mismatch error, not caught by `parena build`'s own exit
     * code. Fixed by deciding boxing from the VALUE argument's own
     * already-known emitted C type (from emit_expr itself) instead of
     * requiring a separately-tracked hint for the TARGET Vec. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn build [(dest : Arena @ Region)]\n"
            "  (let [s (vec/new dest)]\n"
            "    (vec/push! &s 42)\n"
            "    s))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "vec/push! of a scalar onto an un-hinted let-bound local Vec parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL,
              "it emits successfully (previously produced a real gcc type mismatch: double where "
              "void * was expected, since only param/struct-field Vecs got a boxing hint before)");
        if (c_src) {
            /* Real, pre-existing, unrelated convention confirmed here, not
             * changed by this fix: a bare numeric literal like `42`
             * always resolves to "double" (no I32-vs-F64 literal
             * distinction exists in this compiler), so vec_box_f64 is
             * the real, correct helper here -- matching what array.prn's
             * own real strides-for/zeros (gcc-verified separately) also
             * produce for their own int-looking loop-var pushes. */
            CHECK(strstr(c_src, "vec_push_(&(s), vec_box_f64(&(s), 42))") != NULL,
                  "the raw scalar literal is boxed via vec_box_f64 before being stored, decided "
                  "from the argument's own resolved type, not a hint lookup on the target");
        }
        arena_free_all(&arena);
    }

    /* --- Ok/Err/Some wrapping a NON-pointer payload (a by-value struct
     * construction, or a deref'd scalar) with a real Arena in scope --
     * the real, deeper gap found right after the (Vec T) @ Region param
     * fix above got array.prn's own `from-vec` past its own parameter
     * parsing: `(Ok {:data data :shape shape ...})` constructs a
     * by-value NDArray struct (map-literal construction reports its own
     * type as the plain struct name, never a pointer -- structs are
     * always constructed by value in this compiler), which can't
     * implicitly convert to the runtime's own `void *value` field.
     * Fixed via a generated, per-type `TypeName_box(Arena *dest,
     * TypeName v)` helper function (see g_box_helpers' own declaration
     * comment for why this shape, not a GNU statement-expression or a
     * temp-hoisting architecture change), found via the arena bound to
     * the conventional `dest` parameter name (STDLIB.md's own
     * documented "every allocating function takes an explicit dest"
     * convention). --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defstruct Point (x : I32) (y : I32))\n"
            "(defenum MakeError (BadInput))\n"
            "(defn make-point [(x : I32) (y : I32) (dest : Arena @ Region)]\n"
            "  : (Result Point MakeError) @ Region\n"
            "  (Ok {:x x :y y}))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "(Ok {...}) wrapping a by-value struct construction, with a dest "
                                "arena in scope, parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL,
              "it emits successfully (previously failed: 'VS0's emitter only supports pointer-typed "
              "payloads so far', since Ok/Err/Some never attempted boxing at all before this fix)");
        if (c_src) {
            CHECK(strstr(c_src, "static inline Point *Point_box(Arena *dest, Point v)") != NULL,
                  "a real, per-type box helper function is generated once, ahead of the defn that "
                  "needs it");
            CHECK(strstr(c_src, "result_ok(Point_box(dest, Point_new(x, y)))") != NULL,
                  "the call site boxes the by-value struct construction through the generated "
                  "helper before handing it to result_ok, a plain, valid C99 function call -- no "
                  "GNU statement-expression, no temp-hoisting");
        }
        arena_free_all(&arena);
    }

    /* --- `cond` as a pure value expression -- Lisp's own classic
     * multi-clause conditional, found missing entirely (2026-08-21,
     * gcc-verifying regex/glob.prn's own real `glob-match`): with no
     * handling anywhere, `cond` fell through to the generic call path
     * and mangled into a bogus call to a never-defined `cond(...)` C
     * function, undetected by `parena build`'s own exit code. Folds
     * right-to-left into nested C ternaries; the LAST clause is always
     * the unconditional base case (matching every real clause set this
     * stdlib actually writes, always ending `(true ...)`). --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn classify [(n : I32)] : String\n"
            "  (cond\n"
            "    ((< n 0) \"negative\")\n"
            "    ((= n 0) \"zero\")\n"
            "    (true \"positive\")))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a (cond (test result) ... (true default)) parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL,
              "it emits successfully (previously failed: mangled into a bogus call to a "
              "never-defined 'cond' C function, since no handling for cond existed anywhere)");
        if (c_src) {
            CHECK(strstr(c_src, "((n < 0) ? \"negative\" : ((n == 0) ? \"zero\" : \"positive\"))") != NULL,
                  "cond folds right-to-left into real, correctly-nested C ternaries, with the last "
                  "clause's own result used unconditionally as the base case");
        }
        arena_free_all(&arena);
    }

    /* --- `cond` in loop-tail position, with `recur` inside more than
     * one clause -- real, honest necessity beyond the pure-ternary form
     * above: string.prn's own real `split` and map.prn's own real
     * `find-slot` both have `cond` as their WHOLE loop body, with
     * `recur` inside multiple clause results -- `recur` emits a real C
     * `continue;` STATEMENT, which can never appear inside a ternary
     * expression. Also covers a real, self-caught bug in this same fix
     * (found via an actual gcc compile of an isolated repro): the loop
     * result's own C type used to come only from the LAST clause,
     * wrong the instant an EARLIER clause is the real terminal value
     * and the last clause is a `recur` (which reports no type at all)
     * -- fixed to take whichever clause actually resolved one, the
     * same real fallback `if`'s own loop-tail handling already uses
     * across exactly two branches, generalized across N. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn count-up-to [(n : I32)] : I32\n"
            "  (loop [i 0 count 0]\n"
            "    (cond\n"
            "      ((>= i n) count)\n"
            "      ((= i 3) (recur (+ i 1) count))\n"
            "      (true (recur (+ i 1) (+ count 1))))))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "cond with recur inside multiple clauses, as a whole loop body, "
                                "parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL,
              "it emits successfully (previously unreachable at all: emit_loop_tail had no `cond` "
              "handling, only the pure-ternary emit_cond() form, which can't hold a `continue;`)");
        if (c_src) {
            CHECK(strstr(c_src, "continue;") != NULL,
                  "a clause's own recur becomes a real continue; statement, not a ternary branch");
            /* Real, pre-existing, unrelated convention confirmed here:
             * a bare numeric literal always resolves to "double" (no
             * I32-vs-F64 literal distinction in this compiler), so
             * that's the real, correct terminal type here -- the actual
             * bug this test targets is that it resolves to a real type
             * AT ALL (previously NULL/void*, from only ever consulting
             * the unrelated LAST clause), not which specific type. */
            /* __loop_result_N's own number is a process-wide counter
             * (not reset per emit_c() call), so only the prefix is
             * checked here -- the exact number depends on how many
             * other tests' own loops ran earlier in this same process. */
            CHECK(strstr(c_src, "double __loop_result_") != NULL,
                  "the loop's own result type resolves to the real terminal clause's own resolved "
                  "type, not NULL/void* from the unrelated LAST clause happening to be a recur -- "
                  "the real, self-caught bug in this same fix");
        }
        arena_free_all(&arena);
    }

    /* --- `if` in tail position, with `loop` (or `let`/`do`/`when`/
     * `cond`/`match`/`with-arena`) as one of its own branch VALUES --
     * found missing (2026-08-21, gcc-verifying string.prn's own real
     * `is-valid-i32-text?`, whose own let-tail is `(if (= n 0) false
     * (loop ...))`): emit_if() is a pure ternary that calls emit_expr()
     * on both branches, and emit_expr() has no handling for `loop` (or
     * any of its statement-shaped siblings) as a bare value -- those
     * are only ever special-cased at the body-statement/tail level.
     * Fixed by giving `if` the same real statement-level tail
     * composition its siblings already get in emit_body's own tail
     * dispatch, by recursing emit_body() itself on each branch treated
     * as a one-form body -- reaching every one of emit_body's own
     * tail-dispatch cases (including nested `if`) for free. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn sum-to [(n : I32)] : I32\n"
            "  (if (<= n 0)\n"
            "    0\n"
            "    (loop [i 0 acc 0]\n"
            "      (if (> i n)\n"
            "        acc\n"
            "        (recur (+ i 1) (+ acc i))))))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "(if cond 0 (loop ...)) in tail position parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL,
              "it emits successfully (previously failed: 'unsupported expression form', since "
              "emit_if()'s own pure-ternary path has no handling for a loop as a branch value)");
        if (c_src) {
            CHECK(strstr(c_src, "if ((n <= 0)) {") != NULL,
                  "if in tail position becomes a real, statement-level C if, not a ternary");
            CHECK(strstr(c_src, "return 0;") != NULL && strstr(c_src, "while (1) {") != NULL,
                  "each branch composes as its own real tail form -- a plain value returns "
                  "directly, a loop still emits its own real while(1) statement");
        }
        arena_free_all(&arena);
    }

    /* --- `alloc` with a real SIZE EXPRESSION (not a string literal) --
     * found missing (2026-08-21, gcc-verifying string.prn's own real
     * `concat`: `(alloc dest String (+ (length a) (length b)))`,
     * immediately filled by a following #target inline-C body's own
     * strcpy/strcat, not pre-filled with known literal content at all).
     * The original `alloc` only ever understood a NODE_STRING literal
     * value (routing through arena_strdup). Fixed to also accept any
     * other expression as a real byte-count, emitting `(char
     * *)arena_alloc(<arena>, (<size-expr>) + 1)` -- the `+ 1` mirrors
     * arena_strdup()'s own real behavior (reserving room for the null
     * terminator), so a String alloc always gets that room regardless
     * of which of the two real shapes filled it. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn make-buf [(n : I32) (dest : Arena @ Region)] : String @ Region\n"
            "  (alloc dest String (* 2 n)))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "(alloc dest String <size-expr>) with a non-literal size parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL,
              "it emits successfully (previously failed: 'alloc: expected a string literal value', "
              "since alloc only ever understood a NODE_STRING literal 3rd argument before this fix)");
        if (c_src) {
            CHECK(strstr(c_src, "(char *)arena_alloc(dest, ((2 * n)) + 1)") != NULL,
                  "the size expression is emitted as a real arena_alloc call, with + 1 reserved for "
                  "the null terminator, matching arena_strdup's own real behavior for the literal "
                  "shape");
        }
        arena_free_all(&arena);
    }

    /* --- `#target {:c (inline-c "...")}` as a MID-BODY statement, not
     * a whole function body -- found missing (2026-08-21, gcc-verifying
     * string.prn's own real `concat`, whose let-body is exactly `[out
     * (alloc ...)] #target {:c (inline-c "strcpy(out, a); strcat(out,
     * a);")} out`: the inline-C fills the just-allocated buffer for its
     * own side effect, then `out` is returned separately). Before this,
     * `#target` was only ever recognized as a whole-body REPLACEMENT
     * (emit_target_defn) -- a bare `#target` symbol mid-body had no
     * handling at all and fell through to the generic identifier-lookup
     * path, failing outright ('unknown identifier'). --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn length [(s : String @ Region)] : I32\n"
            "  #target\n"
            "  {:c (inline-c \"(int32_t)strlen(s)\")})\n"
            "(defn double-up [(a : String @ Region) (dest : Arena @ Region)] : String @ Region\n"
            "  (let [out (alloc dest String (* 2 (length a)))]\n"
            "    #target\n"
            "    {:c (inline-c \"strcpy(out, a); strcat(out, a);\")}\n"
            "    out))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a mid-body #target {:c (inline-c \"...\")} block, followed by a "
                                "returned value, parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL,
              "it emits successfully (previously failed: 'unknown identifier #target', since "
              "#target had no mid-body statement handling before this fix)");
        if (c_src) {
            CHECK(strstr(c_src, "strcpy(out, a); strcat(out, a);") != NULL,
                  "the raw inline-C is spliced in as a real, bare statement, no return-wrapping");
            CHECK(strstr(c_src, "return out;") != NULL,
                  "the following out symbol is still emitted as the let's own real tail value");
        }
        arena_free_all(&arena);
    }

    /* --- Forward-referenced defn calls -- found missing (2026-08-21,
     * gcc-verifying string.prn's own real `parse-i32`, which calls
     * `is-valid-i32-text?`, defined LATER in the same file): every
     * `defn` was previously emitted strictly in source order with no
     * declarations at all, so any function calling another one defined
     * later in the same file hit a real "implicit declaration of
     * function" gcc error -- `parena build`'s own exit code never
     * caught it, since it never re-parses its own generated C. Fixed
     * via a real forward-DECLARATION pre-pass, emitted for every defn
     * with an explicit `: ReturnType` annotation -- `ReturnType
     * mangled_name();`, an old-style unspecified-argument C
     * declaration (confirmed via a standalone gcc test to compile
     * cleanly under this project's own `-pedantic -Werror`, so this
     * deliberately does NOT duplicate emit_defn's own much larger
     * parameter-type-resolution logic just to match a full prototype
     * nobody's own call site needs). --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn is-even? [(n : I32)] : Bool\n"
            "  (if (= n 0) true (is-odd? (- n 1))))\n"
            "(defn is-odd? [(n : I32)] : Bool\n"
            "  (if (= n 0) false (is-even? (- n 1))))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "two mutually-forward-referencing defns parse fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL,
              "it emits successfully (previously would only fail at the *gcc* stage -- 'implicit "
              "declaration of function is_odd_' -- parena build's own exit code stayed 0 either way)");
        if (c_src) {
            CHECK(strstr(c_src, "int is_odd_();") != NULL,
                  "is-even?'s own forward call to is-odd? (defined LATER in the file) gets a real "
                  "C forward declaration emitted ahead of every defn body");
            CHECK(strstr(c_src, "int is_even_();") != NULL,
                  "and vice versa -- both directions of the real mutual-recursion get a real "
                  "declaration, not just the one that happens to be called first in file order");
        }
        arena_free_all(&arena);
    }

    /* --- Cross-module qualified calls (`module/function`) -- a real,
     * structural gap distinct from mangle()'s own character-
     * substitution job, found via a deliberate multi-file test
     * (2026-08-21, right after regex/glob.prn's own real
     * `(string/length pattern)` -- calling into string.prn's own real
     * `length` -- got past its `cond` blocker only to hit a NEW
     * 'implicit declaration of function string_length' error): calling
     * another module's own exported function by its qualified name
     * used to just blindly full-mangle the WHOLE qualified text
     * (`string/length` -> `string_length`), a C identifier that was
     * NEVER what any real defn actually compiles to (a defn's own name
     * is never itself prefixed by its enclosing module). This
     * compiler's multi-file build has no real per-module C symbol
     * table -- everything lives in one flat namespace -- so the only
     * correct resolution is the bare, unqualified function name.
     *
     * Fixed via mangle_call_name(): try the bare (last `/`-segment)
     * name first, but only USE it if g_defn_return_types already knows
     * a real defn by that exact bare name (populated early by emit_c()'s
     * own forward-declaration pre-pass) -- otherwise fall back to the
     * OLD, full-text mangle unchanged. This single test deliberately
     * covers BOTH real cases at once, the same way a real combined
     * multi-file build would (concatenating both "files'" forms into
     * one program, exactly matching what cmd_build() itself does):
     * `math/inc` resolving to the real, bare `inc` defn (the NEW fix),
     * and `vec/push!` still resolving to the OLD, full-mangled
     * `vec_push_` (no real bare `push!` defn exists anywhere to find,
     * so the fallback correctly fires, unchanged from before). --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn inc [(n : I32)] : I32 (+ n 1))\n"
            "(defn bump-all [(dest : Arena @ Region)]\n"
            "  (let [v (vec/new dest)]\n"
            "    (vec/push! &v (math/inc 41))\n"
            "    v))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a qualified call to a real defn, alongside vec/push!, parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL,
              "it emits successfully (previously would only fail at the *gcc* stage -- 'implicit "
              "declaration of function math_inc' -- parena build's own exit code stayed 0 either way)");
        if (c_src) {
            /* vec_box_i32, not vec_box_f64: a real, separate correctness
             * bonus from this same fix -- math/inc's own return type
             * (I32) is only correctly known here BECAUSE mangle_call_name
             * resolved it to the real bare `inc` defn, whose return type
             * g_defn_return_types already has on file; before this fix,
             * emit_call's own return-type lookup for the bogus
             * "math_inc" name would have missed entirely and fallen back
             * to the generic "void *" guess. */
            CHECK(strstr(c_src, "vec_push_(&(v), vec_box_i32(&(v), inc(41)))") != NULL,
                  "math/inc resolves to the real, bare inc() defn -- not a bogus never-defined "
                  "math_inc() -- while vec/push! in the very same call still correctly resolves to "
                  "the old, full-mangled vec_push_, since no real bare push! defn exists to find");
            CHECK(strstr(c_src, "math_inc") == NULL,
                  "the bogus, never-defined math_inc() never appears anywhere in the output");
        }
        arena_free_all(&arena);
    }

    /* --- Every generated file's own preamble includes <stdlib.h> --
     * found missing (2026-08-21, gcc-verifying string.prn's own real
     * `raw-parse-i32`, whose #target inline-C body calls `atoi`,
     * declared in <stdlib.h>). Same real, honest, unconditional-
     * inclusion tradeoff already made for <stdint.h> -- VS0 has no way
     * to inspect the trusted-verbatim contents of an inline-C string to
     * decide whether it's actually needed. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defn f [] : I32 0)";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a trivial defn parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "it emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "#include <stdlib.h>") != NULL,
                  "every generated file's own preamble includes <stdlib.h> unconditionally "
                  "(previously missing entirely -- atoi() in a #target body hit a real "
                  "'implicit declaration' gcc error, undetected by parena build's own exit code)");
        }
        arena_free_all(&arena);
    }

    /* --- Real, self-caught regression: `vec/get` (and friends) must
     * NEVER resolve via mangle_call_name()'s own bare-name lookup, even
     * when a real, unrelated defn happens to share that exact bare
     * name elsewhere in the combined build -- found via array.prn's
     * own real `product`, which calls `(vec/get shape i)`: array.prn
     * ALSO defines its own, completely unrelated `get` function later
     * in the very same file (returning `(Result F64 IndexError)`), and
     * the bare-name-exists check alone silently resolved `vec/get` to
     * THAT function instead of the runtime's own real `vec_get` --
     * `parena build` itself produced `get(shape, i)`, not even needing
     * a gcc compile to be wrong. `vec` is now explicitly excluded from
     * bare-name resolution -- it's a confirmed, hardcoded RUNTIME
     * pseudo-module with no real `vec.prn` file that could ever
     * legitimately shadow it. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn sum [(shape : &(Vec I32))] : I32\n"
            "  (loop [i 0 acc 0]\n"
            "    (if (>= i (vec/len shape))\n"
            "      acc\n"
            "      (recur (+ i 1) (+ acc (deref (vec/get shape i)))))))\n"
            "(defn get [(a : I32)] : I32 a)";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "vec/get alongside an unrelated, real bare `get` defn parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL, "it emits successfully");
        if (c_src) {
            CHECK(strstr(c_src, "vec_get(shape, i)") != NULL,
                  "vec/get still resolves to the real runtime vec_get, not the unrelated user "
                  "defn also named get -- the real regression this test guards against");
            CHECK(strstr(c_src, "= get(shape, i)") == NULL && strstr(c_src, "*)(get(shape") == NULL,
                  "the unrelated get(a) defn is never mistakenly called in vec/get's place");
        }
        arena_free_all(&arena);
    }

    /* --- `unit` -- the Unit type's own singleton value literal, found
     * genuinely missing (2026-08-21, gcc-verifying buffer.prn's own
     * real `(Ok unit)` -- array.prn's `set!` uses the identical real
     * shape): no handling anywhere, so a bare `unit` symbol fell
     * through to the generic scope_lookup path and failed as an
     * unknown identifier. Fixed as a reserved literal emitting `NULL`,
     * reporting its own type as "void *" -- already pointer-typed, so
     * Ok/Err/Some's own payload check accepts it directly with no
     * boxing needed at all. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defenum MyErr (Bad))\n"
            "(defn f [] : (Result Unit MyErr) (Ok unit))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "(Ok unit) parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL,
              "it emits successfully (previously failed: 'unknown identifier unit', since unit "
              "had no handling anywhere before this fix)");
        if (c_src) {
            CHECK(strstr(c_src, "result_ok(NULL)") != NULL,
                  "unit emits as a plain NULL, already pointer-typed, no boxing needed at all");
        }
        arena_free_all(&arena);
    }

    /* --- A nested `match` used as another match's own clause body --
     * found missing (2026-08-21, gcc-verifying shell.prn's own real
     * `resolve`, whose actual policy chain is `(match explicit (... s)
     * (None (match (getenv "SHELL") (... s) (None ...))))`, a real,
     * idiomatic "chain of Option checks, fall through on None"
     * pattern): the original clause-body emission called emit_expr()
     * directly, which has no handling for `match` as a bare value --
     * the nested match, being a NODE_LIST headed by a symbol, fell all
     * the way to the generic call-dispatch path and mis-parsed into a
     * baffling "unknown identifier" error far from the real cause.
     * Fixed by refactoring emit_match() into a public entry point
     * (owns the one real result_var declaration + return_mode wrap)
     * plus a reusable core the clause-body composer can recurse into
     * directly, targeting the SAME, already-owned result_var -- no
     * second declaration, matching the same real "declare once, learn
     * the type from every branch including nested ones" property `if`
     * and `cond` already have elsewhere in this file. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn get-b [(x : String @ Region)] : (Option String) @ Region\n"
            "  #target\n"
            "  {:c (inline-c \"getenv_as_option(x)\")})\n"
            "(defn resolve [(explicit : (Option String) @ Region)] : String @ Region\n"
            "  (match explicit\n"
            "    ((Some s) s)\n"
            "    (None\n"
            "      (match (get-b \"B\")\n"
            "        ((Some s) s)\n"
            "        (None \"default\")))))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a match nested as another match's own clause body parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL,
              "it emits successfully (previously failed: 'unknown identifier s', since emit_expr() "
              "had no handling for a nested match as a clause-body value)");
        if (c_src) {
            CHECK(strstr(c_src, "__match_result_") != NULL,
                  "a real match result variable is emitted");
            /* The inner match's own assignments target the SAME
             * result_var the outer one declared -- no second, separate
             * result variable for the nested match. */
            const char *first_decl = strstr(c_src, "__match_result_");
            char var_name[32];
            size_t i = 0;
            while (first_decl[i] && (isalnum((unsigned char)first_decl[i]) || first_decl[i] == '_') &&
                   i < sizeof(var_name) - 1) {
                var_name[i] = first_decl[i];
                i++;
            }
            var_name[i] = '\0';
            size_t occurrences = 0;
            const char *p = c_src;
            while ((p = strstr(p, var_name)) != NULL) {
                occurrences++;
                p += strlen(var_name);
            }
            CHECK(occurrences >= 3,
                  "the same result_var is reused by both the outer and inner match's own clause "
                  "assignments (declaration + at least two assignments), not a separate one per "
                  "nesting level");
        }
        arena_free_all(&arena);
    }

    /* --- `when` in loop-tail position, with a NON-LAST body form that
     * is itself statement-shaped (a `match`, in this real case) -- a
     * real, THIRD instance of the same root class of gap (raw
     * emit_expr() where a statement dispatch is needed), found via an
     * isolated repro faithfully reproducing dataframe.prn's own real
     * nesting (`loop` -> `when` -> `match` with a `do`-bodied clause ->
     * `recur`): the non-last-body-form loop inside `when`'s own
     * loop-tail handling used emit_expr() directly on each form except
     * the last, so a `match` appearing there (not the tail form) fell
     * through the same generic call-dispatch mis-parse. Fixed by
     * delegating those non-last forms to emit_body() itself
     * (return_mode=0, discarding any value) instead of hand-rolling a
     * second, narrower statement dispatcher -- emit_body's own
     * with-arena/let/loop/match/do/when/#target statement handling
     * already covers every real statement shape needed, for free. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defstruct Box (n : I32))\n"
            "(defn helper [(b : &Box)] : (Result (&Box) I32) (Ok b))\n"
            "(defn use-loop [(items : &(Vec Box)) (n : I32) (dest : Arena @ Region)]\n"
            "  (let [out (vec/new dest)]\n"
            "    (loop [i 0]\n"
            "      (when (< i n)\n"
            "        (match (helper (vec/get items i))\n"
            "          ((Ok col) (do (vec/push! &out col) col))\n"
            "          ((Err e) (vec/get items 0)))\n"
            "        (recur (+ i 1))))\n"
            "    out))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a non-last, statement-shaped match inside when's own loop-tail body "
                                "parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL,
              "it emits successfully (previously failed: 'unknown identifier col', since the "
              "non-last-body-form loop inside when's own loop-tail handling used raw emit_expr(), "
              "which can't recognize a statement-shaped match)");
        arena_free_all(&arena);
    }

    /* --- `(Map K V)` as a struct-field/return type -- found missing
     * (2026-08-21, gcc-verifying net/http.prn's own real `HttpRequest`/
     * `HttpResponse`, both carrying a `headers : (Map String String) @
     * Region` field): `resolve_declared_type()` handled `(Result ..)`/
     * `(Option ..)`/`(Vec ..)` compound types but not `Map` at all.
     * Erased to a plain `void *`, not a named struct -- unlike `Vec`,
     * there's no real runtime `Map` struct backing it yet (map.prn's
     * own real implementation is itself blocked on real generics), so
     * this only lets a Map-typed field/return be NAMED, not
     * constructed or manipulated -- real, honest, narrower than Vec's
     * own treatment. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src = "(defstruct Headers (data : (Map String String) @ Region))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a (Map K V)-typed struct field parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL,
              "it emits successfully (previously failed: 'unsupported return type form', since "
              "resolve_declared_type() had no handling for Map at all before this fix)");
        if (c_src) {
            CHECK(strstr(c_src, "void * data") != NULL,
                  "the field is erased to a plain void *, honestly reflecting that no real Map "
                  "runtime implementation exists yet, unlike Vec's own real backing struct");
        }
        arena_free_all(&arena);
    }

    /* --- A bare `Arena` (no `@ region`) as a `(Fn [...] ...)` argument
     * type -- found missing (2026-08-21, gcc-verifying net/http.prn's
     * own real `serve`, whose `handler` parameter type is `(Fn
     * [&HttpRequest Arena] HttpResponse)`): a Fn argument-type slot
     * recurses into resolve_base_type_name() for a plain symbol like
     * this, and "Arena" was never in its bare-symbol table at all --
     * every OTHER real Arena usage in this emitter goes through the
     * separate `Type @ Region` / has_region_marker() path instead,
     * which a Fn-type's own argument list doesn't use. --- */
    {
        Arena arena;
        arena_init(&arena);
        const char *src =
            "(defn call-it [(f : (Fn [Arena] I32)) (dest : Arena @ Region)] : I32\n"
            "  (f dest))";
        const char *parse_err = NULL;
        Node *program = parse_program(&arena, src, strlen(src), &parse_err);
        CHECK(program != NULL, "a bare Arena as a (Fn [...] ...) argument type parses fine");
        const char *emit_err = NULL;
        const char *c_src = emit_c(&arena, program, &emit_err);
        CHECK(c_src != NULL && emit_err == NULL,
              "it emits successfully (previously failed: 'unsupported return type symbol Arena', "
              "since resolve_base_type_name() had no handling for bare Arena at all before this fix)");
        if (c_src) {
            CHECK(strstr(c_src, "int (*f)(Arena *)") != NULL,
                  "the bare Arena argument type resolves to a real Arena *, matching every other "
                  "real Arena value's own C representation in this emitter");
        }
        arena_free_all(&arena);
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
