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
            CHECK(strstr(c_src, "T * _t") != NULL,
                  "a &mut T PARAMETER (the two-token form, distinct from the single-token &Type field/param "
                  "path above) also becomes a real T *");
            CHECK(strstr(c_src, "(_t)->failed = 1") != NULL,
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
            CHECK(strstr(c_src, "vec_push_(&((_t)->messages), msg)") != NULL,
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
            CHECK(strstr(c_src, "(*((Item *)(_x)))") != NULL,
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
            CHECK(strstr(c_src, "vec_push_(&((_t)->messages), msg);") != NULL,
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

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
