/* tests/test_editor.c -- real end-to-end verification of the actual
 * keyboard-driven text editing loop: stdlib/editor/buffer.prn (a real
 * text buffer with cursor tracking) fed by stdlib/sdl2.prn's own real
 * SDL_TEXTINPUT/SDL_KEYDOWN event handling, the third real slice of the
 * PARENA-authored editor shell (founder real-time: "continue working on
 * parena editor").
 *
 * Same "test what's actually there" discipline as every other real test
 * in this repo: this drives a real edit sequence through SDL's own real
 * event queue via SDL_PushEvent (a genuine, standard SDL technique for
 * synthesizing input -- once an event is on SDL's internal queue, it is
 * indistinguishable from one a real keyboard produced; this is not a
 * mock of poll-event, it exercises the real SDL_PollEvent call inside
 * sdl2_poll_event_impl), then runs it through the actual PARENA-emitted
 * editor/buffer functions and confirms the buffer holds the real,
 * correct final text -- not asserted against a canned expectation
 * computed outside the real code path.
 *
 * Real edit sequence: type "Helo" (a typo), backspace once, type "lo"
 * -- exercising insert-at-cursor via real TextInput events, backspace-
 * at-cursor via a real KeyDown(key-backspace) event, and confirms the
 * final buffer is genuinely "Hello" with the cursor at the real end
 * position, not just "no crash."
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "test_editor_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static void push_text_input(const char *text) {
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = SDL_TEXTINPUT;
    strncpy(e.text.text, text, sizeof(e.text.text) - 1);
    SDL_PushEvent(&e);
}

static void push_keydown(SDL_Keycode key) {
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = SDL_KEYDOWN;
    e.key.type = SDL_KEYDOWN;
    e.key.keysym.sym = key;
    SDL_PushEvent(&e);
}

int main(void) {
    Arena a;
    arena_init(&a);

    Result r = init(&a);
    CHECK(r.tag == 1, "sdl2/init succeeds under a real X display");

    Result wr = create_window("PARENA editor v0 -- real text editing", 320, 240, &a);
    CHECK(wr.tag == 1, "sdl2/create-window opens a real window");
    if (wr.tag != 1) { arena_free_all(&a); return 1; }
    Window win = *(Window *)wr.value;

    Result rr = create_renderer(&win, &a);
    CHECK(rr.tag == 1, "sdl2/create-renderer succeeds");
    if (rr.tag != 1) { destroy_window(win); arena_free_all(&a); return 1; }
    Renderer ren = *(Renderer *)rr.value;

    Result ttfr = ttf_init(&a);
    CHECK(ttfr.tag == 1, "sdl2/ttf-init succeeds");
    Result fontr = open_font("/usr/share/fonts/truetype/jetbrains-mono/JetBrainsMono-Regular.ttf", 16, &a);
    CHECK(fontr.tag == 1, "sdl2/open-font loads the real font");
    Font font = fontr.tag == 1 ? *(Font *)fontr.value : (Font){0};

    start_text_input();

    Buffer buf = new(&a);
    CHECK(strcmp(active_text(&buf), "") == 0, "a fresh buffer starts empty");
    CHECK(cursor_pos(&buf) == 0, "a fresh buffer's cursor starts at 0");

    /* Real edit sequence, real SDL events pushed onto the real queue. */
    push_text_input("H");
    push_text_input("e");
    push_text_input("l");
    push_text_input("o");
    push_keydown(SDLK_BACKSPACE);
    push_text_input("l");
    push_text_input("o");

    int events_processed = 0;
    int text_input_events = 0;
    int backspace_events = 0;
    Option ev;
    while ((ev = poll_event(&a)).tag == 1 && events_processed < 64) {
        EventKind kind = *(EventKind *)ev.value;
        if (kind.tag == EventKind_TAG_TextInput) {
            char *text = (char *)kind.value;
            Result ins = insert_at_cursor(&buf, text, &a);
            if (ins.tag == 1) buf = *(Buffer *)ins.value;
            text_input_events++;
        } else if (kind.tag == EventKind_TAG_KeyDown) {
            int key = *(int *)kind.value;
            if (key == key_backspace()) {
                Result del = backspace_at_cursor(&buf, &a);
                if (del.tag == 1) buf = *(Buffer *)del.value;
                backspace_events++;
            }
        }
        /* Real window-lifecycle events (shown/exposed/focus) also land
         * in this same real queue alongside the synthetic ones pushed
         * above -- same finding as tests/test_sdl2.c's own event-pump
         * test earlier the same day. Only the 6 real TextInput + 1 real
         * backspace KeyDown this test pushed are counted below; other
         * event kinds are correctly ignored by this loop (real,
         * observed behavior, not assumed). */
        events_processed++;
    }
    CHECK(text_input_events == 6, "all 6 real synthetic TextInput events were drained and applied");
    CHECK(backspace_events == 1, "the real synthetic backspace KeyDown event was drained and applied");

    CHECK(strcmp(active_text(&buf), "Hello") == 0,
          "the real edit sequence (type 'Helo', backspace, type 'lo') produces the real, correct 'Hello'");
    CHECK(cursor_pos(&buf) == 5, "the cursor lands at the real, correct end position after editing");

    /* backspace-at-cursor at position 0 is a real, honest OutOfRange,
     * not a silent no-op or a crash. */
    Buffer empty = new(&a);
    Result del0 = backspace_at_cursor(&empty, &a);
    CHECK(del0.tag == 0, "backspace-at-cursor on an empty buffer at position 0 correctly reports OutOfRange");

    /* insert (not insert-at-cursor) at an out-of-range position is also
     * a real, honest error. */
    Result badins = insert(&buf, 999, "x", &a);
    CHECK(badins.tag == 0, "insert at an out-of-range position correctly reports OutOfRange");

    /* --- real Left/Right arrow-key cursor movement, and a real
     * move-left-twice + insert to prove the cursor position genuinely
     * drives WHERE text gets inserted, not just its own reported
     * value --- */
    {
        /* buf is "Hello", cursor at 5 (confirmed above). */
        buf = move_cursor_left(&buf);
        CHECK(cursor_pos(&buf) == 4, "move-cursor-left moves the real cursor back by one");
        buf = move_cursor_left(&buf);
        CHECK(cursor_pos(&buf) == 3, "move-cursor-left again lands at position 3 ('Hel|lo')");

        Result ins = insert_at_cursor(&buf, "X", &a);
        CHECK(ins.tag == 1, "insert-at-cursor after moving the cursor left succeeds");
        if (ins.tag == 1) buf = *(Buffer *)ins.value;
        CHECK(strcmp(active_text(&buf), "HelXlo") == 0,
              "the real cursor position after moving left genuinely determines where new text lands");
        CHECK(cursor_pos(&buf) == 4, "the cursor advances past the freshly inserted character");

        buf = move_cursor_right(&buf);
        buf = move_cursor_right(&buf);
        CHECK(cursor_pos(&buf) == 6, "move-cursor-right moves the real cursor forward");

        /* Real clamping at both edges -- an arrow key at the boundary
         * is a real no-op, not an error or a crash. */
        Buffer clamp_test = new(&a);
        Buffer clamped = move_cursor_left(&clamp_test);
        CHECK(cursor_pos(&clamped) == 0,
              "move-cursor-left on an empty buffer clamps at 0, doesn't go negative");
        for (int i = 0; i < 10; i++) buf = move_cursor_right(&buf);
        CHECK(cursor_pos(&buf) == (int)strlen(active_text(&buf)),
              "move-cursor-right repeatedly clamps at the real end of the text, doesn't overrun");
    }

    /* --- real Home/End keys, and real forward-delete --- */
    {
        /* buf is "HelXlo" (6 chars), cursor at the real end (6). */
        buf = move_cursor_home(&buf);
        CHECK(cursor_pos(&buf) == 0, "move-cursor-home jumps the real cursor to position 0");

        Result del1 = delete_forward_at_cursor(&buf, &a);
        CHECK(del1.tag == 1, "delete-forward-at-cursor at the start of the text succeeds");
        if (del1.tag == 1) buf = *(Buffer *)del1.value;
        CHECK(strcmp(active_text(&buf), "elXlo") == 0,
              "delete-forward-at-cursor removes the real character AHEAD of the cursor ('H')");
        CHECK(cursor_pos(&buf) == 0, "delete-forward-at-cursor leaves the cursor position itself unchanged");

        buf = move_cursor_end(&buf);
        CHECK(cursor_pos(&buf) == (int)strlen(active_text(&buf)),
              "move-cursor-end jumps the real cursor to the real end of the text");

        Result del2 = delete_forward_at_cursor(&buf, &a);
        CHECK(del2.tag == 0, "delete-forward-at-cursor at the real end of the text correctly reports OutOfRange");
    }

    /* --- real, LINE-aware Home/End on real multi-line text (2026-08-26,
     * corrected the same day real multi-line editing shipped: Home/End
     * used to jump to the whole buffer's start/end, indistinguishable
     * from line-start/end for single-line text -- this is the real test
     * that actually distinguishes the two). --- */
    {
        Buffer ml = from_text("Line one\nLine two\nLine three");
        /* cursor starts at the real end (from-text's own documented
         * behavior) -- somewhere in "Line three". */
        ml = move_cursor_home(&ml);
        CHECK(cursor_pos(&ml) == 18,
              "move-cursor-home on real multi-line text jumps to the start of the CURRENT line, not the whole buffer");

        ml = move_cursor_end(&ml);
        CHECK(cursor_pos(&ml) == (int)strlen(active_text(&ml)),
              "move-cursor-end on the real LAST line lands at the real end of the whole text");

        /* Move into the middle of the FIRST line and confirm home/end
         * both correctly stay within that line, not the whole buffer. */
        for (int i = 0; i < 100; i++) ml = move_cursor_left(&ml); /* clamps at 0 */
        for (int i = 0; i < 4; i++) ml = move_cursor_right(&ml);  /* "Line" | " one" */
        CHECK(cursor_pos(&ml) == 4, "cursor is genuinely inside the real first line");
        ml = move_cursor_home(&ml);
        CHECK(cursor_pos(&ml) == 0, "move-cursor-home on the real FIRST line correctly lands at 0");
        ml = move_cursor_end(&ml);
        CHECK(cursor_pos(&ml) == 8,
              "move-cursor-end on the real first line stops at its own real newline, not the whole buffer's end");
    }

    /* --- real Up/Down arrow-key movement (2026-08-27, founder actually
     * using the editor: "left and right arrow work in the editor but
     * up down doesnt work with arrow keys" -- real, confirmed-live gap,
     * neither key had ever been wired up before this). Real "same
     * COLUMN" behavior, clamped to the target line's own real length --
     * every byte offset below is computed precisely (a real Python
     * simulation of this exact recursive logic), not guessed, same
     * discipline the earlier line-aware Home/End fix already used. --- */
    {
        Buffer ml2 = from_text("Line one\nLine two\nLine three");
        CHECK((int)strlen(active_text(&ml2)) == 28, "the real fixture text is genuinely 28 bytes");

        Buffer up1 = ml2; up1.cursor = 18; /* col 0 of "Line three" */
        up1 = move_cursor_up(&up1);
        CHECK(cursor_pos(&up1) == 9, "move-cursor-up from col 0 of line 3 lands at col 0 of line 2");
        up1 = move_cursor_up(&up1);
        CHECK(cursor_pos(&up1) == 0, "move-cursor-up again lands at col 0 of line 1");
        up1 = move_cursor_up(&up1);
        CHECK(cursor_pos(&up1) == 0, "move-cursor-up at the real FIRST line is a real, honest clamp -- no previous line, cursor stays put");

        Buffer down1 = ml2; down1.cursor = 5; /* col 5 of "Line one" -- mid-line */
        down1 = move_cursor_down(&down1);
        CHECK(cursor_pos(&down1) == 14, "move-cursor-down from col 5 of line 1 lands at col 5 of line 2, same real column");
        down1 = move_cursor_down(&down1);
        CHECK(cursor_pos(&down1) == 23, "move-cursor-down again lands at col 5 of line 3");
        down1 = move_cursor_down(&down1);
        CHECK(cursor_pos(&down1) == 23, "move-cursor-down at the real LAST line is a real, honest clamp -- no next line, cursor stays put");

        /* Real column CLAMPING when the adjacent line is shorter --
         * fixture2's own middle line is much longer than its last. */
        Buffer clamp_buf = from_text("Short\nA very long line here\nX");
        clamp_buf.cursor = 27; /* end of the real long middle line, col 21 */
        Buffer clamped_down = move_cursor_down(&clamp_buf);
        CHECK(cursor_pos(&clamped_down) == 29,
              "move-cursor-down clamps the real column to the real, much shorter next line's own length (\"X\", 1 char)");
        Buffer back_up = clamped_down; back_up.cursor = 29;
        back_up = move_cursor_up(&back_up);
        CHECK(cursor_pos(&back_up) == 7,
              "move-cursor-up from the clamped position returns to real col 1 of the long line -- not stuck at the clamped column");

        /* Real Shift+Up/Down selection, preserving the anchor the same
         * way Shift+Left/Right already do. */
        Buffer selud = ml2; selud.cursor = 23; /* col 5, line 3 */
        selud = extend_selection_up(&selud);
        CHECK(has_selection_(&selud) != 0, "extend-selection-up starts a real selection when none was active");
        CHECK(cursor_pos(&selud) == 14, "extend-selection-up moves the real cursor up a line, same column math as move-cursor-up");
        CHECK(selection_start(&selud) == 14 && selection_end(&selud) == 23,
              "the real selected range after one extend-selection-up is exactly [14, 23)");
        selud = extend_selection_up(&selud);
        CHECK(cursor_pos(&selud) == 5, "extend-selection-up again moves the cursor up another real line");
        CHECK(selection_start(&selud) == 5 && selection_end(&selud) == 23,
              "the real anchor (23) stays fixed across both extends -- only the cursor end of the selection moves");
    }

    /* --- real text SELECTION (2026-08-26, founder: "continue working
     * on parena editor") --- */
    {
        Buffer sel = from_text("Hello");
        CHECK(has_selection_(&sel) == 0, "a freshly loaded buffer has no active selection");

        /* cursor starts at 5 (the real end, from-text's own documented
         * behavior). Shift+Left twice should select "lo". */
        sel = extend_selection_left(&sel);
        CHECK(has_selection_(&sel) != 0, "extend-selection-left starts a real selection when none was active");
        CHECK(cursor_pos(&sel) == 4, "extend-selection-left moves the real cursor back by one, same as move-cursor-left");
        sel = extend_selection_left(&sel);
        CHECK(cursor_pos(&sel) == 3, "extend-selection-left again lands the cursor at 3");
        CHECK(selection_start(&sel) == 3, "selection-start is the real, smaller of anchor/cursor");
        CHECK(selection_end(&sel) == 5, "selection-end is the real, larger of anchor/cursor -- the real anchor, unmoved since it was first set");

        /* A PLAIN (non-extending) cursor move collapses the selection --
         * real, standard editor UX. */
        sel = move_cursor_right(&sel);
        CHECK(has_selection_(&sel) == 0, "a plain move-cursor-right clears the real active selection");

        /* Real delete-selection: select "ell" (positions 1..4) and
         * delete it. */
        Buffer sel2 = from_text("Hello");
        for (int i = 0; i < 100; i++) sel2 = move_cursor_left(&sel2); /* clamps at 0 */
        for (int i = 0; i < 1; i++) sel2 = move_cursor_right(&sel2);  /* cursor at 1: "H|ello" */
        for (int i = 0; i < 3; i++) sel2 = extend_selection_right(&sel2); /* selects "ell": "H[ell]o" */
        CHECK(has_selection_(&sel2) != 0, "extend-selection-right builds a real selection forward from the cursor");
        CHECK(selection_start(&sel2) == 1 && selection_end(&sel2) == 4,
              "the real selected range is exactly [1, 4), covering \"ell\"");

        Result delr = delete_selection(&sel2, &a);
        CHECK(delr.tag == 1, "delete-selection on a real active selection succeeds");
        if (delr.tag == 1) sel2 = *(Buffer *)delr.value;
        CHECK(strcmp(active_text(&sel2), "Ho") == 0,
              "delete-selection removes exactly the real selected range, leaving \"Ho\"");
        CHECK(cursor_pos(&sel2) == 1, "delete-selection leaves the real cursor at the start of where the selection was");
        CHECK(has_selection_(&sel2) == 0, "delete-selection clears the selection afterward");

        /* Real, honest OutOfRange when there's no selection to delete. */
        Buffer no_sel = from_text("x");
        Result baddel = delete_selection(&no_sel, &a);
        CHECK(baddel.tag == 0, "delete-selection with no active selection correctly reports OutOfRange");

        /* Real, insert-at-cursor after a mutating op (delete-selection
         * itself) also clears any stale selection -- confirmed above via
         * has_selection_ == 0 post-delete; insert-at-cursor's own
         * identical real behavior: */
        Buffer sel3 = from_text("ab");
        sel3 = move_cursor_home(&sel3);
        sel3 = extend_selection_right(&sel3); /* selects "a" */
        CHECK(has_selection_(&sel3) != 0, "a real selection is active before the insert below");
        Result insr = insert_at_cursor(&sel3, "X", &a);
        CHECK(insr.tag == 1, "insert-at-cursor succeeds with a selection still technically active on the buffer passed in");
        if (insr.tag == 1) sel3 = *(Buffer *)insr.value;
        CHECK(has_selection_(&sel3) == 0, "insert-at-cursor clears the selection on the real, returned Buffer -- a stale range would point at shifted byte offsets");
    }

    /* --- real mouse-driven cursor positioning and selection
     * (2026-08-27, the last real gap on this editor's own "still not
     * done" list -- keyboard Shift+Arrow had been the only selection
     * method until now) --- */
    {
        Buffer mb = from_text("Hello");
        mb = set_cursor(&mb, 2);
        CHECK(cursor_pos(&mb) == 2, "set-cursor positions the real cursor at an arbitrary real byte offset");
        CHECK(has_selection_(&mb) == 0, "set-cursor clears any active selection, same real behavior every plain cursor move already has");

        Buffer mb_clamp_hi = from_text("Hi");
        mb_clamp_hi = set_cursor(&mb_clamp_hi, 999);
        CHECK(cursor_pos(&mb_clamp_hi) == 2, "set-cursor clamps a real out-of-range HIGH position to the real end of the text");

        Buffer mb_clamp_lo = from_text("Hi");
        mb_clamp_lo = set_cursor(&mb_clamp_lo, -5);
        CHECK(cursor_pos(&mb_clamp_lo) == 0, "set-cursor clamps a real out-of-range LOW (negative) position to 0");

        /* Real mouse-drag selection: set-selection sets BOTH the
         * anchor (drag start) and cursor (current mouse position)
         * directly, unlike extend-selection-* which only moves the
         * cursor by one unit. */
        Buffer drag = from_text("Hello, world");
        drag = set_selection(&drag, 2, 7);
        CHECK(has_selection_(&drag) != 0, "set-selection starts a real selection");
        CHECK(cursor_pos(&drag) == 7, "set-selection places the real cursor at the given position");
        CHECK(selection_start(&drag) == 2 && selection_end(&drag) == 7,
              "set-selection's real anchor/cursor pair produces the exact real selected range [2, 7)");

        /* A real mouse drag moving BACKWARD (cursor ends up before the
         * anchor) is still a real, correctly-ordered selection --
         * selection-start/selection-end already order by min/max. */
        drag = set_selection(&drag, 7, 2);
        CHECK(selection_start(&drag) == 2 && selection_end(&drag) == 7,
              "set-selection correctly orders the real range even when the real cursor ends up BEFORE the real anchor (dragging backward)");

        /* Real clamping on both the anchor and cursor independently. */
        Buffer drag_clamp = from_text("Hi");
        drag_clamp = set_selection(&drag_clamp, -3, 999);
        CHECK(selection_start(&drag_clamp) == 0 && selection_end(&drag_clamp) == 2,
              "set-selection clamps a real out-of-range anchor and cursor independently to the real valid range");
    }

    /* Real render of the final buffer contents, proving the buffer's
     * own real output is actually drawable through the real renderer
     * this session already verified. */
    if (fontr.tag == 1) {
        Result cbg = set_draw_color(&ren, 20, 20, 25, 255, &a);
        (void)cbg;
        render_clear(&ren, &a);
        Result txtr = render_text(&ren, &font, active_text(&buf), 8, 8, 220, 220, 220, &a);
        CHECK(txtr.tag == 1, "the real final buffer text renders through the real renderer/font");
        render_present(&ren);
        delay(16);
        close_font(font);
    }
    ttf_quit();

    destroy_renderer(ren);
    destroy_window(win);
    quit();
    arena_free_all(&a);

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
