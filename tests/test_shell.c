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
#include <time.h>

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

    /* --- shell/spawn: the real, new (2026-08-27) resolve+pty-open
     * combinator the PARENA editor's own terminal panel calls --
     * proves it actually forks a real, working shell using resolve's
     * own auto-detected policy, not just that resolve itself returns
     * a plausible string. */
    {
        Result sr = spawn(80, 24, &a);
        CHECK(sr.tag == 1, "shell/spawn successfully forks a real pty-attached shell via resolve's own auto-detected policy");
        if (sr.tag == 1) {
            Pty sp = *(Pty *)sr.value;
            CHECK(sp.fd >= 0, "shell/spawn's own spawned pty has a real, valid master fd");
            Result swr = pty_write(&sp, "echo real-parena-shell-spawn-test; exit\n", &a);
            CHECK(swr.tag == 1, "pty-write to shell/spawn's own spawned shell succeeds");
            Result srr = pty_read(&sp, &a);
            char *sout = srr.tag == 1 ? (char *)srr.value : NULL;
            CHECK(sout != NULL && strstr(sout, "real-parena-shell-spawn-test") != NULL,
                  "real output from shell/spawn's own spawned shell round-trips back through pty-read");
            Result scr = pty_close(sp, &a);
            CHECK(scr.tag == 1, "pty-close on shell/spawn's own spawned pty succeeds");
        }
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

    /* --- pty/poll-read: the real, new, non-blocking sibling to
     * pty-read (2026-08-27, real gap found integrating this into the
     * PARENA editor's own terminal panel -- a UI render loop can't
     * call the BLOCKING pty-read every frame against a long-lived
     * interactive shell without freezing the whole editor the moment
     * the shell sits idle). This time the spawned bash is genuinely
     * left INTERACTIVE (no "; exit"), proving pty-poll-read returns
     * immediately either way: no data pending yet (nothing written),
     * and real data once something is. A real wall-clock check proves
     * "returns immediately" isn't just an assumption -- if this
     * regressed back to a blocking read, this call would hang for the
     * real shell's own lifetime (or the whole test process's timeout),
     * not just run slow. */
    {
        Result r = spawn_bash(80, 24, &a);
        CHECK(r.tag == 1, "spawn-bash successfully forks a real, interactive pty-attached bash for poll-read testing");
        if (r.tag == 1) {
            Pty p = *(Pty *)r.value;

            time_t t0 = time(NULL);
            Result rr1 = pty_poll_read(&p, &a);
            time_t t1 = time(NULL);
            CHECK(rr1.tag == 1, "pty-poll-read returns Ok against a real, genuinely idle interactive shell");
            CHECK((t1 - t0) < 2,
                  "pty-poll-read returns immediately (well under a second) against an idle shell, "
                  "not blocked waiting for output that isn't coming");

            /* Give bash a real moment to actually print its own prompt
             * (a real, tiny, unavoidable race with a freshly-forked
             * interactive shell -- not polled instantly on purpose, so
             * this doesn't depend on winning that race for its own
             * real assertions below). */
            usleep(300000);

            Result wr2 = pty_write(&p, "echo real-parena-poll-test\n", &a);
            CHECK(wr2.tag == 1, "pty-write to the real, still-interactive spawned shell succeeds");

            /* Real, honest, bounded poll loop -- up to 2 real seconds,
             * checking every 50ms, matching the same real "poll every
             * frame" shape a real render loop uses, not a single
             * lucky-timing check. */
            char *found = NULL;
            for (int attempt = 0; attempt < 40 && !found; attempt++) {
                usleep(50000);
                Result rr2 = pty_poll_read(&p, &a);
                if (rr2.tag == 1) {
                    char *chunk = (char *)rr2.value;
                    if (chunk && strstr(chunk, "real-parena-poll-test")) found = chunk;
                }
            }
            CHECK(found != NULL,
                  "real output from the real, still-interactive spawned shell round-trips back "
                  "through repeated pty-poll-read calls, the same real per-frame polling shape "
                  "the editor's own terminal panel uses");

            Result wr3 = pty_write(&p, "exit\n", &a);
            (void)wr3;
            Result cr2 = pty_close(p, &a);
            CHECK(cr2.tag == 1, "pty-close on the real, poll-read-tested spawned pty succeeds");
        }
    }

    arena_free_all(&a);

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
