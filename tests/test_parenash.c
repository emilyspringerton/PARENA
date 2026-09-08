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

    /* --- real if/then/else/fi conditionals (2026-09-08, Phase 3b) --- */
    CHECK(strcmp(run_script("if true; then echo yes; fi\\n"), "yes\n") == 0,
          "a real 'if true; then ...; fi' runs the then-branch");
    CHECK(strcmp(run_script("if false; then echo yes; fi\\n"), "") == 0,
          "a real 'if false; then ...; fi' with no else runs NOTHING (real, live-found bug fixed "
          "here: a trailing ';' in the condition used to silently discard the real exit status "
          "and always take the then-branch regardless)");
    CHECK(strcmp(run_script("if false; then echo yes; else echo no; fi\\n"), "no\n") == 0,
          "a real 'if false; then ...; else ...; fi' correctly runs the else-branch");
    CHECK(strcmp(run_script("if false; then echo yes; fi; echo after\\n"), "after\n") == 0,
          "a real command AFTER a real 'fi' still runs, and the skipped then-branch produces no "
          "output at all");
    CHECK(strcmp(run_script("if true; then echo a; echo b; fi\\n"), "a\nb\n") == 0,
          "a real then-branch containing MULTIPLE ';'-separated commands runs all of them");

    /* --- real elif support (2026-09-08, Phase 3b continued) --- */
    CHECK(strcmp(run_script("if false; then echo a; elif true; then echo b; else echo c; fi\\n"), "b\n") == 0,
          "a real 'elif' clause runs when the first condition is false and the elif condition is true");
    CHECK(strcmp(run_script("if true; then echo a; elif true; then echo b; else echo c; fi\\n"), "a\n") == 0,
          "the FIRST true condition wins -- a later (also-true) elif never runs");
    CHECK(strcmp(run_script("if false; then echo a; elif false; then echo b; else echo c; fi\\n"), "c\n") == 0,
          "falls through to a real 'else' when every if/elif condition is false");
    CHECK(strcmp(run_script("if false; then echo a; elif false; then echo b; elif true; then echo c; else echo d; fi\\n"), "c\n") == 0,
          "a real, CHAINED multiple-elif structure resolves to the first true condition among them");
    CHECK(strcmp(run_script("if false; then echo a; elif true; then echo b; fi; echo done\\n"), "b\ndone\n") == 0,
          "a real command after a real elif-chain's own 'fi' still runs");

    /* --- real ${VAR:-default}/${VAR-default} parameter expansion (2026-09-08) --- */
    CHECK(strcmp(run_script("echo \\${NOPE:-fallback}\\n"), "fallback\n") == 0,
          "a real '${VAR:-default}' expands to the default when the variable is unset");
    CHECK(strcmp(run_script("export SET=real\\necho \\${SET:-fallback}\\n"), "real\n") == 0,
          "a real '${VAR:-default}' expands to the REAL value when the variable IS set");
    CHECK(strcmp(run_script("echo \\${NOPE-fallback2}\\n"), "fallback2\n") == 0,
          "the plain '${VAR-default}' form (no colon) also expands correctly");
    CHECK(strcmp(run_script("export PLAIN=hi\\necho \\${PLAIN}\\n"), "hi\n") == 0,
          "a plain '${VAR}' brace form with no default operator also expands correctly");

    /* --- real multi-line statement support (2026-09-08, Phase 3c) --- */
    CHECK(strcmp(run_script("if true\\nthen\\necho yes\\nfi\\n"), "yes\n") == 0,
          "a real MULTI-LINE 'if/then/fi' (the real, standard form actual shell scripts use, "
          "spanning several physical lines rather than one ';'-joined line) now runs correctly "
          "(real, live-found architectural gap fixed this pass: this shell used to execute one "
          "physical line at a time, producing nonsensical 'not found' errors for exactly this "
          "real, common shape)");
    CHECK(strcmp(run_script("if false\\nthen\\necho yes\\nelse\\necho no\\nfi\\n"), "no\n") == 0,
          "a real multi-line if/then/else/fi also runs correctly");

    /* --- real shell functions: name() { ... } (2026-09-08, Phase 3c) --- */
    CHECK(strcmp(run_script("greet() {\\necho hello\\necho world\\n}\\ngreet\\n"), "hello\nworld\n") == 0,
          "a real, multi-line function definition followed by a real call to it runs the ENTIRE "
          "real function body (both lines), not just a stub");
    CHECK(strcmp(run_script("greet() { echo hi; }\\ngreet\\n"), "hi\n") == 0,
          "a real SINGLE-LINE function definition also works");
    {
        char *out = run_script("goto_tmp() {\\ncd /tmp\\n}\\ngoto_tmp\\npwd\\n");
        CHECK(strstr(out, "/tmp") != NULL,
              "a real function body containing a real 'cd' builtin actually changes the CALLING "
              "shell's own real working directory (proving the function ran in-process, not a "
              "forked child, the same real reason cd/export are builtins at all)");
    }
    CHECK(strcmp(run_script("check() {\\nif true\\nthen\\necho yes-from-fn\\nfi\\n}\\ncheck\\n"), "yes-from-fn\n") == 0,
          "a real function body containing a real multi-line 'if' conditional runs correctly -- "
          "function bodies get every other real shell feature this shell has, for free, since "
          "they execute through the exact same exec_range every other construct uses");

    /* --- real bare NAME=value assignment, no 'export' keyword (2026-09-08, Phase 3c) --- */
    CHECK(run_script_status("description=\\\"a real assignment\\\"\\n") == 0,
          "a real, bare 'NAME=value' statement (real, live-found gap: this session's own audited "
          "/etc/init.d/hostname script's very first real line is exactly this shape) is now "
          "recognized as an assignment, not misreported as an unknown command");
    CHECK(strcmp(run_script("GREETING=hi\\necho \\$GREETING\\n"), "hi\n") == 0,
          "a real bare assignment's own value is genuinely readable back via \\$VAR expansion "
          "afterward, proving it's a real assignment and not just a silently-ignored no-op");

    /* --- real comment support (2026-09-08, Phase 3d) --- */
    CHECK(strcmp(run_script("# a real comment line\\necho after-comment\\n"), "after-comment\n") == 0,
          "a real '#'-comment line (real, live-found gap: EVERY real script starts lines with "
          "these, and they used to be misreported as bogus '#: not found' commands) is now "
          "correctly skipped to end of line");
    CHECK(strcmp(run_script("echo hi # trailing comment\\n"), "hi\n") == 0,
          "a real trailing '#' comment after a genuine command also works");
    CHECK(strcmp(run_script("echo foo#bar\\n"), "foo#bar\n") == 0,
          "a real '#' that is NOT at the start of a word (mid-word, e.g. 'foo#bar') is correctly "
          "NOT treated as a comment -- real shell comments only start at a word boundary");

    /* --- real source/. builtin (2026-09-08, Phase 3d) --- */
    {
        FILE *f = fopen("/tmp/test_parenash_source.sh", "w");
        fputs("greet() {\n\techo hello from sourced function\n}\nexport SOURCED_VAR=yes\n", f);
        fclose(f);
        CHECK(strcmp(run_script("source /tmp/test_parenash_source.sh\\necho \\$SOURCED_VAR\\ngreet\\n"),
                     "yes\nhello from sourced function\n") == 0,
              "a real 'source' builtin reads a real file, and both its real assignment AND its "
              "real function definition persist in the CALLING shell afterward");
        CHECK(strcmp(run_script(". /tmp/test_parenash_source.sh\\ngreet\\n"), "hello from sourced function\n") == 0,
              "the plain '.' form of the builtin works identically to 'source'");
        remove("/tmp/test_parenash_source.sh");
    }
    CHECK(run_script_status("source /no/such/file.sh\\n") == 1,
          "sourcing a genuinely nonexistent file reports a real, honest failure, not a crash");

    /* --- real brace-on-its-own-line function definitions (2026-09-08, Phase 3d) --- */
    CHECK(strcmp(run_script("greet()\\n{\\necho hi-brace-own-line\\n}\\ngreet\\n"), "hi-brace-own-line\n") == 0,
          "a real function definition with the opening '{' on its OWN line (real, live-found "
          "gap: OpenRC's own real functions.sh is written exactly this way, and it used to make "
          "the REPL execute the bare 'NAME()' header as a bogus command one statement too early) "
          "now defines and runs correctly");

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
