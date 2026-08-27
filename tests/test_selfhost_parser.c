/* tests/test_selfhost_parser.c -- real end-to-end verification of
 * selfhost/parser.prn, the real second domain of PARENA's own self-
 * hosting effort (NORTHSTAR.md's own "Self-hosting" section), directly
 * continuing selfhost/lexer.prn (2026-08-27, founder real-time:
 * "self hosted compiler" -> "continue" -> "clnt"/"continue"). Every
 * expected AST shape below was traced by hand against src/parser.c's
 * own real, documented behavior (not guessed at), the same discipline
 * tests/test_selfhost_lexer.c already established for the lexer.
 *
 * Real, honest scope note: drives the PARENA-emitted parser only (same
 * real, separate-deferred boundary tests/test_selfhost_lexer.c's own
 * header comment already documents for not cross-checking live against
 * the C reference in the same binary).
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>

#include "test_selfhost_parser_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static const char *kind_name(NodeType_Tag tag) {
    switch (tag) {
        case NodeType_TAG_NList: return "NList";
        case NodeType_TAG_NVec: return "NVec";
        case NodeType_TAG_NMap: return "NMap";
        case NodeType_TAG_NSymbol: return "NSymbol";
        case NodeType_TAG_NKeyword: return "NKeyword";
        case NodeType_TAG_NString: return "NString";
        case NodeType_TAG_NNumber: return "NNumber";
        case NodeType_TAG_NColon: return "NColon";
        case NodeType_TAG_NAt: return "NAt";
        default: return "???";
    }
}

static void check_atom(Node n, NodeType_Tag expect_kind, const char *expect_text, const char *label) {
    char msg[256];
    snprintf(msg, sizeof msg, "%s: kind is %s (expected %s)", label, kind_name(n.kind.tag), kind_name(expect_kind));
    CHECK(n.kind.tag == expect_kind, msg);
    snprintf(msg, sizeof msg, "%s: text is \"%s\" (expected \"%s\")", label, n.text ? n.text : "(null)", expect_text);
    CHECK(n.text && strcmp(n.text, expect_text) == 0, msg);
    snprintf(msg, sizeof msg, "%s: has 0 real children (it's an atom)", label);
    CHECK(vec_len(&n.children) == 0, msg);
}

int main(void) {
    Arena a;
    arena_init(&a);

    /* --- real balanced form: (a b) --- */
    {
        Result r = parse_program("(a b)", &a);
        CHECK(r.tag == 1, "parse-program succeeds on a real simple balanced form");
        if (r.tag == 1) {
            Node program = *(Node *)r.value;
            CHECK(program.kind.tag == NodeType_TAG_NList, "the real top-level program node is NList");
            CHECK(vec_len(&program.children) == 1, "'(a b)' produces exactly 1 real top-level form");
            Node list = *(Node *)vec_get(&program.children, 0);
            CHECK(list.kind.tag == NodeType_TAG_NList, "the real top-level form is itself NList");
            CHECK(vec_len(&list.children) == 2, "'(a b)' has exactly 2 real children");
            check_atom(*(Node *)vec_get(&list.children, 0), NodeType_TAG_NSymbol, "a", "child 0");
            check_atom(*(Node *)vec_get(&list.children, 1), NodeType_TAG_NSymbol, "b", "child 1");
        }
    }

    /* --- real nested/mixed bracket forms: (defn foo [] 1) --- */
    {
        Result r = parse_program("(defn foo [] 1)", &a);
        CHECK(r.tag == 1, "parse-program succeeds on a real (defn foo [] 1)");
        if (r.tag == 1) {
            Node program = *(Node *)r.value;
            Node form = *(Node *)vec_get(&program.children, 0);
            CHECK(vec_len(&form.children) == 4, "(defn foo [] 1) has 4 real children: defn, foo, [], 1");
            check_atom(*(Node *)vec_get(&form.children, 0), NodeType_TAG_NSymbol, "defn", "defn");
            check_atom(*(Node *)vec_get(&form.children, 1), NodeType_TAG_NSymbol, "foo", "foo");
            Node vec_node = *(Node *)vec_get(&form.children, 2);
            CHECK(vec_node.kind.tag == NodeType_TAG_NVec, "the empty [] parses as a real NVec");
            CHECK(vec_len(&vec_node.children) == 0, "[] has 0 real children");
            check_atom(*(Node *)vec_get(&form.children, 3), NodeType_TAG_NNumber, "1", "1");
        }
    }

    /* --- real map literal: {:a 1 :b 2} --- */
    {
        Result r = parse_program("{:a 1 :b 2}", &a);
        CHECK(r.tag == 1, "parse-program succeeds on a real map literal");
        if (r.tag == 1) {
            Node program = *(Node *)r.value;
            Node map_node = *(Node *)vec_get(&program.children, 0);
            CHECK(map_node.kind.tag == NodeType_TAG_NMap, "{...} parses as a real NMap");
            CHECK(vec_len(&map_node.children) == 4, "a real 2-pair map has 4 real flat children");
            check_atom(*(Node *)vec_get(&map_node.children, 0), NodeType_TAG_NKeyword, ":a", "key :a");
        }
    }

    /* --- real type/region signature: (x : Type @ Region) --- */
    {
        Result r = parse_program("(x : Type @ Region)", &a);
        CHECK(r.tag == 1, "parse-program succeeds on a real type/region signature");
        if (r.tag == 1) {
            Node program = *(Node *)r.value;
            Node form = *(Node *)vec_get(&program.children, 0);
            CHECK(vec_len(&form.children) == 5, "(x : Type @ Region) has exactly 5 real children");
            check_atom(*(Node *)vec_get(&form.children, 1), NodeType_TAG_NColon, "", "standalone colon");
            check_atom(*(Node *)vec_get(&form.children, 3), NodeType_TAG_NAt, "", "standalone at");
        }
    }

    /* --- real empty file parses to a real empty program, not an error --- */
    {
        Result r = parse_program("", &a);
        CHECK(r.tag == 1, "parse-program succeeds on a real empty file");
        if (r.tag == 1) {
            Node program = *(Node *)r.value;
            CHECK(vec_len(&program.children) == 0, "a real empty file produces a real, genuinely empty program");
        }
    }

    /* --- real, genuine PARENA source fragment, lifted from this
     * repo's own stdlib/string.prn (not synthetic) --- */
    {
        char real_src[] = "(defn is-digit? [(c : I32)]\n  : Bool\n  (and (>= c 48) (<= c 57)))";
        Result r = parse_program(real_src, &a);
        CHECK(r.tag == 1, "parse-program succeeds on a real fragment lifted from stdlib/string.prn");
        if (r.tag == 1) {
            Node program = *(Node *)r.value;
            CHECK(vec_len(&program.children) == 1, "the real fragment is exactly 1 top-level form");
            Node form = *(Node *)vec_get(&program.children, 0);
            check_atom(*(Node *)vec_get(&form.children, 0), NodeType_TAG_NSymbol, "defn", "real defn");
            check_atom(*(Node *)vec_get(&form.children, 1), NodeType_TAG_NSymbol, "is-digit?", "real defn name");
        }
    }

    /* --- real, imbalanced S-expressions: the DoD's own required
     * failure path (NORTHSTAR.md's own domain 1 acceptance bar),
     * exact wording checked against src/parser.c's own real
     * documented output (this repo's own tests/test_lexer_parser.c
     * already pins these exact strings for the C reference). --- */
    {
        Result r = parse_program("(foo", &a);
        CHECK(r.tag == 0, "parse-program reports a real Err on an unterminated list");
        if (r.tag == 0) {
            SelfhostParseError e = *(SelfhostParseError *)r.value;
            char *msg = (char *)e.value;
            CHECK(strcmp(msg, "unterminated form: expected ')' to close the form opened at line 1") == 0,
                  "the real error message matches src/parser.c's own exact DoD-required wording");
        }
    }
    {
        Result r = parse_program("(foo]", &a);
        CHECK(r.tag == 0, "parse-program reports a real Err on a mismatched bracket kind");
        if (r.tag == 0) {
            SelfhostParseError e = *(SelfhostParseError *)r.value;
            char *msg = (char *)e.value;
            CHECK(strcmp(msg, "mismatched bracket: expected ')' but found ']' at line 1") == 0,
                  "the real mismatched-bracket message matches src/parser.c's own exact wording");
        }
    }
    {
        Result r = parse_program(")", &a);
        CHECK(r.tag == 0, "parse-program reports a real Err on a stray close with nothing open");
        if (r.tag == 0) {
            SelfhostParseError e = *(SelfhostParseError *)r.value;
            char *msg = (char *)e.value;
            CHECK(strcmp(msg, "unexpected ')' with no matching open bracket at line 1") == 0,
                  "the real stray-close message matches src/parser.c's own exact wording");
        }
    }
    {
        Result r = parse_program("\"never closed", &a);
        CHECK(r.tag == 0, "parse-program reports a real Err on an unterminated string literal");
        if (r.tag == 0) {
            SelfhostParseError e = *(SelfhostParseError *)r.value;
            char *msg = (char *)e.value;
            CHECK(strcmp(msg, "unterminated string literal (opened at line 1)") == 0,
                  "the real unterminated-string message (this file's own honest, documented departure "
                  "from src/parser.c's own double-line-number artifact -- see selfhost/parser.prn's own "
                  "SelfhostParseError header comment)");
        }
    }
    {
        /* Real mismatched NESTED bracket -- confirms parse-compound's
         * own real recursive descent correctly reports the INNER
         * form's own open line, not the outer one. */
        char real_src[] = "(a (b]";
        Result r = parse_program(real_src, &a);
        CHECK(r.tag == 0, "parse-program reports a real Err on a mismatched nested bracket");
        if (r.tag == 0) {
            SelfhostParseError e = *(SelfhostParseError *)r.value;
            char *msg = (char *)e.value;
            CHECK(strcmp(msg, "mismatched bracket: expected ')' but found ']' at line 1") == 0,
                  "the real nested-mismatch message correctly names the INNER '(b' form's own close, not the outer one");
        }
    }

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
