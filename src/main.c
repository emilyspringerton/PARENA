/* main.c — the parena CLI. VS0's Definition of Done specifies a single
 * `./parena build input.prn -o output.c` command driving the full
 * parse -> region-analyze -> emit pipeline. Only the parse stage exists
 * so far (S189-13, in progress) -- `parse` is a real, honest subcommand
 * for that stage alone; `build` exists but reports clearly that the
 * region analyzer and C emitter aren't wired in yet rather than silently
 * doing nothing or emitting something misleading.
 */
#include "arena.h"
#include "ast.h"
#include "emit.h"
#include "emit_java.h"
#include "emit_ts.h"
#include "fmt.h"
#include "parser.h"
#include "region.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

/* PARENA_HAS_CI_STATUS -- real, deliberate two-stage bootstrap, not
 * dead code: `parena ci-status` (founder, real-time, 2026-08-21: "add
 * it to the parena cli") calls into `check()`, the C function
 * stdlib/ci/status.prn itself compiles down to (a REAL PARENA module,
 * not hand-written C -- see that file's own header comment) -- but
 * `check()` only exists AFTER some already-built `parena` has compiled
 * that module. This macro is defined ONLY by the Makefile's own
 * second build stage (`make build`, see its own comment), which links
 * against the just-generated tools/ci_status_gen.c; the first,
 * bootstrap-only stage compiles main.c WITHOUT it, so the subcommand
 * this guards (and the extern declaration it needs) simply isn't
 * compiled in yet at that point -- there's genuinely nothing real to
 * call. */
#ifdef PARENA_HAS_CI_STATUS
extern int check(char *repo, char *sha, char *token);
#endif

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';
    *out_len = n;
    return buf;
}

static const char *node_type_name(NodeType t) {
    switch (t) {
        case NODE_LIST: return "list";
        case NODE_VEC: return "vec";
        case NODE_MAP: return "map";
        case NODE_SYMBOL: return "symbol";
        case NODE_KEYWORD: return "keyword";
        case NODE_STRING: return "string";
        case NODE_NUMBER: return "number";
        case NODE_COLON: return ":";
        case NODE_AT: return "@";
    }
    return "?";
}

static void print_node(Node *n, int depth) {
    for (int i = 0; i < depth; i++) printf("  ");
    if (n->text) {
        printf("%s: %s (line %d)\n", node_type_name(n->type), n->text, n->line);
    } else if (n->type == NODE_LIST || n->type == NODE_VEC || n->type == NODE_MAP) {
        printf("%s (line %d, %zu children)\n", node_type_name(n->type), n->line, n->child_count);
    } else {
        printf("%s (line %d)\n", node_type_name(n->type), n->line);
    }
    for (size_t i = 0; i < n->child_count; i++) print_node(n->children[i], depth + 1);
}

static int cmd_parse(const char *path) {
    size_t len;
    char *src = read_file(path, &len);
    if (!src) {
        fprintf(stderr, "parena: cannot read %s\n", path);
        return 1;
    }
    Arena arena;
    arena_init(&arena);
    const char *err = NULL;
    Node *program = parse_program(&arena, src, len, &err);
    free(src);
    if (!program) {
        fprintf(stderr, "parena: parse error: %s\n", err);
        arena_free_all(&arena);
        return 1;
    }
    print_node(program, 0);
    arena_free_all(&arena);
    return 0;
}

/* cmd_analyze — VS0 domain 2, the region analyzer. Parses `path`, then
 * runs region_analyze() over the resulting AST. Real, honest scope
 * (see region.h's own header comment): checks the assignment invariant
 * only, not the full region-safety story yet. */
static int cmd_analyze(const char *path) {
    size_t len;
    char *src = read_file(path, &len);
    if (!src) {
        fprintf(stderr, "parena: cannot read %s\n", path);
        return 1;
    }
    Arena arena;
    arena_init(&arena);
    const char *parse_err = NULL;
    Node *program = parse_program(&arena, src, len, &parse_err);
    free(src);
    if (!program) {
        fprintf(stderr, "parena: parse error: %s\n", parse_err);
        arena_free_all(&arena);
        return 1;
    }
    const char *region_err = region_analyze(&arena, program);
    if (region_err) {
        fprintf(stderr, "parena: %s\n", region_err);
        arena_free_all(&arena);
        return 1;
    }
    printf("parena: %s: region analysis OK\n", path);
    arena_free_all(&arena);
    return 0;
}

