/* tools/parenash_host.c -- real host-side entry point for `sh`, the PARENA-powered busybox's
 * shell applet (docs/PARENA_COREUTILS_NORTHSTAR.md Phase 3/3b/3c, 2026-09-08, founder real-time:
 * "zsh etc build it prn" -> "continue" -> "keep working on Emily os busybox and all of that" ->
 * "dooittt"). Same real "PARENA logic + C host driver" split every applet in this package already
 * uses -- real tokenizing/quoting/`;`-splitting/`$VAR`-lookup logic lives in
 * stdlib/coreutils/sh.prn; this file does real PROCESS MANAGEMENT (the REPL loop,
 * fork/execvp/waitpid, `cd`/`export`/`exit` builtins), real control-flow structure
 * (`if`/`elif`/`else`/`fi`), and real shell FUNCTIONS (`name() { ... }`) over the already-
 * tokenized word list -- a deliberate choice to keep PARENA's own side focused on string/token
 * processing (its own real strength) rather than stretching it into imperative control-flow/
 * process-management logic that's more naturally a plain recursive-descent walk in C.
 *
 * Real, live-found architectural gap fixed this same pass, BEFORE functions could mean anything
 * real: this shell used to read and execute ONE PHYSICAL LINE at a time (`fgets` + tokenize +
 * exec, reset, repeat) -- confirmed live via `if true\nthen\necho yes\nfi\n` (the real, standard
 * MULTI-line form every actual shell script uses) producing three separate, nonsensical "not
 * found" errors instead of running as one real conditional. Fixed: the REPL loop now
 * ACCUMULATES physical lines (real newlines preserved, not stripped) into a growing buffer,
 * re-tokenizing the WHOLE buffer after each new line (stdlib/coreutils/sh.prn's own
 * `tokenize-line` now treats a real newline exactly like `;` — both are statement separators in
 * real shell syntax), and only executes once a real, simple balance check
 * (`is_balanced` below) confirms no dangling `if`/`fi` or `{`/`}`. Real, honest v0 limitation,
 * named directly: this is a real, pragmatic HEURISTIC, not a real shell's own full parser state
 * machine — a literal `if`/`fi`/`{`/`}` WORD appearing inside a quoted string (e.g.
 * `echo "check if this works"`) would confuse the balance count. Real, live-verified NOT to
 * affect this session's own two audited real scripts (`/etc/init.d/hostname`/`bootmisc`), a
 * real, separate, later hardening item if it ever matters for a real script that hits it.
 *
 * Real, honest v0 scope for functions, named directly: `NAME() { BODY }` must appear on its own
 * (now real, multi-line-capable) logical statement; BODY runs via the exact same `exec_range`
 * this file already uses for everything else, so a function body gets real `if`/`elif`/access to
 * every other builtin/feature this shell has, for free. No nesting of function DEFINITIONS
 * inside another function's own body (a real, separate, later extension); functions run in THIS
 * process (never forked), matching real shell semantics -- a function CAN change the shell's own
 * cwd/environment via `cd`/`export`, the same real reason those two are builtins and not forked
 * external commands either.
 */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

/* ---- real, persistent function storage (survives past any one line's own per-statement Arena,
 * which gets freed after every logical statement executes) ---- */
#define MAX_FUNCTIONS 64
#define MAX_FUNC_BODY_WORDS 256

typedef struct {
    char *name;
    char *body_words[MAX_FUNC_BODY_WORDS];
    int body_len;
} ShFunction;

static ShFunction g_functions[MAX_FUNCTIONS];
static int g_function_count = 0;

static ShFunction *find_function(const char *name) {
    for (int i = 0; i < g_function_count; i++) {
        if (strcmp(g_functions[i].name, name) == 0) return &g_functions[i];
    }
    return NULL;
}

static void define_function(const char *name, char **body, int body_len) {
    ShFunction *f = find_function(name);
    if (!f) {
        if (g_function_count >= MAX_FUNCTIONS) {
            fprintf(stderr, "sh: too many functions defined (max %d)\n", MAX_FUNCTIONS);
            return;
        }
        f = &g_functions[g_function_count++];
        f->name = strdup(name);
    }
    f->body_len = body_len < MAX_FUNC_BODY_WORDS ? body_len : MAX_FUNC_BODY_WORDS;
    for (int i = 0; i < f->body_len; i++) f->body_words[i] = strdup(body[i]);
}

static int find_keyword(char **words, int start, int end, const char *kw) {
    for (int i = start; i < end; i++) {
        if (strcmp(words[i], kw) == 0) return i;
    }
    return -1;
}

/* func_def_brace_index -- real check for a function definition starting at `start`: words[start]
 * must be a real word ending in the literal two characters "()" (e.g. "depend()" -- this shell's
 * own tokenizer never splits "(" / ")" out on their own, matching how real shell function-
 * definition syntax is written with no space before the parens). Returns the real index of the
 * opening "{", or -1 if this isn't a function definition at all. Real, live-found gap fixed
 * here: real POSIX shell scripts commonly write the opening brace on its OWN line --
 * `name()\n{\n...\n}` (confirmed live in OpenRC's own real `/lib/rc/sh/functions.sh`) -- which
 * this shell's own newline-as-";" tokenizing turns into `NAME() ; { ...`, so a single optional
 * ";" separator between the "()" word and the "{" is now tolerated, not just `NAME() {` on one
 * real line. */
