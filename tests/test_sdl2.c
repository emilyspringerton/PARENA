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

/* poll_until_tag -- drains real pending events (bounded) until one
 * with the wanted real EventKind tag is found, or gives up. Real,
 * confirmed-live need (2026-08-27, real mouse event tests): a single
 * poll-event call right after pushing a synthetic event isn't safe to
 * assume is THAT event -- real window-lifecycle/X11 events genuinely
 * land in this same real queue too (same phenomenon this file's own
 * earlier event-pump test already documents for KeyDown/TextInput).
 *
 * Real, confirmed-live, precisely-isolated finding along the way (NOT
 * assumed): SDL_PollEvent right after SDL_PushEvent can genuinely
 * return "no event" ONCE even though the pushed event really is on the
 * queue (confirmed independently via SDL_PeepEvents(..., SDL_PEEKEVENT,
 * ...), which saw it when a same-instant SDL_PollEvent call did not) --
 * a real SDL2/Xvfb timing quirk in SDL_PollEvent's own internal
 * SDL_PumpEvents() step, reproduced deterministically across repeated
 * runs (always exactly one empty poll, never zero, never more than
 * one, for a MouseUp specifically). Real production input never hits
 * this (a real OS-generated event arrives via SDL_SendMouseButton, a
 * completely different code path that doesn't share this quirk) -- an
 * earlier draft of this exact helper gave up the INSTANT poll-event
 * returned None even once, which is why it kept failing here
 * specifically for MouseUp; fixed by retrying THROUGH empty polls too,
 * not just non-matching ones. */