/* cmd_build — VS0 domain 3, the C emitter, wired into the full DoD
 * pipeline: parse -> region-analyze (abort on error, same as cmd_analyze)
 * -> emit -> write output.c. Real, honest scope carried from emit.h's own
 * header comment: only understands the shape examples/test.prn's own
 * load-config uses; a real "unsupported" error is reported (not guessed
 * C) for anything past that.
 *
 * Real, minimal multi-file support added 2026-08-20: `paths`/`path_count`
 * -- one or more input files -- are each parsed separately, then their
 * own top-level forms are concatenated into ONE combined program node
 * (node_new_compound()/node_push_child(), the same real AST-construction
 * primitives the parser itself uses) before region-analysis/emission run
 * over the whole thing as a single unit. This is real, if honestly
 * minimal: not a real linker (no separate compilation units, no
 * per-module namespacing, no cross-file symbol resolution beyond "every
 * top-level form from every input file is now visible to every other
 * one, in file order") -- but it's exactly what closes the real,
 * repeatedly-hit "T, defined in firefly.prn, isn't visible when
 * firefly/ladybug.prn is compiled standalone" gap this same session kept
 * running into, without pretending to have built a full module system.
 * `(module ...)`/`(import ...)` forms remain the same real no-ops they
 * already were -- multi-file `build` doesn't read or validate them,
 * ordering the files correctly on the command line is still the real,
 * human responsibility it always was. */
/* cmd_fmt -- `parena fmt [-w] <file.prn> [file2.prn ...]`. Default
 * (no -w) matches gofmt's own default: print the reformatted source to
 * stdout, one file's output right after another, leave the file on
 * disk untouched. `-w` writes each result back into its own source
 * file in place -- gofmt's own real, most-used mode ("comeon bro", the
 * founder's own real-time framing, is a request for the tool that just
 * fixes the files, not a printer). See fmt.c's own header comment for
 * this pass's real scope (depth-based re-indentation, comment/string-
 * preserving, not full AST-based pretty-printing). */
static int cmd_fmt(const char **paths, size_t path_count, int write_in_place) {
    int had_error = 0;
    for (size_t i = 0; i < path_count; i++) {
        size_t len;
        char *src = read_file(paths[i], &len);
        if (!src) {
            fprintf(stderr, "parena: cannot read %s\n", paths[i]);
            had_error = 1;
            continue;
        }
        Arena arena;
        arena_init(&arena);
        const char *formatted = fmt_source(&arena, src, len);
        free(src);
        if (write_in_place) {
            FILE *out = fopen(paths[i], "wb");
            if (!out) {
                fprintf(stderr, "parena: cannot write %s\n", paths[i]);
                had_error = 1;
            } else {
                fputs(formatted, out);
                fclose(out);
            }
        } else {
            fputs(formatted, stdout);
        }
        arena_free_all(&arena);
    }
    return had_error;
}

/* java_class_name_from_path -- real javac requirement: a `.java` file's own public top-level
 * class must match the file's own basename exactly. Derives that name from `out_path` (strips any
 * leading directory components, strips the trailing ".java"), arena-owned so it outlives the call
 * into emit_java(). Real, narrow: doesn't validate the result is a legal Java identifier (no
 * leading digit, no reserved word) -- same honest "garbage in, garbage out" scope emit_java.c's
 * own real, narrow v0 already draws elsewhere; a human picking a sane output filename is assumed,
 * same real assumption cmd_build already makes about every other -o argument. */
static const char *java_class_name_from_path(Arena *arena, const char *out_path) {
    const char *base = out_path;
    for (const char *p = out_path; *p; p++) {
        if (*p == '/') base = p + 1;
    }
    size_t base_len = strlen(base);
    if (base_len > 5 && strcmp(base + base_len - 5, ".java") == 0) {
        base_len -= 5;
    }
    return arena_strdup(arena, base, base_len);
}

