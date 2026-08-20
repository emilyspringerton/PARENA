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

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: parena parse <file.prn>\n"
                         "       parena analyze <file.prn>                (VS0 domain 2 -- region analyzer)\n"
                         "       parena build <file.prn> -o <output.c>    (not yet implemented -- S189-13)\n");
        return 1;
    }
    if (strcmp(argv[1], "parse") == 0 && argc >= 3) {
        return cmd_parse(argv[2]);
    }
    if (strcmp(argv[1], "analyze") == 0 && argc >= 3) {
        return cmd_analyze(argv[2]);
    }
    if (strcmp(argv[1], "build") == 0) {
        fprintf(stderr, "parena: build not yet implemented -- lexer/parser/region analyzer exist, "
                         "C emitter doesn't yet (see PARENA/NORTHSTAR.md's VS0 Definition of Done, "
                         "backlog S189-13)\n");
        return 1;
    }
    fprintf(stderr, "parena: unknown command '%s'\n", argv[1]);
    return 1;
}
