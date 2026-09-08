/* tools/parenash_host.c -- real host-side entry point for `sh`, the PARENA-powered busybox's
 * shell applet (docs/PARENA_COREUTILS_NORTHSTAR.md Phase 3, 2026-09-08, founder real-time:
 * "zsh etc build it prn"). Same real "PARENA logic + C host driver" split every applet in this
 * package already uses -- real tokenizing/quoting/`;`-splitting/`$VAR`-lookup logic lives in
 * stdlib/coreutils/sh.prn; this file does real PROCESS MANAGEMENT (the REPL loop,
 * fork/execvp/waitpid, and the `cd`/`exit` builtins, which must run in the parent process, not a
 * forked child, matching every real shell's own actual semantics).
 *
 * Real, honest v0 scope, named directly (not silently oversold): sequential simple commands
 * separated by `;`, `$VAR` expansion, `cd`/`exit` builtins. NO pipes, NO redirection, NO
 * functions, NO `if`/conditionals, NO job control/backgrounding. This does not run real OpenRC
 * init scripts yet -- confirmed via a real audit of this session's own built EmilyOS rootfs
 * (`/etc/init.d/hostname`/`bootmisc` use functions + `if`/`[` + `${var:-default}` parameter
 * expansion, genuinely beyond this v0) -- a real, separate, later phase, not attempted here.
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

/* Splits the real, already-tokenized word Vec at literal ";" entries into separate simple-
 * command argv arrays, running each in turn. Returns the real exit status of the LAST command
 * run (0 if the line had no real commands at all -- an empty or all-";" line). */
static int run_segments(Vec *words) {
    int n = vec_len(words);
    int last_status = 0;
    int start = 0;
    Arena expand_arena;
    arena_init(&expand_arena);

    for (int i = 0; i <= n; i++) {
        int at_boundary = (i == n) || (strcmp((char *)vec_get(words, i), ";") == 0);
        if (!at_boundary) continue;
        int seg_len = i - start;
        if (seg_len > 0) {
            char *argv[64];
            int argc = seg_len < 63 ? seg_len : 63;
            for (int k = 0; k < argc; k++) {
                argv[k] = expand_word((char *)vec_get(words, start + k), &expand_arena);
            }
            argv[argc] = NULL;

            if (strcmp(argv[0], "cd") == 0) {
                const char *target = argc >= 2 ? argv[1] : coreutils_getenv_impl("HOME");
                if (chdir(target) != 0) {
                    fprintf(stderr, "sh: cd: %s: No such file or directory\n", target);
                    last_status = 1;
                } else {
                    last_status = 0;
                }
            } else if (strcmp(argv[0], "export") == 0) {
                /* Real, minimal `export NAME=value` builtin -- must run in the parent process
                 * (setenv affects only the calling process's own environment), same real reason
                 * `cd` does. Real, honest v0: only the `NAME=value` form (no bare `export NAME`
                 * marking an existing shell variable for export -- this shell has no separate
                 * "shell variable vs. environment variable" distinction at all, everything is
                 * already a real environment variable via setenv). */
                if (argc >= 2) {
                    char *eq = strchr(argv[1], '=');
                    if (eq) {
                        *eq = '\0';
                        setenv(argv[1], eq + 1, 1);
                    }
                }
                last_status = 0;
            } else if (strcmp(argv[0], "exit") == 0) {
                int code = argc >= 2 ? atoi(argv[1]) : last_status;
                arena_free_all(&expand_arena);
                exit(code);
            } else {
                pid_t pid = fork();
                if (pid == 0) {
                    execvp(argv[0], argv);
                    fprintf(stderr, "sh: %s: not found\n", argv[0]);
                    _exit(127);
                } else if (pid > 0) {
                    int status = 0;
                    waitpid(pid, &status, 0);
                    last_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
                } else {
                    fprintf(stderr, "sh: fork failed\n");
                    last_status = 1;
                }
            }
        }
        start = i + 1;
    }

    arena_free_all(&expand_arena);
    return last_status;
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
        if (vec_len(&words) > 0) {
            last_status = run_segments(&words);
        }
        arena_free_all(&arena);

        if (interactive) fputs("$ ", stdout);
    }
    if (interactive) fputc('\n', stdout);
    return last_status;
}