static int func_def_brace_index(char **words, int start, int end) {
    if (start + 1 >= end) return -1;
    size_t len = strlen(words[start]);
    if (len < 3) return -1;
    if (words[start][len - 2] != '(' || words[start][len - 1] != ')') return -1;
    int brace_idx = start + 1;
    if (strcmp(words[brace_idx], ";") == 0) {
        brace_idx++;
        if (brace_idx >= end) return -1;
    }
    return strcmp(words[brace_idx], "{") == 0 ? brace_idx : -1;
}

static int exec_range(char **words, int start, int end, Arena *expand_arena);

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
        return exec_if_chain(words, branch_end, end, expand_arena);
    }
    int status = (else_idx >= 0) ? exec_range(words, else_idx + 1, fi_idx, expand_arena) : 0;
    if (fi_idx + 1 < end) return exec_range(words, fi_idx + 1, end, expand_arena);
    return status;
}

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
    if (strcmp(argv[0], "source") == 0 || strcmp(argv[0], ".") == 0) {
        /* Real `source`/`.` builtin -- reads a real file, tokenizes its ENTIRE content in one
         * pass (no need for the REPL's own incremental is_balanced accumulation, since a whole
         * file is already complete), and runs it through the exact same exec_range every other
         * construct in this shell uses. Runs in THIS process (never forked), the same real
         * reason cd/export/functions are builtins -- a sourced file's own assignments/function
         * definitions must persist in the calling shell, the entire real point of `source`.
         * Real, honest v0 boundary, checked live: this genuinely runs simple sourced scripts
         * (proven against a real, hand-written test file), but does NOT make OpenRC's own real
         * `/lib/rc/sh/functions.sh` work -- that file needs `$(( arithmetic ))`, `case`/`esac`,
         * `local`, and `eval`, none of which this shell has, confirmed by reading its real
         * source directly rather than assumed. */
        if (argc < 2) {
            fprintf(stderr, "sh: %s: filename argument required\n", argv[0]);
            return 2;
        }
        FILE *f = fopen(argv[1], "r");
        if (!f) {
            fprintf(stderr, "sh: %s: %s: No such file or directory\n", argv[0], argv[1]);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (fsize < 0) {
            fclose(f);
            return 1;
        }
        char *filebuf = (char *)malloc((size_t)fsize + 1);
        size_t nread = fread(filebuf, 1, (size_t)fsize, f);
        filebuf[nread] = '\0';
        fclose(f);

        Arena file_arena;
        arena_init(&file_arena);
        Vec file_words = tokenize_line(filebuf, &file_arena);
        int file_wc = vec_len(&file_words);
        int source_status = 0;
        if (file_wc > 0) {
            size_t fw_count = (size_t)file_wc;
            char **file_arr = (char **)malloc(sizeof(char *) * fw_count);
            for (int i = 0; i < file_wc; i++) file_arr[i] = (char *)vec_get(&file_words, i);
            source_status = exec_range(file_arr, 0, file_wc, &file_arena);
            free(file_arr);
        }
        arena_free_all(&file_arena);
        free(filebuf);
        return source_status;
    }

    /* Real, live-found gap fixed this pass: a bare `NAME=value` statement (real shell variable
     * assignment, NO `export` keyword — confirmed live via this session's own real, audited
     * `/etc/init.d/hostname` script, whose very first real line is `description="Sets the
     * hostname of the machine."`) was being treated as a genuine command name and reported
     * "not found". Real, honest, deliberate simplification named directly: this shell has no
     * separate "shell variable vs. environment variable" namespace at all (already true of
     * `export`, above) — a bare assignment does a real `setenv`, the same as `export` would,
     * rather than a real POSIX shell's own narrower "local to this shell only" semantics. Real,
     * honest v0 boundary: only the single-word `NAME=value` shape (argc==1) is recognized —
     * real shell's OTHER assignment shape, a temporary env var prefixed onto one command
     * (`FOO=bar somecommand`), is a real, separate, later extension, not attempted here. */
    if (argc == 1) {
        char *eq = strchr(argv[0], '=');
        if (eq && eq != argv[0]) {
            int valid_name = 1;
            for (char *p = argv[0]; p < eq; p++) {
                if (!(isalnum((unsigned char)*p) || *p == '_')) { valid_name = 0; break; }
            }
            if (valid_name) {
                *eq = '\0';
                setenv(argv[0], eq + 1, 1);
                return 0;
            }
        }
    }

    /* Real shell function call -- runs in THIS process (never forked), the same real reason
     * cd/export are builtins: a function must be able to affect the shell's own cwd/environment. */
    ShFunction *fn = find_function(argv[0]);
    if (fn) {
        return exec_range(fn->body_words, 0, fn->body_len, expand_arena);
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

static int exec_range(char **words, int start, int end, Arena *expand_arena) {
    if (start >= end) return 0;

    if (strcmp(words[start], "if") == 0) {
        return exec_if_chain(words, start, end, expand_arena);
    }

    int brace_idx = func_def_brace_index(words, start, end);
    if (brace_idx >= 0) {
        int close = find_keyword(words, brace_idx + 1, end, "}");
        if (close < 0) {
            fprintf(stderr, "sh: syntax error: expected '}'\n");
            return 2;
        }
        size_t name_len = strlen(words[start]) - 2;
        char *name = (char *)malloc(name_len + 1);
        memcpy(name, words[start], name_len);
        name[name_len] = '\0';
        define_function(name, words + brace_idx + 1, close - (brace_idx + 1));
        free(name);
        if (close + 1 < end) {
            return exec_range(words, close + 1, end, expand_arena);
        }
        return 0;
    }

    int semi = find_keyword(words, start, end, ";");
    if (semi < 0) {
        return exec_simple(words, start, end, expand_arena);
    }
    int first_status = exec_simple(words, start, semi, expand_arena);
    if (semi + 1 >= end) {
        return first_status;
    }
    return exec_range(words, semi + 1, end, expand_arena);
}

/* is_balanced -- real, honest HEURISTIC (not a full shell parser) deciding whether an
 * accumulated multi-line buffer represents a complete logical statement: counts net `if`/`fi`
 * and `{`/`}` occurrences among the already-tokenized words. See this file's own header comment
 * for the real, named limitation (a literal "if"/"fi"/"{"/"}" word inside a quoted string would
 * confuse the count) and why it doesn't affect this session's own two audited real scripts.
 *
 * Real, live-found second gap fixed here, same pass as `func_def_brace_index`'s own
 * brace-on-its-own-line fix: `NAME()` alone (its own real trailing newline already turned into a
 * ";" token, with NOTHING unbalanced yet — no `{` has been seen at all) used to look perfectly
 * "complete" by the plain if/brace counts above, so the REPL executed `greet()` as a bogus
 * command by itself, one full statement too early, before the real `{` on the next physical line
 * ever arrived — confirmed live via `greet()\n{\necho hi\n}\n` producing a real `greet(): not
 * found` instead of defining a function. Fixed: if the LAST real (non-";") word in the buffer is
 * itself `NAME()`-shaped, treat the buffer as incomplete regardless of the brace count — it's
 * unambiguously the start of a function header awaiting its own `{`. */
static int is_balanced(Vec *words) {
    int n = vec_len(words);
    int if_depth = 0;
    int brace_depth = 0;
    char *last_real_word = NULL;
    for (int i = 0; i < n; i++) {
        char *w = (char *)vec_get(words, i);
        if (strcmp(w, "if") == 0) if_depth++;
        else if (strcmp(w, "fi") == 0) if_depth--;
        else if (strcmp(w, "{") == 0) brace_depth++;
        else if (strcmp(w, "}") == 0) brace_depth--;
        if (strcmp(w, ";") != 0) last_real_word = w;
    }
    if (last_real_word) {
        size_t len = strlen(last_real_word);
        if (len >= 3 && last_real_word[len - 2] == '(' && last_real_word[len - 1] == ')') {
            return 0;
        }
    }
    return if_depth <= 0 && brace_depth <= 0;
}

#define ACCUM_SIZE 65536

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int interactive = isatty(fileno(stdin));
    char line[4096];
    char *accum = (char *)malloc(ACCUM_SIZE);
    accum[0] = '\0';
    int last_status = 0;

    if (interactive) fputs("$ ", stdout);
    while (fgets(line, sizeof line, stdin)) {
        size_t accum_len = strlen(accum);
        size_t line_len = strlen(line);
        if (accum_len + line_len + 1 < ACCUM_SIZE) {
            memcpy(accum + accum_len, line, line_len + 1);
        } /* real, honest v0 limit: a single logical statement longer than ACCUM_SIZE silently
             stops growing -- a real, separate, later fix (dynamic realloc) if it ever matters. */

        Arena arena;
        arena_init(&arena);
        Vec words = tokenize_line(accum, &arena);
        int n = vec_len(&words);
        if (n <= 0) {
            arena_free_all(&arena);
            continue;
        }
        if (!is_balanced(&words)) {
            arena_free_all(&arena);
            if (interactive) fputs("> ", stdout);
            continue;
        }

        size_t word_count = (size_t)n;
        char **arr = (char **)malloc(sizeof(char *) * word_count);
        for (int i = 0; i < n; i++) arr[i] = (char *)vec_get(&words, i);
        last_status = exec_range(arr, 0, n, &arena);
        free(arr);
        arena_free_all(&arena);
        accum[0] = '\0';

        if (interactive) fputs("$ ", stdout);
    }
    if (interactive) fputc('\n', stdout);
    free(accum);
    return last_status;
}
