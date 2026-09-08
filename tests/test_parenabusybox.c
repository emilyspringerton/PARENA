/* tests/test_parenabusybox.c -- real end-to-end verification of the PARENA-powered busybox v0
 * (docs/PARENA_COREUTILS_NORTHSTAR.md, 2026-09-08). Builds nothing itself -- the Makefile's own
 * `test-parenabusybox` target builds the real `/tmp/parenabusybox` binary first, including
 * real symlinks for each applet name, matching busybox's own real dual-invocation convention
 * (direct-name symlink vs. `parenabusybox <applet>`). This test invokes the REAL compiled binary
 * via `popen`/`system` and checks its REAL stdout/exit codes -- not calling the PARENA-compiled
 * functions directly (those are exercised structurally by the other selfhost-style tests
 * throughout this repo; this one is specifically about the multi-call dispatch mechanism itself
 * working end to end, the same real thing a real `/bin/echo -> busybox` symlink proves on a real
 * system).
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

static char *run_capture(const char *cmd) {
    static char buf[4096];
    buf[0] = '\0';
    FILE *p = popen(cmd, "r");
    if (!p) return buf;
    size_t n = fread(buf, 1, sizeof buf - 1, p);
    buf[n] = '\0';
    pclose(p);
    return buf;
}

int main(void) {
    /* --- direct invocation: parenabusybox <applet> [args...] --- */
    CHECK(strcmp(run_capture("/tmp/parenabusybox echo hello world"), "hello world\n") == 0,
          "a real 'parenabusybox echo hello world' invocation joins args with a space and a "
          "trailing newline");
    CHECK(strcmp(run_capture("/tmp/parenabusybox echo -n hello"), "hello") == 0,
          "a real '-n' flag suppresses the trailing newline");
    CHECK(strcmp(run_capture("/tmp/parenabusybox basename /usr/local/bin/foo"), "foo\n") == 0,
          "a real basename invocation strips the directory prefix");
    CHECK(strcmp(run_capture("/tmp/parenabusybox basename /usr/local/bin/foo.tar .tar"), "foo\n") == 0,
          "a real basename invocation with a suffix arg strips both the directory prefix AND the suffix");
    CHECK(strcmp(run_capture("/tmp/parenabusybox basename foo//"), "foo\n") == 0,
          "a real basename invocation correctly strips trailing slashes first");
    {
        char *pwd_out = run_capture("cd /tmp && /tmp/parenabusybox pwd");
        CHECK(strstr(pwd_out, "/tmp") != NULL,
              "a real pwd invocation reports the real, actual current directory (via a real "
              "getcwd(3) call, not a hardcoded/fake value)");
    }
    CHECK(system("/tmp/parenabusybox true") == 0,
          "a real 'parenabusybox true' invocation exits 0");
    CHECK(WEXITSTATUS(system("/tmp/parenabusybox false")) == 1,
          "a real 'parenabusybox false' invocation exits 1");
    CHECK(WEXITSTATUS(system("/tmp/parenabusybox nonexistent-applet 2>/dev/null")) == 127,
          "an unknown applet name reports the real, standard shell 'command not found' exit code "
          "(127), not a crash or a silent 0");

    /* --- Phase 1 applets (docs/PARENA_COREUTILS_NORTHSTAR.md): wc/head/yes/cat/sleep/env --- */
    CHECK(strcmp(run_capture("printf 'a\\nb\\nc\\n' | /tmp/parenabusybox wc"), "3\n") == 0,
          "a real 'wc' invocation over stdin counts real newline bytes");
    {
        FILE *f = fopen("/tmp/parenabusybox_wc_test.txt", "w");
        fputs("x\ny\n", f);
        fclose(f);
        CHECK(strcmp(run_capture("/tmp/parenabusybox wc /tmp/parenabusybox_wc_test.txt"),
                     "2 /tmp/parenabusybox_wc_test.txt\n") == 0,
              "a real 'wc <file>' invocation prints the count AND the real filename");
    }
    CHECK(strcmp(run_capture("seq 1 15 | /tmp/parenabusybox head"),
                 "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n") == 0,
          "a real 'head' invocation with no -n defaults to the real, standard 10 lines");
    CHECK(strcmp(run_capture("seq 1 15 | /tmp/parenabusybox head -n 3"), "1\n2\n3\n") == 0,
          "a real 'head -n 3' invocation stops after exactly 3 lines");
    CHECK(strcmp(run_capture("/tmp/parenabusybox yes | head -n 3"), "y\ny\ny\n") == 0,
          "a real 'yes' invocation with no args repeats the real, standard default line 'y' "
          "forever, until the downstream pipe closes (SIGPIPE)");
    CHECK(strcmp(run_capture("/tmp/parenabusybox yes hi there | head -n 2"), "hi there\nhi there\n") == 0,
          "a real 'yes hi there' invocation repeats its own space-joined argument text");
    CHECK(strcmp(run_capture("printf 'cat-check\\n' | /tmp/parenabusybox cat"), "cat-check\n") == 0,
          "a real 'cat' invocation with no file args passes stdin straight through");
    CHECK(strcmp(run_capture("/tmp/parenabusybox cat /tmp/parenabusybox_wc_test.txt"), "x\ny\n") == 0,
          "a real 'cat <file>' invocation prints the real file's real content");
    CHECK(system("/tmp/parenabusybox sleep 0") == 0,
          "a real 'sleep 0' invocation returns real exit code 0 (not testing real timing here, "
          "just that the applet dispatches and exits cleanly)");
    {
        char *env_out = run_capture("/tmp/parenabusybox env");
        CHECK(strstr(env_out, "=") != NULL,
              "a real 'env' invocation prints real, actual environment variables (KEY=VALUE "
              "shaped), not an empty or fake list");
    }
    remove("/tmp/parenabusybox_wc_test.txt");

    /* --- real multi-call dispatch: invoked THROUGH a real symlink named after the applet,
     * exactly like a real /bin/echo -> busybox symlink on a real system, not just via the
     * 'parenabusybox <applet>' form above. This is the real thing busybox's own multi-call
     * mechanism is actually FOR. --- */
    CHECK(strcmp(run_capture("/tmp/echo hi there"), "hi there\n") == 0,
          "invoking the real 'echo' SYMLINK directly (argv[0] basename dispatch, not the "
          "'parenabusybox <applet>' form) produces the identical, correct real output");
    CHECK(strcmp(run_capture("/tmp/basename /a/b/c"), "c\n") == 0,
          "invoking the real 'basename' SYMLINK directly also dispatches correctly");
    CHECK(system("/tmp/true") == 0,
          "invoking the real 'true' SYMLINK directly exits 0");

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
