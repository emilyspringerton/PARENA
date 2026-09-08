/* tools/parenash_host.c -- real host-side entry point for `sh`, the PARENA-powered busybox's
 * shell applet (docs/PARENA_COREUTILS_NORTHSTAR.md Phase 3/3b, 2026-09-08, founder real-time:
 * "zsh etc build it prn" -> "continue"). Same real "PARENA logic + C host driver" split every
 * applet in this package already uses -- real tokenizing/quoting/`;`-splitting/`$VAR`-lookup
 * logic lives in stdlib/coreutils/sh.prn; this file does real PROCESS MANAGEMENT (the REPL loop,
 * fork/execvp/waitpid, `cd`/`export`/`exit` builtins) AND real control-flow structure
 * (`if`/`then`/`else`/`fi`) over the already-tokenized word list -- a deliberate choice to keep
 * PARENA's own side focused on string/token processing (its own real strength) rather than
 * stretching it into imperative control-flow logic that's more naturally a plain recursive-
 * descent walk in C, the same language every other part of this process-management layer is in.
 *
 * Real, honest v0 scope, named directly (not silently oversold): sequential simple commands
 * separated by `;`, single-level `if COND; then ...; [else ...;] fi` (no `elif`, no nesting --
 * a real, separate, later extension of the same `exec_range` recursion below), `$VAR` expansion,
 * `cd`/`export`/`exit` builtins. NO pipes, NO redirection, NO functions, NO `${var:-default}`
 * parameter expansion, NO job control/backgrounding. `test`/`[` already work today via the plain
 * `execvp` fallback (real system binaries, not builtins) -- confirmed live, not assumed. This
 * does not run real OpenRC init scripts yet -- confirmed via a real audit of this session's own
 * built EmilyOS rootfs (`/etc/init.d/hostname`/`bootmisc` also use real shell FUNCTIONS and
 * `${var:-default}` expansion, both still genuinely beyond this pass) -- real, separate, later
 * phases, not attempted here.
 *
 * Interactive REPL when stdin is a tty (prints a real "$ " prompt); reads and executes lines from
 * stdin either way (a real, minimal script-execution mode too, e.g. `parenash < script.sh`).
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

/* find_keyword -- real, plain linear scan for a literal token (";", "if", "then", "else", "fi")
 * at or after `start`, stopping before `end`. No nesting awareness (the first "then"/"fi" found
 * closes the nearest-enclosing `if` in this v0 -- correct for the real, common non-nested case,
 * a real, honest limitation for a nested `if` inside another `if`'s own condition/branch). */
static int find_keyword(char **words, int start, int end, const char *kw) {
    for (int i = start; i < end; i++) {
        if (strcmp(words[i], kw) == 0) return i;
    }
    return -1;
}

/* exec_simple -- runs ONE real simple command (words[start..end), already `$VAR`-expanded) via
 * a builtin or a real fork/execvp. Builtins run in THIS process (the parent) deliberately -- a
 * forked child could never affect the shell's own cwd/environment, the same real reason every
 * actual shell handles them this way. */
static int exec_simple(char **words, int start, int end, Arena *expand_arena) {
    int seg_len = end - start;
    if (seg_len <= 0) return 0;
    char *argv[64];
    int argc = seg_len < 63 ? seg_len : 63;
    for (int k = 0; k < argc; k++) {
        argv[k] = expand_word(words[start + k], expand_arena);
    }
    argv[argc] = NULL;

    if (strcmp(argv[0], "cd") == 0) {
        const char *target = argc >= 2 ? argv[1] : coreutils_getenv_impl("HOME");
        if (chdir(target) != 0) {
            fprintf(stderr, "sh: cd: %s: No such file or directory\n", target);
            return 1;
        }
        return 0;
    }
    if (strcmp(argv[0], "export") == 0) {
        /* Real, minimal `export NAME=value` builtin. Real, honest v0: only the `NAME=value`
         * form -- this shell has no separate "shell variable vs. environment variable"
         * distinction, everything is already a real environment variable via setenv. */
        if (argc >= 2) {
            char *eq = strchr(argv[1], '=');
            if (eq) {
                *eq = '\0';
                setenv(argv[1], eq + 1, 1);
            }
        }
        return 0;
    }
    if (strcmp(argv[0], "exit") == 0) {
        int code = argc >= 2 ? atoi(argv[1]) : 0;
        exit(code);
    }

    pid_t pid = fork();
    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "sh: %s: not found\n", argv[0]);
        _exit(127);
    } else if (pid > 0) {
        int status = 0;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }
    fprintf(stderr, "sh: fork failed\n");
    return 1;
}

static int exec_range(char **words, int start, int end, Arena *expand_arena);

