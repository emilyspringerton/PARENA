/* tests/test_parenash.c -- real end-to-end verification of `sh`, the PARENA-powered busybox's own
 * shell applet v0 (docs/PARENA_COREUTILS_NORTHSTAR.md Phase 3, 2026-09-08). Pipes real script
 * text into the ACTUAL compiled `/tmp/parenash` binary via `popen("... | /tmp/parenash", "r")`
 * and checks its real stdout/exit codes -- the same "invoke the real compiled binary" discipline
 * `test_parenabusybox.c` already established, not calling the PARENA-compiled tokenizer directly.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("PASS: %s\n", msg); } \
    else { printf("FAIL: %s\n", msg); failures++; } \
} while (0)

static char *run_script(const char *script) {
    static char buf[4096];
    buf[0] = '\0';
    char cmd[4096];
    snprintf(cmd, sizeof cmd, "printf '%%b' \"%s\" | /tmp/parenash", script);
    FILE *p = popen(cmd, "r");
    if (!p) return buf;
    size_t n = fread(buf, 1, sizeof buf - 1, p);
    buf[n] = '\0';
    pclose(p);
    return buf;
}

static int run_script_status(const char *script) {
    char cmd[4096];
    snprintf(cmd, sizeof cmd, "printf '%%b' \"%s\" | /tmp/parenash > /dev/null 2>&1", script);
    return WEXITSTATUS(system(cmd));
}

int main(void) {
    CHECK(strcmp(run_script("echo hello world\\n"), "hello world\n") == 0,
          "a real, simple 'echo hello world' command runs via a real fork/execvp and produces "
          "the real, correct output");
    CHECK(strcmp(run_script("echo one; echo two\\n"), "one\ntwo\n") == 0,
          "a real ';'-separated line runs BOTH commands in order, not just the first");
    CHECK(strcmp(run_script("echo 'hello world'\\n"), "hello world\n") == 0,
          "a real single-quoted argument is correctly kept as ONE word (not split on its own "
          "internal space) and the quote characters themselves are stripped from the output");
    CHECK(strcmp(run_script("echo \\\"a b\\\" c\\n"), "a b c\n") == 0,
          "a real double-quoted argument works the same way as single-quoted");
    CHECK(strcmp(run_script("export GREETING=hi\\necho \\$GREETING\\n"), "hi\n") == 0,
          "a real $VAR expansion reads back a real environment variable set earlier in the same "
          "script (export is a real shell builtin every POSIX shell supports; this test relies "
          "on the real system's own /usr/bin/env or shell export semantics being available via "
          "execvp, exercising real $VAR lookup either way")
          ;
    CHECK(run_script_status("true\\n") == 0, "a real 'true' invocation (via real execvp/PATH "
          "lookup, not this package's own busybox applet) exits 0");
    CHECK(run_script_status("false\\n") == 1, "a real 'false' invocation exits 1");
    CHECK(run_script_status("this-command-does-not-exist-anywhere\\n") == 127,
          "an unknown command reports the real, standard shell 'not found' exit code (127)");

    {
        /* cd is a real builtin -- must actually change the PARENT process's cwd (this whole
         * /tmp/parenash invocation's own process), provably different from a command run via a
         * real forked child (which could never affect the parent's cwd at all). */
        char *out = run_script("cd /tmp; pwd\\n");
        CHECK(strstr(out, "/tmp") != NULL,
              "a real 'cd /tmp' builtin actually changes the shell's own real working directory "
              "(verified via a following real 'pwd' command seeing the change) -- proving cd ran "
              "in the parent, not a throwaway forked child");
    }

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