/* java_package_name_from_path -- real, standard Maven/Gradle convention: everything under
 * "src/main/java/" up to the output file's own directory maps 1:1 to the Java package, slashes
 * becoming dots (e.g. ".../src/main/java/industrial/einhorn/gta7/generated/Foo.java" ->
 * "industrial.einhorn.gta7.generated"). Returns an arena-owned string, "" if `out_path` doesn't
 * contain that marker (no package -- the default/unnamed package, same real behavior as before
 * this convention was added). Real, narrow: only understands this one, standard layout -- a
 * caller not using it gets no package declaration, not a guessed-at one. */
static const char *java_package_name_from_path(Arena *arena, const char *out_path) {
    static const char MARKER[] = "src/main/java/";
    const char *marker_pos = strstr(out_path, MARKER);
    if (!marker_pos) return "";
    const char *pkg_start = marker_pos + (sizeof(MARKER) - 1);
    const char *last_slash = NULL;
    for (const char *p = pkg_start; *p; p++) {
        if (*p == '/') last_slash = p;
    }
    if (!last_slash || last_slash == pkg_start) return "";
    size_t pkg_len = (size_t)(last_slash - pkg_start);
    char *pkg = arena_alloc(arena, pkg_len + 1);
    for (size_t i = 0; i < pkg_len; i++) {
        pkg[i] = (pkg_start[i] == '/') ? '.' : pkg_start[i];
    }
    pkg[pkg_len] = '\0';
    return pkg;
}

static int cmd_build(const char **paths, size_t path_count, const char *out_path) {
    Arena arena;
    arena_init(&arena);
    Node *program = node_new_compound(&arena, NODE_LIST, 1);
    for (size_t i = 0; i < path_count; i++) {
        size_t len;
        char *src = read_file(paths[i], &len);
        if (!src) {
            fprintf(stderr, "parena: cannot read %s\n", paths[i]);
            arena_free_all(&arena);
            return 1;
        }
        const char *parse_err = NULL;
        Node *file_program = parse_program(&arena, src, len, &parse_err);
        free(src);
        if (!file_program) {
            fprintf(stderr, "parena: %s: parse error: %s\n", paths[i], parse_err);
            arena_free_all(&arena);
            return 1;
        }
        for (size_t c = 0; c < file_program->child_count; c++) {
            node_push_child(&arena, program, file_program->children[c]);
        }
    }
    const char *region_err = region_analyze(&arena, program);
    if (region_err) {
        fprintf(stderr, "parena: %s\n", region_err);
        arena_free_all(&arena);
        return 1;
    }
    /* Real, minimal target dispatch by output extension -- `-o output.ts` routes to the real v0
       TypeScript emitter (emit_ts.h's own doc comment has the full real scope statement), `-o
       output.java` routes to the real v0 Java emitter (emit_java.h's own doc comment), any other
       extension keeps the existing, default, unchanged C emitter path. No new subcommand/flag
       needed; every existing `-o output.c` caller is completely unaffected. */
    size_t out_path_len = strlen(out_path);
    int is_ts_target = out_path_len >= 3 && strcmp(out_path + out_path_len - 3, ".ts") == 0;
    int is_java_target = out_path_len >= 5 && strcmp(out_path + out_path_len - 5, ".java") == 0;

    const char *emit_err = NULL;
    const char *emitted_source;
    if (is_ts_target) {
        emitted_source = emit_ts(&arena, program, &emit_err);
    } else if (is_java_target) {
        const char *class_name = java_class_name_from_path(&arena, out_path);
        const char *package_name = java_package_name_from_path(&arena, out_path);
        emitted_source = emit_java(&arena, program, class_name, package_name, &emit_err);
    } else {
        emitted_source = emit_c(&arena, program, &emit_err);
    }
    if (!emitted_source) {
        fprintf(stderr, "parena: %s\n", emit_err);
        arena_free_all(&arena);
        return 1;
    }
    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "parena: cannot write %s\n", out_path);
        arena_free_all(&arena);
        return 1;
    }
    fputs(emitted_source, out);
    fclose(out);
    if (path_count == 1) {
        printf("parena: %s -> %s\n", paths[0], out_path);
    } else {
        printf("parena: [%zu files] -> %s\n", path_count, out_path);
    }
    arena_free_all(&arena);
    return 0;
}

