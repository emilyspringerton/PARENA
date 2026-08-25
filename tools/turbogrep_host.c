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
 */
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: turbogrep <pattern> <file> [file2 ...]\n");
        return 2;
    }
    const char *pattern = argv[1];
    int file_count = argc - 2;
    int any_match = 0;

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
