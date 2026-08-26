/* tests/test_shell.c -- real end-to-end verification for stdlib/pty.prn
 * and stdlib/shell.prn, the concrete "PARENA eats PITVIPER" dogfooding
 * step NORTHSTAR.md's own strangler-fig section names: a real, direct
 * port of PITVIPER's own internal/pty/pty_linux.go (openpty-based spawn)
 * and internal/pty/pty_windows.go's shell-resolution policy (isWslStub,
 * findGitBash, resolve's own explicit > $SHELL > Git Bash > fallback
 * chain).
 *
 * Same "test what's actually there" discipline test_awk.c/test_json.c
 * already establish: this box is Linux, so the Windows-only half of
 * shell.prn's own policy (Git Bash detection) is verified for CORRECT
 * behavior on a box where it should find nothing (a real None, not a
 * crash or a false positive), not for actually finding a real Git Bash
 * install -- that half needs a real Windows box to verify further, a
 * real, honest, un-closed gap, not pretended solved here. pty.prn's own
 * spawn/read/write/close is verified for real, though: this test
 * actually forks a real child process attached to a real pty and reads
 * its real stdout back through the whole PARENA-emitted call chain.
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "test_shell_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

int main(void) {
    Arena a;
    arena_init(&a);

    /* --- shell/resolve: explicit arg wins over everything else --- */
    {
        Option explicit;
        explicit.tag = 1; /* Some */
        /* String is already a `char *` -- Option's own option_some()
         * stores the pointer directly as .value, no extra indirection. */
        explicit.value = (void *)"/bin/explicit-shell";
        char *resolved = resolve(explicit, &a);
        CHECK(strcmp(resolved, "/bin/explicit-shell") == 0,
              "resolve honors an explicit shell over $SHELL/PATH/fallback");
    }

    /* --- shell/resolve: no explicit arg falls through to a real,
     * non-empty resolution (this box's own real $SHELL, or the
     * platform fallback) -- never crashes, never returns an empty
     * string. */
    {
        Option none_explicit;
        none_explicit.tag = 0; /* None */
        none_explicit.value = NULL;
        char *resolved = resolve(none_explicit, &a);
        CHECK(resolved != NULL && strlen(resolved) > 0,
              "resolve with no explicit arg still resolves to a real, non-empty shell");
    }

    /* --- shell/find-git-bash: correctly finds nothing on this real
     * Linux box (the env vars it probes -- ProgramFiles etc. -- don't
     * exist here), proving the None path is real, not a crash or a
     * false positive from an uninitialized value. */
    {
        Option git_bash = find_git_bash(&a);
        CHECK(git_bash.tag == 0, "find-git-bash correctly finds nothing on this real Linux box");
    }

    /* --- pty/spawn-bash + real read/write/close, actually forking a
     * real child process attached to a real pty (not a mock).
     *
     * pty-read's own real, documented, narrow scope (same shape
     * tcp-read already carries) reads until the peer side closes --
     * against a long-lived INTERACTIVE bash sitting idle at its next
     * prompt, that read would simply block forever waiting for an EOF
     * that never comes. So the written command includes its own
     * `; exit` -- bash actually runs the echo, then terminates and
     * closes its side of the pty, giving pty-read a real EOF to
     * return on, rather than open-endedly polling an interactive
     * session (which pty-read's own real scope doesn't support). */
    {
        Result r = spawn_bash(80, 24, &a);
        CHECK(r.tag == 1, "spawn-bash successfully forks a real pty-attached bash");
        if (r.tag == 1) {
            Pty p = *(Pty *)r.value;
            CHECK(p.fd >= 0, "spawned pty has a real, valid master fd");

            Result rz = pty_resize(&p, 100, 40, &a);
            CHECK(rz.tag == 1, "pty-resize on the real spawned pty succeeds");

            Result wr = pty_write(&p, "echo real-parena-pty-test; exit\n", &a);
            CHECK(wr.tag == 1, "pty-write to the real spawned shell succeeds");

            Result rr = pty_read(&p, &a);
            CHECK(rr.tag == 1, "pty-read returns Ok after the real shell exits and closes its side");
            /* String is already a `char *` -- result_ok() boxes it by
             * storing the pointer directly as .value, no extra Foo_box()
             * indirection the way a struct payload (Pty above) needs. */
            char *out = rr.tag == 1 ? (char *)rr.value : NULL;
            CHECK(out != NULL && strstr(out, "real-parena-pty-test") != NULL,
                  "real output from the real spawned shell round-trips back through pty-read");

            Result cr = pty_close(p, &a);
            CHECK(cr.tag == 1, "pty-close on the real spawned pty succeeds");
        }
    }

    arena_free_all(&a);

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
