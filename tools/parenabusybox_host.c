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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

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

/* read_all_fp -- real, plain-C whole-file slurp into a malloc'd, null-terminated buffer. Used by
 * do_wc/do_cat (both genuinely need the WHOLE content: wc to count newlines across the entire
 * input, cat to pass it straight through). Not routed through stdlib/io.prn's own raw-read-all --
 * that function operates on a PARENA FileHandle/Arena, and the host driver here already owns a
 * plain libc FILE* the same way do_pwd/do_basename already use plain C strings, not PARENA
 * Strings, for their own host-side argv handling. Caller frees the result. */
static char *read_all_fp(FILE *fp) {
    size_t cap = 4096, len = 0;
    char *buf = (char *)malloc(cap);
    for (;;) {
        if (len + 1 >= cap) {
            cap *= 2;
            buf = (char *)realloc(buf, cap);
        }
        size_t n = fread(buf + len, 1, cap - len - 1, fp);
        len += n;
        if (n == 0) break;
    }
    buf[len] = '\0';
    return buf;
}

static int do_wc(int argc, char **argv) {
    FILE *fp = stdin;
    const char *name = NULL;
    if (argc >= 2) {
        name = argv[1];
        fp = fopen(name, "r");
        if (!fp) {
            fprintf(stderr, "wc: %s: No such file or directory\n", name);
            return 1;
        }
    }
    char *content = read_all_fp(fp);
    if (fp != stdin) fclose(fp);
    int32_t n = count_lines(content);
    free(content);
    if (name) {
        printf("%d %s\n", n, name);
    } else {
        printf("%d\n", n);
    }
    return 0;
}

static int do_head(int argc, char **argv) {
    int n = 10; /* real, standard head default */
    int i = 1;
    if (i < argc && strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
        n = atoi(argv[i + 1]);
        i += 2;
    }
    FILE *fp = stdin;
    if (i < argc) {
        fp = fopen(argv[i], "r");
        if (!fp) {
            fprintf(stderr, "head: %s: No such file or directory\n", argv[i]);
            return 1;
        }
    }
    char line[4096];
    int32_t line_number = 0;
    while (fgets(line, sizeof(line), fp) != NULL) {
        line_number++;
        if (!head_should_print_(line_number, n)) break;
        fputs(line, stdout);
    }
    if (fp != stdin) fclose(fp);
    return 0;
}

static int do_yes(int argc, char **argv) {
    Arena arena;
    arena_init(&arena);
    char *joined = (char *)"";
    for (int i = 1; i < argc; i++) {
        char *with_arg = concat(joined, argv[i], &arena);
        joined = (i + 1 < argc) ? concat(with_arg, " ", &arena) : with_arg;
    }
    char *line = yes_line(joined, &arena);
    /* Real, standard `yes` behavior: repeat forever. A closed pipe (e.g. `yes | head`) delivers
     * SIGPIPE, whose default disposition terminates this process -- exactly the correct, real
     * exit path, not an error this loop needs to detect itself. */
    for (;;) {
        fputs(line, stdout);
    }
    arena_free_all(&arena); /* unreachable in practice, kept for a clean single exit shape */
    return 0;
}

static int do_cat(int argc, char **argv) {
    if (argc < 2) {
        char *content = read_all_fp(stdin);
        fputs(content, stdout);
        free(content);
        return 0;
    }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        FILE *fp = fopen(argv[i], "r");
        if (!fp) {
            fprintf(stderr, "cat: %s: No such file or directory\n", argv[i]);
            status = 1;
            continue;
        }
        char *content = read_all_fp(fp);
        fclose(fp);
        fputs(content, stdout);
        free(content);
    }
    return status;
}

static int do_sleep(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: sleep <seconds>\n");
        return 2;
    }
    /* Real, honest v0 boundary: whole seconds only (atoi truncates, matches sleep(3)'s own
     * integer-seconds signature) -- real `sleep` supports fractional seconds via a separate
     * nanosleep(2) path, a later extension, not silently promised here. */
    sleep((unsigned int)atoi(argv[1]));
    return 0;
}

static int do_env(int argc, char **argv) {
    (void)argc;
    (void)argv;
    for (char **e = environ; *e != NULL; e++) {
        printf("%s\n", *e);
    }
    return 0;
}

static int dispatch(const char *applet, int argc, char **argv) {
    if (strcmp(applet, "echo") == 0) return do_echo(argc, argv);
    if (strcmp(applet, "basename") == 0) return do_basename(argc, argv);
    if (strcmp(applet, "pwd") == 0) return do_pwd(argc, argv);
    if (strcmp(applet, "true") == 0) return 0;
    if (strcmp(applet, "false") == 0) return 1;
    if (strcmp(applet, "wc") == 0) return do_wc(argc, argv);
    if (strcmp(applet, "head") == 0) return do_head(argc, argv);
    if (strcmp(applet, "yes") == 0) return do_yes(argc, argv);
    if (strcmp(applet, "cat") == 0) return do_cat(argc, argv);
    if (strcmp(applet, "sleep") == 0) return do_sleep(argc, argv);
    if (strcmp(applet, "env") == 0) return do_env(argc, argv);
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
                        "applets: echo basename pwd true false wc head yes cat sleep env\n");
        return 2;
    }
    return dispatch(argv[1], argc - 1, argv + 1);
}
