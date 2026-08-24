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
#include "fmt.h"
#include "parser.h"
#include "region.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    const char *emit_err = NULL;
    const char *c_source = emit_c(&arena, program, &emit_err);
    if (!c_source) {
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
    fputs(c_source, out);
    fclose(out);
    if (path_count == 1) {
        printf("parena: %s -> %s\n", paths[0], out_path);
    } else {
        printf("parena: [%zu files] -> %s\n", path_count, out_path);
    }
    arena_free_all(&arena);
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
                         "       parena build <file.prn> [file2.prn ...] -o <output.c>    (VS0 domain 3 -- C emitter; "
                         "multiple files are combined into one compilation unit, in the order given)\n"
                         "       parena fmt [-w] <file.prn> [file2.prn ...]         (re-indent; -w writes in place, "
                         "default prints to stdout)\n"
#ifdef PARENA_HAS_CI_STATUS
                         "       parena ci-status <owner/repo> <sha>                (GITHUB_TOKEN env var required; "
                         "exit 0=all green, 1=pending, 2=failed, 3=not found/error)\n"
#endif
        );
        return 1;
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
            fprintf(stderr, "usage: parena build <file.prn> [file2.prn ...] -o <output.c>\n");
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