/* cp_file — real, minimal whole-file copy, the one small helper cmd_new needs to bring the real
 * runtime/parena_runtime.h + .c into a new scaffolded project (a consuming repo shouldn't need
 * PARENA's own source tree checked out at a known relative path just to build its own scaffold).
 * Real, honest v0 assumption, named directly: `parena new` is run from the PARENA repo's own
 * root (matching how every real invocation in this whole repo's own Makefile/tests already
 * assumes `./parena` is run from here) — reads `runtime/<name>` relative to the CURRENT working
 * directory, not relative to argv[0] (a real, more portable fix, deliberately not attempted in
 * this v0 pass). */
static int cp_file(const char *src_path, const char *dst_path) {
    size_t len;
    char *data = read_file(src_path, &len);
    if (!data) return 0;
    FILE *out = fopen(dst_path, "wb");
    if (!out) {
        free(data);
        return 0;
    }
    fwrite(data, 1, len, out);
    fclose(out);
    free(data);
    return 1;
}

/* cmd_new — real, new "batteries included" scaffolding command (kanban priority-queue card
 * PXCL-9311: "make parena cli tool do the same thing burrow new does but for C instead of for
 * go"), the direct real C-target sibling of BURROW's own already-shipped `burrow new`. Generates
 * a real, immediately-runnable starter: `<name>.prn` (a minimal PARENA decision-logic module),
 * `<name>_gen.c` (the real, compiled C output), `main.c` (a real C host including the generated
 * output directly and calling into it), and a real, local copy of `runtime/parena_runtime.h`/
 * `.c` (so the new project doesn't need PARENA's own source tree at a known relative path) —
 * then actually compiles AND RUNS the result via a real `gcc` invocation before returning
 * success, so a broken scaffold is a real, honest failure here, never silently handed to the
 * user. Real, deliberate v0 scope: built headless (`-DPARENA_NO_GRAPHICS`, this same session's
 * own real opt-out for the SDL2-by-default runtime), matching `burrow new`'s own real "don't
 * force a heavier dependency on a trivial scaffold" judgment. */
