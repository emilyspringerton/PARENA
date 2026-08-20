/* test_lexer_parser.c — DoD domain 1's own verification method: "Unit
 * tests on balanced and imbalanced S-expressions." Plain PASS/FAIL
 * assertions to stdout, same convention this monorepo's other C test
 * suites (e.g. REDGARDEN's test_arena.sh output) already use.
 */
#include "../src/arena.h"
#include "../src/ast.h"
#include "../src/parser.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; printf("PASS: %s\n", msg); } \
    else { g_fail++; printf("FAIL: %s\n", msg); } \
} while (0)

/* parse_ok_arena: like parse_ok, but hands the caller the Arena too so it
 * can inspect the resulting tree (whose nodes live IN that arena) before
 * freeing it themselves -- avoids ASan flagging every successful-parse
 * test as a leak, which a real "unfreed until process exit" test binary
 * would otherwise do harmlessly but noisily. */
static Node *parse_ok_arena(Arena *arena, const char *src, const char *msg) {
    arena_init(arena);
    const char *err = NULL;
    Node *n = parse_program(arena, src, strlen(src), &err);
    CHECK(n != NULL && err == NULL, msg);
    return n;
}

static void expect_parse_error(const char *src, const char *msg) {
    Arena arena;
    arena_init(&arena);
    const char *err = NULL;
    Node *n = parse_program(&arena, src, strlen(src), &err);
    CHECK(n == NULL && err != NULL, msg);
    if (n == NULL && err != NULL) {
        printf("       (error message: %s)\n", err);
    }
    arena_free_all(&arena);
}

int main(void) {
    /* --- balanced S-expressions --- */
    {
        Arena arena;
        Node *n = parse_ok_arena(&arena, "(defn foo [] 1)", "simple balanced list with a vector child parses");
        if (n) {
            CHECK(n->child_count == 1, "top-level program has exactly one form");
            Node *defn = n->children[0];
            CHECK(defn->type == NODE_LIST, "the one form is a list");
            CHECK(defn->child_count == 4, "(defn foo [] 1) has 4 children: defn, foo, [], 1");
            CHECK(defn->children[0]->type == NODE_SYMBOL && strcmp(defn->children[0]->text, "defn") == 0,
                  "first child is the symbol 'defn'");
            CHECK(defn->children[2]->type == NODE_VEC && defn->children[2]->child_count == 0,
                  "third child is an empty vector []");
        }
        arena_free_all(&arena);
    }
    {
        Arena arena;
        Node *n = parse_ok_arena(&arena,
            "(defn load-config [(buf-arena : Arena @ :region/buffer)]\n"
            "  (with-arena [scratch :region/scratch 1024]\n"
            "    (let [temp-str (alloc scratch String \"config.json\")]\n"
            "      temp-str)))",
            "real VS0 test.prn-style scratch-to-buffer promotion example parses");
        (void)n;
        arena_free_all(&arena);
    }
    {
        Arena arena;
        Node *n = parse_ok_arena(&arena, "{:field1 val1 :field2 val2}", "map literal parses");
        if (n) {
            Node *m = n->children[0];
            CHECK(m->type == NODE_MAP, "top form is a map");
            CHECK(m->child_count == 4, "map has 4 flat children (2 keyword/value pairs)");
            CHECK(m->children[0]->type == NODE_KEYWORD && strcmp(m->children[0]->text, ":field1") == 0,
                  "first map child is the keyword :field1");
        }
        arena_free_all(&arena);
    }
    {
        Arena arena;
        Node *n = parse_ok_arena(&arena, "!file &var &mut", "bang/amp-prefixed symbols (linear/borrow forms) parse as symbols");
        if (n) {
            CHECK(n->children[0]->type == NODE_SYMBOL && strcmp(n->children[0]->text, "!file") == 0,
                  "!file is one SYMBOL token, not a separate operator + symbol");
            CHECK(n->children[1]->type == NODE_SYMBOL && strcmp(n->children[1]->text, "&var") == 0,
                  "&var is one SYMBOL token");
            CHECK(n->children[2]->type == NODE_SYMBOL && strcmp(n->children[2]->text, "&mut") == 0,
                  "&mut is one SYMBOL token");
        }
        arena_free_all(&arena);
    }
    {
        Arena arena;
        Node *n = parse_ok_arena(&arena, "; a comment\n(foo 1) ;; trailing comment", "line comments are skipped, both ; and ;; forms");
        if (n) CHECK(n->child_count == 1, "comment-only line contributes no form");
        arena_free_all(&arena);
    }
    {
        Arena arena;
        Node *n = parse_ok_arena(&arena, "(x : Type @ :region/scratch)", "standalone ':' and '@' tokens parse distinctly from keywords");
        if (n) {
            Node *form = n->children[0];
            CHECK(form->children[1]->type == NODE_COLON, "standalone ':' parses as NODE_COLON, not a keyword");
            CHECK(form->children[3]->type == NODE_AT, "'@' parses as NODE_AT");
            CHECK(form->children[4]->type == NODE_KEYWORD && strcmp(form->children[4]->text, ":region/scratch") == 0,
                  ":region/scratch (no space after ':') parses as one KEYWORD token");
        }
        arena_free_all(&arena);
    }
    {
        Arena arena;
        Node *n = parse_ok_arena(&arena, "-42 3.5 100", "negative integers and decimals parse as numbers");
        if (n) {
            CHECK(n->children[0]->type == NODE_NUMBER && strcmp(n->children[0]->text, "-42") == 0, "-42 parses as a number");
            CHECK(n->children[1]->type == NODE_NUMBER && strcmp(n->children[1]->text, "3.5") == 0, "3.5 parses as a number");
        }
        arena_free_all(&arena);
    }
    {
        Arena arena;
        Node *n = parse_ok_arena(&arena, "\"hello\\nworld\"", "string with an escape sequence parses");
        if (n) CHECK(n->children[0]->type == NODE_STRING && n->children[0]->text_len == 11,
                     "escaped \\n counts as one character in the decoded string");
        arena_free_all(&arena);
    }
    {
        Arena arena;
        parse_ok_arena(&arena, "", "an empty file parses to an empty program, not an error");
        arena_free_all(&arena);
    }

    /* --- imbalanced / malformed S-expressions (DoD's own required negative case) --- */
    expect_parse_error("(defn foo [] 1", "unterminated list (missing final paren) is a parse error");
    expect_parse_error("(foo (bar]", "mismatched bracket kind (opened with (, closed with ]) is a parse error");
    expect_parse_error(")", "a stray closing paren with nothing open is a parse error");
    expect_parse_error("(foo \"unterminated string", "unterminated string literal is a parse error");
    expect_parse_error("[1 2 (3 4]", "mismatched nested bracket is a parse error");

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
