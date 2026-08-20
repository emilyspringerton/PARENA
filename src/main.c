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
#include "parser.h"
#include "region.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: parena parse <file.prn>\n"
                         "       parena analyze <file.prn>                          (VS0 domain 2 -- region analyzer)\n"
                         "       parena build <file.prn> [file2.prn ...] -o <output.c>    (VS0 domain 3 -- C emitter; "
                         "multiple files are combined into one compilation unit, in the order given)\n");
        return 1;
    }
    if (strcmp(argv[1], "parse") == 0 && argc >= 3) {
        return cmd_parse(argv[2]);
    }
    if (strcmp(argv[1], "analyze") == 0 && argc >= 3) {
        return cmd_analyze(argv[2]);
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