static int cmd_new(const char *name) {
    struct stat st;
    if (stat(name, &st) == 0) {
        fprintf(stderr, "parena: new: %s already exists\n", name);
        return 1;
    }
    char pathbuf[1024];
    if (mkdir(name, 0755) != 0) {
        fprintf(stderr, "parena: new: cannot create directory %s\n", name);
        return 1;
    }
    snprintf(pathbuf, sizeof(pathbuf), "%s/runtime", name);
    if (mkdir(pathbuf, 0755) != 0) {
        fprintf(stderr, "parena: new: cannot create %s\n", pathbuf);
        return 1;
    }

    char prn_path[1024], main_path[1024], gen_path[1024], rt_h_dst[1024], rt_c_dst[1024];
    snprintf(prn_path, sizeof(prn_path), "%s/%s.prn", name, name);
    snprintf(main_path, sizeof(main_path), "%s/main.c", name);
    snprintf(gen_path, sizeof(gen_path), "%s/%s_gen.c", name, name);
    snprintf(rt_h_dst, sizeof(rt_h_dst), "%s/runtime/parena_runtime.h", name);
    snprintf(rt_c_dst, sizeof(rt_c_dst), "%s/runtime/parena_runtime.c", name);

    if (!cp_file("runtime/parena_runtime.h", rt_h_dst) || !cp_file("runtime/parena_runtime.c", rt_c_dst)) {
        fprintf(stderr, "parena: new: could not find runtime/parena_runtime.h/.c -- run 'parena new' from the PARENA repo's own root\n");
        return 1;
    }

    FILE *prn = fopen(prn_path, "wb");
    if (!prn) {
        fprintf(stderr, "parena: new: cannot write %s\n", prn_path);
        return 1;
    }
    fprintf(prn, "(module %s)\n(export hello)\n\n(defn hello [] : String\n  \"Hello from %s!\")\n", name, name);
    fclose(prn);

    FILE *main_c = fopen(main_path, "wb");
    if (!main_c) {
        fprintf(stderr, "parena: new: cannot write %s\n", main_path);
        return 1;
    }
    fprintf(main_c,
        "/* Real, \"batteries included\" scaffold generated by \"parena new\" -- the PARENA\n"
        " * decision logic (%s.prn) is compiled into %s_gen.c (regenerate via:\n"
        " * parena build %s.prn -o %s_gen.c) and included directly here, matching the real\n"
        " * host-glue pattern this repo's own tools/*_host.c files already establish. */\n"
        "#define PARENA_NO_GRAPHICS\n"
        "#include \"runtime/parena_runtime.h\"\n"
        "#include \"%s_gen.c\"\n"
        "#include <stdio.h>\n\n"
        "int main(void) {\n"
        "    printf(\"%%s\\n\", hello());\n"
        "    return 0;\n"
        "}\n",
        name, name, name, name, name);
    fclose(main_c);

    /* Real, in-process build: same pipeline cmd_build itself uses (parse -> region-analyze ->
     * emit), not a re-exec of this same binary, so a real error here reports precisely. */
    Arena arena;
    arena_init(&arena);
    size_t src_len;
    char *src = read_file(prn_path, &src_len);
    if (!src) {
        fprintf(stderr, "parena: new: internal error reading %s back\n", prn_path);
        return 1;
    }
    const char *parse_err = NULL;
    Node *program = parse_program(&arena, src, src_len, &parse_err);
    free(src);
    if (!program) {
        fprintf(stderr, "parena: new: internal error generating a real starter .prn: %s\n", parse_err);
        arena_free_all(&arena);
        return 1;
    }
    const char *region_err = region_analyze(&arena, program);
    if (region_err) {
        fprintf(stderr, "parena: new: internal error generating a real starter .prn: %s\n", region_err);
        arena_free_all(&arena);
        return 1;
    }
    const char *emit_err = NULL;
    const char *emitted = emit_c(&arena, program, &emit_err);
    if (!emitted) {
        fprintf(stderr, "parena: new: internal error generating a real starter .prn: %s\n", emit_err);
        arena_free_all(&arena);
        return 1;
    }
    FILE *gen = fopen(gen_path, "wb");
    if (!gen) {
        fprintf(stderr, "parena: new: cannot write %s\n", gen_path);
        arena_free_all(&arena);
        return 1;
    }
    fputs(emitted, gen);
    fclose(gen);
    arena_free_all(&arena);

    /* Real, "batteries included" step: actually compile+link+RUN the new scaffold right now, not
     * just drop template text and hope. A scaffold that doesn't build is a real bug in this
     * command, not something the user should discover themselves. */
    char cmd[4096];
    char bin_path[1024];
    snprintf(bin_path, sizeof(bin_path), "%s/%s_bin", name, name);
    snprintf(cmd, sizeof(cmd), "cc -std=c99 -I %s/runtime -o %s %s %s/runtime/parena_runtime.c -lm",
             name, bin_path, main_path, name);
    if (system(cmd) != 0) {
        fprintf(stderr, "parena: new: generated scaffold failed to build (this is a real bug in \"parena new\", not your code)\n");
        return 1;
    }
    snprintf(cmd, sizeof(cmd), "./%s", bin_path);
    if (system(cmd) != 0) {
        fprintf(stderr, "parena: new: generated scaffold built but failed to run\n");
        return 1;
    }

    printf("parena: new: %s scaffolded and built successfully\n", name);
    printf("  %-30s -- real PARENA decision logic (edit this)\n", prn_path);
    printf("  %-30s -- real C host (edit this)\n", main_path);
    printf("  %-30s -- compiled output (regenerate: parena build %s.prn -o %s_gen.c)\n", gen_path, name, name);
    printf("  cd %s && ./%s_bin\n", name, name);
    return 0;
}

#ifdef PARENA_HAS_CI_STATUS
/* cmd_ci_status -- the real PARENA CLI subcommand wired around
 * stdlib/ci/status.prn's own `check()` (see PARENA_HAS_CI_STATUS's own
 * comment above for why this whole function only exists in the
 * second build stage). Reads GITHUB_TOKEN from the environment
 * (matching every other real ops script in this monorepo's own
 * convention) rather than accepting it as a THIRD positional argument
 * -- a real, deliberate choice: a token is a real secret, and argv is
 * visible to every other process on the same machine via /proc/<pid>/
 * cmdline, an env var isn't quite as exposed but still a real,
 * pre-existing convention this repo's own CI scripts already use
 * (`GH_TOKEN`/`GITHUB_TOKEN` passed via `env:`, never as a bare CLI
 * arg) -- kept consistent here rather than inventing a new pattern. */