/* exec_if_chain -- real, recursive-descent handling of `if`/`elif` (words[start] is one of the
 * two, both treated identically here since real POSIX `elif` is exactly "the same shape as `if`,
 * chained"). Finds the nearest boundary keyword among `elif`/`else`/`fi` (whichever comes first)
 * to know where the current branch ends: a real `elif` boundary recurses right back into this
 * same function treating the `elif` word as a fresh chain-start (it shares the SAME final `fi`
 * as the outer `if`, so no separate fi-search is needed there); `else`/`fi` are the real, simple
 * terminal cases already established. Real, honest v0 boundary: no nesting (the first
 * `elif`/`else`/`fi` found closes the NEAREST enclosing `if`/`elif`, not a real one this v0
 * tracks depth for). */
static int exec_if_chain(char **words, int start, int end, Arena *expand_arena) {
    int then_idx = find_keyword(words, start + 1, end, "then");
    if (then_idx < 0) {
        fprintf(stderr, "sh: syntax error: expected 'then'\n");
        return 2;
    }
    int fi_idx = find_keyword(words, then_idx + 1, end, "fi");
    if (fi_idx < 0) {
        fprintf(stderr, "sh: syntax error: expected 'fi'\n");
        return 2;
    }
    int elif_idx = find_keyword(words, then_idx + 1, fi_idx, "elif");
    int else_idx = find_keyword(words, then_idx + 1, fi_idx, "else");
    int branch_end = fi_idx;
    int is_elif = 0;
    if (else_idx >= 0 && else_idx < branch_end) branch_end = else_idx;
    if (elif_idx >= 0 && elif_idx < branch_end) { branch_end = elif_idx; is_elif = 1; }

    int cond_status = exec_range(words, start + 1, then_idx, expand_arena);
    if (cond_status == 0) {
        int status = exec_range(words, then_idx + 1, branch_end, expand_arena);
        if (fi_idx + 1 < end) return exec_range(words, fi_idx + 1, end, expand_arena);
        return status;
    }
    if (is_elif) {
        /* The recursive call shares this same outer `fi` and handles "anything after it" too --
         * nothing further to do here. */
        return exec_if_chain(words, branch_end, end, expand_arena);
    }
    int status = (else_idx >= 0) ? exec_range(words, else_idx + 1, fi_idx, expand_arena) : 0;
    if (fi_idx + 1 < end) return exec_range(words, fi_idx + 1, end, expand_arena);
    return status;
}

/* exec_range -- real, recursive-descent walk over words[start..end): recognizes a real
 * `if COND; then BRANCH1; [elif COND2; then BRANCH2;]... [else BRANCHN;] fi` structure
 * (COND/BRANCHes may themselves contain further `;`-separated commands, handled by recursing
 * back into exec_range), otherwise splits off and runs one simple command up to the next
 * top-level `;` and continues with the remainder. Returns the real exit status of the LAST thing
 * actually run (0 for an empty range). */
static int exec_range(char **words, int start, int end, Arena *expand_arena) {
    if (start >= end) return 0;

    if (strcmp(words[start], "if") == 0) {
        return exec_if_chain(words, start, end, expand_arena);
    }

    int semi = find_keyword(words, start, end, ";");
    if (semi < 0) {
        return exec_simple(words, start, end, expand_arena);
    }
    int first_status = exec_simple(words, start, semi, expand_arena);
    /* Real, live-found bug fixed here: a trailing ";" with nothing meaningful after it (e.g. the
     * condition slice of "if false; then ...", which is genuinely "false" followed by its own
     * trailing ";") must NOT blindly recurse into an empty [semi+1, end) range -- that range's
     * own base case returns a fixed 0, silently discarding the real exit status just computed
     * above. Confirmed live: this bug made every `if <condition>;` with a trailing semicolon
     * always look like the condition succeeded, regardless of its real exit status. */
    if (semi + 1 >= end) {
        return first_status;
    }
    return exec_range(words, semi + 1, end, expand_arena);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int interactive = isatty(fileno(stdin));
    char line[4096];
    int last_status = 0;

    if (interactive) fputs("$ ", stdout);
    while (fgets(line, sizeof line, stdin)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

        Arena arena;
        arena_init(&arena);
        Vec words = tokenize_line(line, &arena);
        int n = vec_len(&words);
        if (n > 0) {
            char **arr = (char **)malloc(sizeof(char *) * (size_t)n);
            for (int i = 0; i < n; i++) arr[i] = (char *)vec_get(&words, i);
            last_status = exec_range(arr, 0, n, &arena);
            free(arr);
        }
        arena_free_all(&arena);

        if (interactive) fputs("$ ", stdout);
    }
    if (interactive) fputc('\n', stdout);
    return last_status;
}
