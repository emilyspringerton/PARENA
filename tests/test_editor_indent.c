/* tests/test_editor_indent.c -- real, direct verification of
 * paren_depth_before, examples/editor_main.c's own real auto-indent-
 * on-Enter bracket-nesting counter (2026-08-27, founder real-time,
 * actively using the editor: "auto 2 space indent doesnt work when you
 * hit enter and you are in a parena... if you are in parens and hit
 * enter it should indendt you"). Pure C string logic, no PARENA/SDL
 * dependency at all -- paren_depth_before is `static`, private to
 * editor_main.c, so this carries its own exact copy for direct
 * testing, same real "test what's actually there" discipline
 * tests/test_editor_undo.c already established for that file's own
 * push_undo/pop_undo. Real correctness risk worth testing directly:
 * the comment/string-skipping logic is easy to get subtly wrong (an
 * off-by-one in the escaped-quote skip, or counting a paren that's
 * really inside a `;;` comment or a "..." string literal, would
 * silently mis-indent real PARENA source).
 */
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static int paren_depth_before(const char *text, int pos) {
    int depth = 0;
    int i = 0;
    while (i < pos) {
        if (text[i] == ';' && i + 1 < pos && text[i + 1] == ';') {
            while (i < pos && text[i] != '\n') i++;
            continue;
        }
        if (text[i] == '"') {
            i++;
            while (i < pos && text[i] != '"') {
                if (text[i] == '\\' && i + 1 < pos) i++;
                i++;
            }
            if (i < pos) i++;
            continue;
        }
        if (text[i] == '(' || text[i] == '[' || text[i] == '{') {
            depth++;
        } else if (text[i] == ')' || text[i] == ']' || text[i] == '}') {
            if (depth > 0) depth--;
        }
        i++;
    }
    return depth;
}

int main(void) {
    CHECK(paren_depth_before("", 0) == 0, "an empty real buffer has real depth 0");

    {
        const char *t = "(a (b (c";
        CHECK(paren_depth_before(t, (int)strlen(t)) == 3, "3 real unclosed opens gives real depth 3");
    }
    {
        const char *t = "(a (b) (c";
        CHECK(paren_depth_before(t, (int)strlen(t)) == 2,
              "a real closed pair in the middle doesn't count -- only the 2 real still-open parens do");
    }
    {
        const char *t = ")))";
        CHECK(paren_depth_before(t, (int)strlen(t)) == 0,
              "real extra closes never drive depth negative, clamped at 0");
    }
    {
        const char *t = "([{";
        CHECK(paren_depth_before(t, (int)strlen(t)) == 3, "real (/[/{ all count toward the same real depth uniformly");
    }
    {
        const char *t = "([{}])";
        CHECK(paren_depth_before(t, (int)strlen(t)) == 0, "real balanced mixed-bracket-type nesting closes back to 0");
    }
    {
        const char *t = ";; a real comment with a (fake paren\n(real";
        CHECK(paren_depth_before(t, (int)strlen(t)) == 1,
              "a real paren INSIDE a ;; comment doesn't count -- only the real one after it does");
    }
    {
        const char *t = "(f \"a real (fake) paren in a string\" (real";
        CHECK(paren_depth_before(t, (int)strlen(t)) == 2,
              "real parens INSIDE a \"...\" string literal don't count -- only the 2 real ones outside do");
    }
    {
        const char *t = "(f \"a real \\\"(fake)\\\" escaped-quote string\" (real";
        CHECK(paren_depth_before(t, (int)strlen(t)) == 2,
              "a real escaped quote inside a string doesn't end it early -- the real (fake) paren still stays inside");
    }
    {
        const char *t = "(a)(b";
        CHECK(paren_depth_before(t, 3) == 0,
              "the real `pos` boundary is honestly respected -- depth at position 3 (right after \"(a)\") is 0, not counting the real open paren at position 3 onward");
        CHECK(paren_depth_before(t, (int)strlen(t)) == 1,
              "the same real buffer's full depth (past that boundary) is 1");
    }
    {
        /* A real, genuine PARENA source fragment, lifted from this
         * repo's own stdlib/editor/buffer.prn (real, not synthetic). */
        const char *t = "(defn move-cursor-left [(!buf : &Buffer)] : Buffer\n"
                         "  (let [pos (get-field !buf :cursor)\n"
                         "        text (get-field !buf :text)]\n";
        CHECK(paren_depth_before(t, (int)strlen(t)) == 2,
              "a real, genuine PARENA source fragment (lifted from buffer.prn) reports the real, correct depth 2");
    }

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
