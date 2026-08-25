/* tools/turbosed_host.c -- real host-side CLI entry point for
 * stdlib/sed.prn's own `substitute`. Same "PARENA module + a real,
 * hand-written C host driver" split turbogrep_host.c already
 * established -- see that file's own header comment for why this
 * exists as a separate concatenated-onto-generated-output driver
 * rather than a PARENA-side main.
 *
 * Usage: turbosed 's/PATTERN/REPLACEMENT/[g]' <file>
 * Deliberately the single narrowest real sed invocation shape --
 * exactly one bare `s/PAT/REPL/` or `s/PAT/REPL/g` expression, one
 * file, no other flags (-n/-i/-e/multiple scripts, line-addressing,
 * `d`/`p`/other commands are all real, unimplemented sed features --
 * a bare (no trailing `g`) expression matches real sed's own default
 * of first-match-per-line only -- tools/turbosed-router.sh
 * is what actually decides whether an invocation is even a candidate
 * for this binary at all, same layered-safety shape turbogrep-router.sh
 * already uses). Prints the substituted text to stdout; does not write
 * back to the file (no -i here) -- that's the router/caller's job if
 * ever added, matching sed's own real -i semantics being a caller-side
 * concern (write to a temp file, rename over the original), not
 * something this narrow substitute-and-print core needs to know about.
 *
 * Exit codes: 0 = ran, printed substituted output (whether or not any
 * substitution actually matched -- same as real GNU sed's own exit 0
 * for "ran successfully" regardless of match count). 2 = usage error
 * (wrong arg count, malformed `s/../../ ` expression, file open
 * failure). No exit-3 unsupported-pattern sentinel here the way
 * turbogrep has one: pattern-supported? was never called during this
 * build -- router.sh does that check itself before invoking this
 * binary at all (same reason: don't duplicate the capability check in
 * two places that could drift). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* parse_sed_expr -- splits a bare `s/PAT/REPL/` or `s/PAT/REPL/g` (or
 * `s#PAT#REPL#`/`s#PAT#REPL#g` etc, any single ASCII punctuation
 * delimiter, matching real sed's own convention) into pattern,
 * replacement, and the global flag. Real, honest scope: no
 * escaped-delimiter support (`s/a\/b/c/` is not handled -- the
 * delimiter literally cannot appear inside pattern or replacement),
 * and `g` is the only trailing flag understood -- anything else after
 * the final delimiter (i/m/p/other real sed flags) is a malformed
 * expression here, same "narrowest real shape, delegate everything
 * else" discipline turbogrep-router.sh's own layered-safety comment
 * documents. Returns 0 on success, -1 on a malformed expression. */
static int parse_sed_expr(const char *expr, char *pattern_out, char *repl_out, size_t bufsize, int *global_out) {
    if (expr[0] != 's' || expr[1] == '\0') return -1;
    char delim = expr[1];
    const char *p = expr + 2;
    const char *mid = strchr(p, delim);
    if (!mid) return -1;
    const char *end = strchr(mid + 1, delim);
    if (!end) return -1;

    size_t pat_len = (size_t)(mid - p);
    size_t repl_len = (size_t)(end - (mid + 1));
    if (pat_len >= bufsize || repl_len >= bufsize) return -1;

    memcpy(pattern_out, p, pat_len);
    pattern_out[pat_len] = '\0';
    memcpy(repl_out, mid + 1, repl_len);
    repl_out[repl_len] = '\0';

    const char *flags = end + 1;
    if (flags[0] == '\0') {
        *global_out = 0;
    } else if (flags[0] == 'g' && flags[1] == '\0') {
        *global_out = 1;
    } else {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: turbosed 's/PATTERN/REPLACEMENT/' <file>\n");
        return 2;
    }

    char pattern[4096];
    char replacement[4096];
    int global_flag = 0;
    if (parse_sed_expr(argv[1], pattern, replacement, sizeof(pattern), &global_flag) != 0) {
        fprintf(stderr, "turbosed: malformed expression: %s\n", argv[1]);
        return 2;
    }

    /* Same real, distinct exit-3 sentinel turbogrep_host.c already
     * established (separate from 0=ran/2=usage-error): the pattern
     * parses fine but uses a regex feature match-node doesn't
     * implement (Plus/Optional/Anchor), checked once up front via
     * pattern-supported? before touching the file, so
     * turbosed-router.sh can fall back to real sed without scraping
     * stderr text. */
    {
        Arena check_arena;
        arena_init(&check_arena);
        MatchBudget check_budget;
        check_budget.max_steps = 100000;
        Result check_compile = compile(pattern, check_budget, &check_arena);
        if (check_compile.tag != 1) {
            fprintf(stderr, "turbosed: %s: bad pattern\n", pattern);
            arena_free_all(&check_arena);
            return 2;
        }
        Regex *check_re = (Regex *)check_compile.value;
        if (!pattern_supported_(check_re)) {
            fprintf(stderr, "turbosed: pattern uses an unimplemented feature (Plus/Optional/Anchor)\n");
            arena_free_all(&check_arena);
            return 3;
        }
        arena_free_all(&check_arena);
    }

    Arena arena;
    arena_init(&arena);

    Result open_result = file_open(argv[2], OpenMode_Read(), &arena);
    if (open_result.tag != 1) {
        fprintf(stderr, "turbosed: %s: cannot open\n", argv[2]);
        arena_free_all(&arena);
        return 2;
    }
    FileHandle fh = *(FileHandle *)open_result.value;

    Result sub_result = substitute(fh, pattern, replacement, global_flag, &arena);
    file_close(fh, &arena);
    if (sub_result.tag != 1) {
        fprintf(stderr, "turbosed: substitution failed\n");
        arena_free_all(&arena);
        return 2;
    }

    fputs((char *)sub_result.value, stdout);

    arena_free_all(&arena);
    return 0;
}
