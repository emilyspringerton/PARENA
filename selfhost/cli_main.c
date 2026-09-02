/* selfhost/cli_main.c -- the real, standalone argv-parsing entry point for
 * the self-hosted PARENA compiler, closing the honest gap selfhost/
 * main.prn's own header comment names directly: "NOT yet a real
 * argv-parsing standalone executable the way `./parena build in.prn -o
 * out.c` is ... argv plumbing and a real main-emission convention are a
 * genuinely separate, unstarted emitter feature, not attempted here."
 *
 * Real, deliberate design: parena-c itself has no `(defn main ...)` ->
 * C `int main` emission convention for ANY PARENA program (confirmed
 * live, checked in src/emit.c -- see selfhost/main.prn's own header).
 * Rather than add that as a NEW compiler feature (a real, separate,
 * bigger undertaking with its own real design questions -- how does a
 * PARENA-level `main` even receive argv as a typed Vec String?), this
 * file is the same real, already-established pattern every selfhost
 * test driver already uses: a small, hand-written C `main` doing the
 * real OS-interop (argv -> a real Vec String, an exit code back out),
 * calling straight into the PARENA-compiled build-file/build-files --
 * all real compiler logic still lives entirely in the selfhost .prn
 * files, unchanged. The only thing new here is that this driver is a real,
 * permanent, user-runnable binary instead of a test-only one.
 *
 * Mirrors src/main.c's own real "build" subcommand shape exactly:
 *   parena-selfhost build <file1> [file2 ...] -o <output.c>
 * Every argument between "build" and "-o" is an input file, the same
 * real convention src/main.c's own header comment documents -- a
 * single input path calls build-file directly (no Vec needed); two or
 * more call build-files (the real multi-file merge path), matching
 * build-files' own real, already-tested "combined into one compilation
 * unit, in the order given" behavior.
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>

#include "selfhost_cli_gen.c"

int main(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "build") != 0) {
        fprintf(stderr, "usage: %s build <file1> [file2 ...] -o <output.c>\n", argv[0]);
        return 1;
    }
    if (argc < 5 || strcmp(argv[argc - 2], "-o") != 0) {
        fprintf(stderr, "usage: %s build <file1> [file2 ...] -o <output.c>\n", argv[0]);
        return 1;
    }

    int path_count = argc - 4; /* argv[2..argc-3] are input files */
    char *out_path = argv[argc - 1];

    Arena a;
    arena_init(&a);

    Result r;
    if (path_count == 1) {
        r = build_file(argv[2], out_path, &a);
    } else {
        Vec paths = vec_new(&a);
        for (int i = 0; i < path_count; i++) {
            vec_push_(&paths, (void *)argv[2 + i]);
        }
        r = build_files(&paths, out_path, &a);
    }

    if (!r.tag) {
        fprintf(stderr, "%s\n", (char *)r.value);
        return 1;
    }
    printf("parena-selfhost: %d file%s -> %s\n", path_count, path_count == 1 ? "" : "s", out_path);
    return 0;
}
