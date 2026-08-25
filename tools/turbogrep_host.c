/* tools/turbogrep_host.c -- real host-side CLI entry point for
 * stdlib/grep.prn's own `lines-matching`. Same real "PARENA module +
 * a real, hand-written C host driver" split ci_status_host.c already
 * established for this repo's own `parena ci-status` subcommand --
 * this one is a standalone verification/benchmark tool, not (yet)
 * wired into the `parena` CLI itself the way ci-status is; see
 * EMILY/BACKLOG.md's own S170-295 for why PATH-replacing the real
 * system grep is a separate, later, explicitly-approved step, not
 * done here.
 *
 * Deliberately NOT a separate translation unit with hand-duplicated
 * struct/extern declarations -- Parena's own generated structs
 * (FileHandle, Engine, OpenMode, Vec, Arena, Result) have no emitted
 * header today (a real, separate, small gap every other host-glue
 * file in this stdlib shares), so hand-redeclaring them in a second
 * .c file risks a real, silent ABI mismatch if they ever drift. This
 * file is meant to be concatenated directly onto the end of `parena
 * build`'s own generated grep output (see Makefile's own `turbogrep`
 * target) -- the same real, verified-working shape this session's own
 * test harness already used, just promoted to a real, reusable repo
 * artifact instead of a one-off /tmp file.
 *
 * Usage: turbogrep <pattern> <file> [file2 ...]
 * Prints every matching line, prefixed with its filename when more
 * than one file is given -- the same real convention GNU grep itself
 * uses, not invented here.
 *
 * Exit code 3 is a real, distinct sentinel (separate from 0=matched,
 * 1=no match, 2=usage error): the pattern parses fine but uses a
 * regex feature match-node doesn't implement yet (Plus/Optional/
 * Anchor, per docs/TURBOGREP_BOTTLENECK_AUDIT.md's own C2-C4) --
 * checked ONCE up front via pattern-supported? (regex/pcre.prn),
 * before touching any file, so a caller (tools/turbogrep-router.sh)
 * can tell "unsupported, fall back to real grep" apart from "ran and
 * genuinely found nothing" without scraping stderr text. */
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: turbogrep <pattern> <file> [file2 ...]\n");
        return 2;
    }
    const char *pattern = argv[1];
    int file_count = argc - 2;
    int any_match = 0;

    {
        Arena check_arena;
        arena_init(&check_arena);
        MatchBudget check_budget;
        check_budget.max_steps = 100000;
        Result check_compile = compile((char *)pattern, check_budget, &check_arena);
        if (check_compile.tag != 1) {
            fprintf(stderr, "turbogrep: %s: bad pattern\n", pattern);
            arena_free_all(&check_arena);
            return 2;
        }
        Regex *check_re = (Regex *)check_compile.value;
        if (!pattern_supported_(check_re)) {
            fprintf(stderr, "turbogrep: pattern uses an unimplemented feature (Plus/Optional/Anchor)\n");
            arena_free_all(&check_arena);
            return 3;
        }
        arena_free_all(&check_arena);
    }

    for (int i = 2; i < argc; i++) {
        const char *path = argv[i];
        Arena arena;
        arena_init(&arena);

        Result open_result = file_open((char *)path, OpenMode_Read(), &arena);
        if (open_result.tag != 1) {
            fprintf(stderr, "turbogrep: %s: cannot open\n", path);
            arena_free_all(&arena);
            continue;
        }
        FileHandle *fh = (FileHandle *)open_result.value;

        Result lm_result = lines_matching(*fh, (char *)pattern, Engine_Pcre(), &arena);
        if (lm_result.tag != 1) {
            fprintf(stderr, "turbogrep: %s: match failed\n", path);
            file_close(*fh, &arena);
            arena_free_all(&arena);
            continue;
        }
        Vec *matches = (Vec *)lm_result.value;
        int n = vec_len(matches);
        for (int m = 0; m < n; m++) {
            const char *line = (const char *)vec_get(matches, m);
            if (file_count > 1) {
                printf("%s:%s\n", path, line);
            } else {
                printf("%s\n", line);
            }
            any_match = 1;
        }

        file_close(*fh, &arena);
        arena_free_all(&arena);
    }

    return any_match ? 0 : 1;
}