static int poll_until_tag(Arena *a, int want_tag, EventKind *out) {
    for (int i = 0; i < 32; i++) {
        Option ev = poll_event(a);
        if (ev.tag != 1) continue;
        EventKind k = *(EventKind *)ev.value;
        if (k.tag == want_tag) { *out = k; return 1; }
    }
    return 0;
}

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

            /* Real text rendering, PITVIPER's own real font (JetBrains
             * Mono, the same font its own F11 "shiny font" toggle uses
             * via SDL2_ttf). */
            Result ttfr = ttf_init(&a);
            CHECK(ttfr.tag == 1, "sdl2/ttf-init succeeds");

            Result badfont = open_font("/nonexistent/font/path.ttf", 16, &a);
            CHECK(badfont.tag == 0, "open-font on a real nonexistent path correctly fails, not a false Ok");

            Result fontr = open_font(
                "/usr/share/fonts/truetype/jetbrains-mono/JetBrainsMono-Regular.ttf", 16, &a);
            CHECK(fontr.tag == 1, "sdl2/open-font loads the real JetBrains Mono font PITVIPER itself uses");
            if (fontr.tag == 1) {
                Font font = *(Font *)fontr.value;
                CHECK(font.handle >= 0, "the real font has a valid handle");

                int gw = measure_text_width(&font, "M");
                int gh = measure_text_height(&font, "M");
                CHECK(gw > 0 && gh > 0, "measure-text-width/height report real, positive glyph cell dimensions");

                Result txtr = render_text(&ren, &font, "PARENA editor -- real text", 4, 4, 230, 230, 230, &a);
                CHECK(txtr.tag == 1, "render-text draws a real string with the real font onto the real renderer");

                Result emptytxtr = render_text(&ren, &font, "", 4, 20, 230, 230, 230, &a);
                CHECK(emptytxtr.tag == 1, "render-text on an empty string is a real, honest no-op, not an error");

                render_present(&ren);
                delay(16);

                close_font(font);
            }
            ttf_quit();

            /* Real event pump -- proves poll-event actually talks to the
             * real SDL event queue, not a canned value. No real user
             * input exists on this headless Xvfb run, but SDL itself
             * genuinely queues real window-lifecycle events (shown,
             * exposed, focus) the instant a window is created -- drain
             * everything actually pending (a real, bounded loop, not an
             * infinite one) and confirm each drained event is a real,
             * well-formed EventKind (Quit/KeyDown/TextInput/MouseDown/
             * MouseUp/MouseMotion/UnhandledEvent, never a garbage tag),
             * then confirm the queue genuinely empties (poll-event
             * correctly returns None once there is truly nothing left,
             * not just once). Upper bound updated 2026-08-27 (real
             * mouse event plumbing grew EventKind from 4 real variants
             * to 7 -- this bound went stale the moment that landed,
             * caught by actually running this test, not noticed by
             * inspection). */
            int drained = 0;
            int all_real_events = 1;
            Option ev;
            while ((ev = poll_event(&a)).tag == 1 && drained < 64) {
                EventKind kind = *(EventKind *)ev.value;
                if (kind.tag < 0 || kind.tag > 6) all_real_events = 0;
                drained++;
            }
            CHECK(all_real_events, "every drained event is a real, well-formed EventKind");
            CHECK(ev.tag == 0, "poll-event correctly reports None once the real SDL event queue is empty");

            /* --- real clipboard round-trip + real Ctrl modifier
             * detection (2026-08-27, real Ctrl+C/X/V copy/cut/paste)
             * --- */
            set_clipboard_text("hello from parena");
            char *clip = get_clipboard_text(&a);
            CHECK(strcmp(clip, "hello from parena") == 0,
                  "set-clipboard-text then get-clipboard-text round-trips the real X11 clipboard exactly");

            CHECK(ctrl_held_() == 0, "ctrl-held? is false with no Ctrl key actually held");
            {
                /* Real, confirmed-live finding (2026-08-27): SDL_PushEvent
                 * alone does NOT update SDL's own internal modifier-state
                 * tracking (what SDL_GetModState reads) -- that update
                 * happens inside SDL_SendKeyboardKey, the real internal
                 * path a genuine OS-generated key event takes, which a
                 * manually pushed queue entry bypasses. Confirmed by
                 * actually trying the push-event approach first and
                 * watching it fail. SDL_SetModState is the real, public,
                 * intended-for-exactly-this SDL2 API for imposing a
                 * modifier state directly (its own real doc comment: "the
                 * inverse of SDL_GetModState... allows you to impose
                 * modifier key states on your program"). */
                SDL_SetModState(KMOD_LCTRL);
                CHECK(ctrl_held_() != 0, "ctrl-held? reports true once SDL_SetModState actually sets KMOD_LCTRL");
                SDL_SetModState(KMOD_NONE);
                CHECK(ctrl_held_() == 0, "ctrl-held? goes back to false once the modifier state is actually cleared");
            }

            /* --- real mouse event plumbing (2026-08-27, real mouse-
             * driven selection) -- unlike modifier state above, x/y are
             * plain DATA carried directly on the pushed event struct
             * (e.button.x/.y, e.motion.x/.y), read straight off by
             * sdl2_poll_event_impl -- no separate SDL-internal-state
             * side effect required, so SDL_PushEvent should genuinely
             * work here. Verified for real rather than assumed, same
             * discipline the modifier-state finding above already
             * established. --- */
            {
                EventKind k;

                SDL_Event down;
                memset(&down, 0, sizeof down);
                down.type = SDL_MOUSEBUTTONDOWN;
                down.button.type = SDL_MOUSEBUTTONDOWN;
                down.button.button = SDL_BUTTON_LEFT;
                down.button.x = 42;
                down.button.y = 99;
                SDL_PushEvent(&down);
                CHECK(poll_until_tag(&a, EventKind_TAG_MouseDown, &k), "poll-event correctly reports a real MouseDown");
                CHECK(mouse_x() == 42 && mouse_y() == 99,
                      "mouse-x/mouse-y report the real pushed event's own real coordinates, not stale or zeroed values");

                SDL_Event up;
                memset(&up, 0, sizeof up);
                up.type = SDL_MOUSEBUTTONUP;
                up.button.type = SDL_MOUSEBUTTONUP;
                up.button.button = SDL_BUTTON_LEFT;
                up.button.x = 7;
                up.button.y = 13;
                SDL_PushEvent(&up);
                CHECK(poll_until_tag(&a, EventKind_TAG_MouseUp, &k), "poll-event correctly reports a real MouseUp");
                CHECK(mouse_x() == 7 && mouse_y() == 13, "mouse-x/mouse-y update to the real MouseUp event's own coordinates");

                SDL_Event motion;
                memset(&motion, 0, sizeof motion);
                motion.type = SDL_MOUSEMOTION;
                motion.motion.type = SDL_MOUSEMOTION;
                motion.motion.x = 200;
                motion.motion.y = 150;
                SDL_PushEvent(&motion);
                CHECK(poll_until_tag(&a, EventKind_TAG_MouseMotion, &k), "poll-event correctly reports a real MouseMotion");
                CHECK(mouse_x() == 200 && mouse_y() == 150, "mouse-x/mouse-y update to the real MouseMotion event's own coordinates");

                /* A real RIGHT-click is deliberately NOT reported as
                 * MouseDown -- only SDL_BUTTON_LEFT is (this editor has
                 * no context menu yet, a real, separate, deferred gap).
                 * No poll_until_tag here on purpose: real interleaved
                 * events (window focus/expose, etc.) ALSO report as
                 * UnhandledEvent, so this assertion would pass
                 * vacuously if it hunted for that specific tag -- a
                 * single, immediate poll right after the push is the
                 * real, meaningful check (confirms THIS event, not
                 * "some event or other, eventually, reports
                 * UnhandledEvent"). */
                /* Drain to a genuinely clean queue first -- real
                 * interleaved events can still be pending from the
                 * MouseMotion push above, and this specific check needs
                 * to know the event it polls really is the one just
                 * pushed, not an unrelated real one that also happens
                 * to report UnhandledEvent. */
                { Option drain; int n = 0; while ((drain = poll_event(&a)).tag == 1 && n < 32) n++; }

                SDL_Event right;
                memset(&right, 0, sizeof right);
                right.type = SDL_MOUSEBUTTONDOWN;
                right.button.type = SDL_MOUSEBUTTONDOWN;
                right.button.button = SDL_BUTTON_RIGHT;
                right.button.x = 1;
                right.button.y = 1;
                SDL_PushEvent(&right);
                /* Retries past the real, confirmed SDL_PollEvent-right-
                 * after-SDL_PushEvent empty-poll quirk documented on
                 * poll_until_tag above -- the queue was just fully
                 * drained, so the first non-empty result really is this
                 * right-click, not a broad hunt for any UnhandledEvent. */
                Option rev;
                int found = 0;
                for (int i = 0; i < 32 && !found; i++) {
                    rev = poll_event(&a);
                    if (rev.tag == 1) found = 1;
                }
                CHECK(found, "a real pushed right-click MouseDown event is genuinely drained from the queue");
                if (found) {
                    EventKind rk = *(EventKind *)rev.value;
                    CHECK(rk.tag == EventKind_TAG_UnhandledEvent,
                          "a real right-click reports as UnhandledEvent, not MouseDown -- left-click only for now");
                }
            }

            destroy_renderer(ren);
        }
        destroy_window(win);
    }

    quit();
    arena_free_all(&a);

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
