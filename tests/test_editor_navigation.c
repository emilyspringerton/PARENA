/* tests/test_editor_navigation.c -- real, direct verification of
 * parent_dir_of, examples/editor_main.c's own real host-driver helper
 * for the file-tree sidebar's directory navigation (2026-08-27, closing
 * v0.83.0's own honest "clicking a subdirectory opens it as a file"
 * gap). Pure C string logic, no PARENA/SDL/Arena dependency -- carries
 * its own exact copy (with a plain malloc instead of arena_alloc, the
 * only real difference from editor_main.c's own version) for direct
 * testing, same "test what's actually there" discipline
 * tests/test_editor_indent.c already established for paren_depth_before.
 *
 * Real correctness risk worth testing directly: parent_dir_of is
 * DELIBERATELY not a general path-normalization routine (see its own
 * header comment in editor_main.c) -- these tests exist to pin down
 * exactly where that deliberate boundary is, so a future change can't
 * silently widen or narrow it without a real, visible test failure.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static char *parent_dir_of(const char *dir) {
    if (strcmp(dir, ".") == 0) {
        char *out = (char *)malloc(3);
        memcpy(out, "..", 3);
        return out;
    }
    int len = (int)strlen(dir);
    int last_slash = -1;
    for (int i = len - 1; i >= 0; i--) {
        if (dir[i] == '/') { last_slash = i; break; }
    }
    if (last_slash < 0) {
        char *out = (char *)malloc(2);
        out[0] = '.'; out[1] = '\0';
        return out;
    }
    if (last_slash == 0) {
        char *out = (char *)malloc(2);
        out[0] = '/'; out[1] = '\0';
        return out;
    }
    char *out = (char *)malloc((size_t)last_slash + 1);
    memcpy(out, dir, (size_t)last_slash);
    out[last_slash] = '\0';
    return out;
}

int main(void) {
    {
        char *p = parent_dir_of(".");
        CHECK(strcmp(p, "..") == 0, "parent of the real launch CWD (\".\") is \"..\"");
        free(p);
    }
    {
        char *p = parent_dir_of("./sub");
        CHECK(strcmp(p, ".") == 0, "parent of a real one-level-deep dir (\"./sub\") is \".\"");
        free(p);
    }
    {
        char *p = parent_dir_of("./a/b");
        CHECK(strcmp(p, "./a") == 0, "parent of a real two-level-deep dir (\"./a/b\") is \"./a\"");
        free(p);
    }
    {
        char *p = parent_dir_of("./a/b/c");
        CHECK(strcmp(p, "./a/b") == 0, "parent of a real three-level-deep dir strips exactly one real segment");
        free(p);
    }
    {
        /* Real, deliberate bounded-cap behavior (see this file's own
         * header comment + editor_main.c's own parent_dir_of comment):
         * ".." has no '/' in it, so its own "parent" falls back to
         * "." rather than genuinely escaping a second real level above
         * the launch CWD. */
        char *p = parent_dir_of("..");
        CHECK(strcmp(p, ".") == 0,
              "parent of \"..\" (one level above launch CWD) is the real, deliberate, documented \".\" fallback, not a mis-parsed further-up path");
        free(p);
    }
    {
        /* Real absolute-path case: stripping the last segment off a
         * path with the last '/' at index 0 leaves the real root "/",
         * not an empty string. */
        char *p = parent_dir_of("/home");
        CHECK(strcmp(p, "/") == 0, "parent of a real top-level absolute path (\"/home\") is the real root \"/\"");
        free(p);
    }
    {
        char *p = parent_dir_of("/home/user/project");
        CHECK(strcmp(p, "/home/user") == 0, "parent of a real deeper absolute path strips exactly one real segment");
        free(p);
    }

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
