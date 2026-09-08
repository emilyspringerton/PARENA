/* tools/parenabusybox_host.c -- real host-side multi-call entry point for the PARENA-powered
 * busybox (docs/PARENA_COREUTILS_NORTHSTAR.md, 2026-09-08, founder real-time: "let's write our
 * own parena powered busybox"). Same real "PARENA logic + a real, hand-written C host driver"
 * split every other real PARENA binary in this repo already follows (tools/turbogrep_host.c,
 * examples/editor_main.c) -- this file provides main()/argv/stdio, real applet LOGIC lives in
 * stdlib/coreutils/*.prn.
 *
 * Real, standard busybox dual-invocation convention, not invented here: when this binary's own
 * argv[0] basename IS a known applet name (matching a real symlink pointing at it, e.g.
 * `/bin/echo -> parenabusybox`), dispatch directly; otherwise (invoked as plain
 * `parenabusybox <applet> [args...]`) shift argv left by one and dispatch on argv[1] instead --
 * real busybox itself falls back to exactly this same second form when invoked without any
 * applet-named symlink.
 *
 * Deliberately NOT a separate translation unit with hand-duplicated struct declarations -- same
 * real reason turbogrep_host.c's own header comment already names (Parena's generated structs
 * have no emitted header today): meant to be concatenated onto `parena build`'s own generated
 * coreutils output (see Makefile's own `parenabusybox` target).
 */
#include <stdio.h>
#include <string.h>

static const char *basename_c(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int do_echo(int argc, char **argv) {
    int i = 1;
    int suppress_newline = 0;
    if (i < argc && strcmp(argv[i], "-n") == 0) {
        suppress_newline = 1;
        i++;
    }
    Arena arena;
    arena_init(&arena);
    char *joined = (char *)"";
    for (; i < argc; i++) {
        char *with_arg = concat(joined, argv[i], &arena);
        joined = (i + 1 < argc) ? concat(with_arg, " ", &arena) : with_arg;
    }
    char *line = echo_line(joined, suppress_newline, &arena);
    fputs(line, stdout);
    arena_free_all(&arena);
    return 0;
}

static int do_basename(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: basename <path> [suffix]\n");
        return 2;
    }
    Arena arena;
    arena_init(&arena);
    const char *suffix = argc >= 3 ? argv[2] : "";
    char *result = basename_of(argv[1], (char *)suffix, &arena);
    printf("%s\n", result);
    arena_free_all(&arena);
    return 0;
}

static int do_pwd(int argc, char **argv) {
    (void)argc;
    (void)argv;
    Arena arena;
    arena_init(&arena);
    char *cwd = pwd_format(&arena);
    if (cwd[0] == '\0') {
        fprintf(stderr, "pwd: cannot determine current directory\n");
        arena_free_all(&arena);
        return 1;
    }
    printf("%s\n", cwd);
    arena_free_all(&arena);
    return 0;
}

static int dispatch(const char *applet, int argc, char **argv) {
    if (strcmp(applet, "echo") == 0) return do_echo(argc, argv);
    if (strcmp(applet, "basename") == 0) return do_basename(argc, argv);
    if (strcmp(applet, "pwd") == 0) return do_pwd(argc, argv);
    if (strcmp(applet, "true") == 0) return 0;
    if (strcmp(applet, "false") == 0) return 1;
    fprintf(stderr, "parenabusybox: applet not found: %s\n", applet);
    return 127;
}

int main(int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "parenabusybox: no argv[0]\n");
        return 2;
    }
    const char *self_name = basename_c(argv[0]);
    if (strcmp(self_name, "parenabusybox") != 0) {
        return dispatch(self_name, argc, argv);
    }
    if (argc < 2) {
        fprintf(stderr, "usage: parenabusybox <applet> [args...]\n"
                        "applets: echo basename pwd true false\n");
        return 2;
    }
    return dispatch(argv[1], argc - 1, argv + 1);
}