static int cmd_ci_status(const char *repo, const char *sha) {
    const char *token = getenv("GITHUB_TOKEN");
    if (!token) {
        fprintf(stderr, "parena: ci-status: GITHUB_TOKEN must be set\n");
        return 3;
    }
    int code = check((char *)repo, (char *)sha, (char *)token);
    switch (code) {
        case 0:
            printf("parena: ci-status: %s @ %s: all checks completed, all conclusions success\n", repo, sha);
            break;
        case 1:
            printf("parena: ci-status: %s @ %s: still pending\n", repo, sha);
            break;
        case 2:
            printf("parena: ci-status: %s @ %s: completed, but at least one check failed\n", repo, sha);
            break;
        default:
            printf("parena: ci-status: %s @ %s: no check-runs found, or the request itself failed\n", repo, sha);
            break;
    }
    return code;
}
#endif

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: parena parse <file.prn>\n"
                         "       parena analyze <file.prn>                          (VS0 domain 2 -- region analyzer)\n"
                         "       parena build <file.prn> [file2.prn ...] -o <output.c|output.ts|output.java>    "
                         "(VS0 domain 3 -- C emitter (default), or the real v0 TypeScript/Java emitters if -o "
                         "ends in .ts/.java; multiple files are combined into one compilation unit, in the "
                         "order given)\n"
                         "       parena fmt [-w] <file.prn> [file2.prn ...]         (re-indent; -w writes in place, "
                         "default prints to stdout)\n"
#ifdef PARENA_HAS_CI_STATUS
                         "       parena ci-status <owner/repo> <sha>                (GITHUB_TOKEN env var required; "
                         "exit 0=all green, 1=pending, 2=failed, 3=not found/error)\n"
#endif
                         "       parena new <name>                                  (real, \"batteries included\" "
                         "scaffold: a starter .prn file, a real C host main.c + a local copy of runtime/"
                         "parena_runtime.h/.c, built and run immediately so the scaffold is proven working "
                         "with zero further manual steps)\n"
        );
        return 1;
    }
    if (strcmp(argv[1], "new") == 0 && argc >= 3) {
        return cmd_new(argv[2]);
    }
    if (strcmp(argv[1], "parse") == 0 && argc >= 3) {
        return cmd_parse(argv[2]);
    }
    if (strcmp(argv[1], "analyze") == 0 && argc >= 3) {
        return cmd_analyze(argv[2]);
    }
#ifdef PARENA_HAS_CI_STATUS
    if (strcmp(argv[1], "ci-status") == 0 && argc >= 4) {
        return cmd_ci_status(argv[2], argv[3]);
    }
#endif
    if (strcmp(argv[1], "fmt") == 0) {
        if (argc < 3) {
            fprintf(stderr, "usage: parena fmt [-w] <file.prn> [file2.prn ...]\n");
            return 1;
        }
        int write_in_place = 0;
        int first_path_arg = 2;
        if (strcmp(argv[2], "-w") == 0) {
            write_in_place = 1;
            first_path_arg = 3;
        }
        if (argc <= first_path_arg) {
            fprintf(stderr, "usage: parena fmt [-w] <file.prn> [file2.prn ...]\n");
            return 1;
        }
        size_t path_count = (size_t)(argc - first_path_arg);
        const char **paths = (const char **)malloc(sizeof(char *) * path_count);
        for (size_t i = 0; i < path_count; i++) {
            paths[i] = argv[first_path_arg + i];
        }
        int rc = cmd_fmt(paths, path_count, write_in_place);
        free(paths);
        return rc;
    }
    if (strcmp(argv[1], "build") == 0) {
        /* Every argument between "build" and "-o" is an input file --
         * at least one required. `-o <output.c>` must be the final two
         * arguments, same real, simple convention the single-file form
         * already used. */
        if (argc < 5 || strcmp(argv[argc - 2], "-o") != 0) {
            fprintf(stderr, "usage: parena build <file.prn> [file2.prn ...] -o <output.c|output.ts|output.java>\n");
            return 1;
        }
        size_t path_count = (size_t)(argc - 4); /* argv[2..argc-3] are input files */
        const char **paths = (const char **)malloc(sizeof(char *) * path_count);
        for (size_t i = 0; i < path_count; i++) {
            paths[i] = argv[2 + i];
        }
        int rc = cmd_build(paths, path_count, argv[argc - 1]);
        free(paths);
        return rc;
    }
    fprintf(stderr, "parena: unknown command '%s'\n", argv[1]);
    return 1;
}
