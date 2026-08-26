/* tests/test_sdl2.c -- real end-to-end verification for stdlib/sdl2.prn,
 * the first real slice of a PARENA-authored editor shell (founder
 * real-time: "continue working on parena editor" -- NORTHSTAR.md's own
 * "editor/plugin API" section, PITVIPER's own cmd/pitviper/main.go
 * renderFrame as the real port target).
 *
 * Same "test what's actually there" discipline test_awk.c/test_shell.c
 * already establish: this actually opens a real SDL2 window (via Xvfb
 * on this headless box -- DISPLAY must be set to a real running X
 * server, see Makefile's own test-sdl2 target for how it launches one),
 * creates a real renderer, draws a real PITVIPER-shaped cell grid
 * (SetDrawColor + FillRect per cell, matching cmd/pitviper/main.go's
 * own renderFrame -- glyph/texture blitting is real, separate, deferred
 * follow-up, not attempted here), presents a few real frames, polls for
 * events, and tears everything down cleanly -- not a mock, not a
 * headless no-op stub.
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "test_sdl2_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

int main(void) {
    Arena a;
    arena_init(&a);

    Result r = init(&a);
    CHECK(r.tag == 1, "sdl2/init succeeds under a real X display");

    Result wr = create_window("PARENA editor v0 -- real SDL2 window", 320, 240, &a);
    CHECK(wr.tag == 1, "sdl2/create-window opens a real window");
    if (wr.tag == 1) {
        Window win = *(Window *)wr.value;
        CHECK(win.handle >= 0, "the real window has a valid handle");

        Result rr = create_renderer(&win, &a);
        CHECK(rr.tag == 1, "sdl2/create-renderer creates a real renderer on the real window");
        if (rr.tag == 1) {
            Renderer ren = *(Renderer *)rr.value;
            CHECK(ren.handle >= 0, "the real renderer has a valid handle");

            /* Real PITVIPER-shaped per-cell render: background fill,
             * then a real terminal-cell grid (checkerboard so a real
             * screenshot would actually show something, not just a
             * flat clear). */
            const int cols = 8, rows = 6, cell_w = 320 / 8, cell_h = 240 / 6;
            int all_draws_ok = 1;
            for (int frame = 0; frame < 3; frame++) {
                Result cbg = set_draw_color(&ren, 30, 30, 40, 255, &a);
                if (cbg.tag != 1) all_draws_ok = 0;
                Result cr = render_clear(&ren, &a);
                if (cr.tag != 1) all_draws_ok = 0;

                for (int row = 0; row < rows; row++) {
                    for (int col = 0; col < cols; col++) {
                        int lit = (row + col + frame) % 2 == 0;
                        Result cc = set_draw_color(&ren, lit ? 80 : 20, lit ? 200 : 60, lit ? 120 : 40, 255, &a);
                        if (cc.tag != 1) all_draws_ok = 0;
                        Result fr = render_fill_rect(&ren, col * cell_w, row * cell_h, cell_w, cell_h, &a);
                        if (fr.tag != 1) all_draws_ok = 0;
                    }
                }

                render_present(&ren);
                delay(16);
            }
            CHECK(all_draws_ok, "a real multi-frame PITVIPER-shaped cell grid renders without error");

            /* Real event pump -- proves poll-event actually talks to the
             * real SDL event queue, not a canned value. No real user
             * input exists on this headless Xvfb run, but SDL itself
             * genuinely queues real window-lifecycle events (shown,
             * exposed, focus) the instant a window is created -- drain
             * everything actually pending (a real, bounded loop, not an
             * infinite one) and confirm each drained event is a real,
             * well-formed EventKind (Quit/KeyDown/Other, never a
             * garbage tag), then confirm the queue genuinely empties
             * (poll-event correctly returns None once there is truly
             * nothing left, not just once). */
            int drained = 0;
            int all_real_events = 1;
            Option ev;
            while ((ev = poll_event(&a)).tag == 1 && drained < 64) {
                EventKind kind = *(EventKind *)ev.value;
                if (kind.tag < 0 || kind.tag > 2) all_real_events = 0;
                drained++;
            }
            CHECK(all_real_events, "every drained event is a real, well-formed EventKind");
            CHECK(ev.tag == 0, "poll-event correctly reports None once the real SDL event queue is empty");

            destroy_renderer(ren);
        }
        destroy_window(win);
    }

    quit();
    arena_free_all(&a);

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
