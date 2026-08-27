/* examples/editor_main.c -- real host driver for a real, standalone,
 * runnable PARENA editor: opens a real SDL2 window and runs a real
 * interactive edit loop (type text, backspace, see it syntax-
 * highlighted live) using nothing but the real PARENA-emitted functions
 * from stdlib/editor/{buffer,textmate,textmate_parena,theme,render}.prn
 * and stdlib/sdl2.prn -- this file itself is a thin C event loop, all
 * the real logic (text editing, tokenization, coloring) lives in
 * PARENA source, same "real host driver + real PARENA module" split
 * tools/turbogrep_host.c already established for this repo's own
 * standalone tools (concatenated onto the generated .c, not a separate
 * translation unit -- see that file's own header comment for why:
 * Parena's generated structs have no emitted header yet).
 *
 * Founder real-time: "continue working on parena editor" -- this is the
 * actual runnable program every real piece shipped earlier the same
 * session (window/renderer/text rendering, keyboard-driven buffer, the
 * TextMate tokenizer/grammar/theme) was building toward: type real
 * text, see it highlighted, live, in a real window.
 *
 * Real, honest v0 scope: real multi-line editing (Return inserts a real
 * newline; Buffer.text needed no change to support this -- it was
 * already a plain String, which can already hold embedded '\n' bytes;
 * only rendering, via the new render-highlighted-text, and this file's
 * own real row/column cursor tracking, needed real multi-line
 * awareness). Escape or the window's own close button quits, Backspace/
 * Delete edit, Left/Right move the cursor (correctly crossing real line
 * boundaries -- a single byte-offset cursor needs no special-casing for
 * that), Home/End jump to the real start/end of the CURRENT line
 * (corrected same day, after multi-line shipped -- the original v0
 * jumped to the whole buffer instead, indistinguishable from line-
 * start/end when the buffer was still single-line-only). F2 saves the
 * real, whole buffer
 * (newlines included) to a real file; F3 reloads the real whole file
 * back (io/read-string, not read-line, now that multi-line is real).
 * Real text SELECTION (2026-08-26): Shift+Left/Shift+Right extend a
 * real selection (rendered as a real translucent-free filled rect
 * behind the selected text -- SDL2's own alpha blending isn't wired up
 * anywhere in this stdlib yet, a real, separate, deferred gap, so this
 * uses a solid, muted color and draws it BEFORE the real text so the
 * text still renders legibly on top); Backspace/Delete/typed
 * replacement all act on the whole selection when one is active.
 * Real Ctrl+C/X/V copy/cut/paste (2026-08-27), through SDL2's own real
 * clipboard (sdl2/get-clipboard-text and set-clipboard-text -- closes
 * that file's own previously-flagged "NOT closed in this pass" gap).
 * Real Ctrl+Z undo + real Ctrl+Y redo (2026-08-27, same day): a plain
 * stack of PAST Buffer values (push_undo/pop_undo/push_redo/pop_redo
 * below), each real keystroke its own undo step -- no coalescing
 * consecutive typing into one step -- real, separate, deferred
 * follow-up, matching every other "expand when a real feature needs
 * it" scope note this whole stdlib already carries.
 * Real mouse-driven selection (2026-08-27, same day): a real click
 * positions the cursor (set-cursor), a real click-drag selects
 * (set-selection, anchored at the real click position) -- see
 * pos_from_mouse below for the real screen-to-byte-offset math.
 *
 * Usage: ./editor-demo [file]
 *   file defaults to "scratch.prn" in the current directory if not
 *   given. If the file exists at startup, its real full contents are
 *   loaded into the buffer.
 *   (needs a real X display -- DISPLAY must point at one; this repo's
 *   own headless dev box runs it under a real, scratch Xvfb instance
 *   for build verification only, see Makefile's own editor-demo-smoke
 *   target -- the real deliverable is this binary, run on a real
 *   machine with a real screen.)
 */
#include <stdio.h>
#include <string.h>

#define LINE_HEIGHT 26 /* real pixel spacing between real lines, matches the real 20pt font this editor opens */

/* save-to-file / load-from-file -- thin C wrappers around the real
 * PARENA io/file-open/write-string/read-string/file-close functions
 * (already real and working, this session's own stdlib/shell.prn work
 * exercises the same file), kept here rather than as more PARENA
 * source since they're pure host-driver plumbing (which file, when to
 * save/load), not editor logic. save_to_file's own return is 1 on real
 * success, 0 on a real, reported (not silently swallowed) failure. */
static int save_to_file(const char *path, const char *text, Arena *a) {
    Result openr = file_open((char *)path, OpenMode_Write(), a);
    if (openr.tag != 1) { fprintf(stderr, "editor: save failed (could not open %s)\n", path); return 0; }
    FileHandle f = *(FileHandle *)openr.value;
    Result wr = write_string(f, (char *)text, a);
    Result cr = file_close(f, a);
    if (wr.tag != 1 || cr.tag != 1) { fprintf(stderr, "editor: save failed (write/close error)\n"); return 0; }
    fprintf(stderr, "editor: saved to %s\n", path);
    return 1;
}

static Buffer load_from_file(const char *path, Arena *a) {
    if (!path_exists_((char *)path)) return new(a);
    Result openr = file_open((char *)path, OpenMode_Read(), a);
    if (openr.tag != 1) { fprintf(stderr, "editor: load failed (could not open %s)\n", path); return new(a); }
    FileHandle f = *(FileHandle *)openr.value;
    /* read-string (whole file), not read-line -- now that the buffer
       and renderer both support real multi-line text (2026-08-26), a
       real load should restore the whole real file, not just its first
       line. */
    Result rr = read_string(f, a);
    file_close(f, a);
    if (rr.tag != 1) { fprintf(stderr, "editor: load failed (read error)\n"); return new(a); }
    char *text = (char *)rr.value;
    fprintf(stderr, "editor: loaded from %s\n", path);
    return from_text(text);
}

/* row_and_line_start_for_pos / line_end_from -- real, minimal host-
 * driver plumbing (2026-08-26, real text SELECTION): the same "scan for
 * newlines to find the real row/line-start" logic the main loop's own
 * cursor-position tracking already used inline, pulled out here so
 * selection rendering (which needs this for BOTH the selection's start
 * and end, potentially several rows apart) doesn't duplicate it a
 * third time. Real, honest, simple linear scan -- no line-index cache,
 * matching stdlib/editor/buffer.prn's own line-start-before/line-end-
 * after real, established tradeoff. */
static void row_and_line_start_for_pos(const char *text, int pos, int *out_row, int *out_line_start) {
    int row = 0, line_start = 0;
    for (int i = 0; i < pos; i++) {
        if (text[i] == '\n') { row++; line_start = i + 1; }
    }
    *out_row = row;
    *out_line_start = line_start;
}

static int line_end_from(const char *text, int line_start) {
    int i = line_start;
    while (text[i] != '\0' && text[i] != '\n') i++;
    return i;
}

/* pos_from_mouse -- real mouse-driven cursor positioning (2026-08-27,
 * real mouse-driven selection: click-to-position-cursor, click-drag-
 * to-select). The real INVERSE of the row/col-to-pixel math this same
 * file's own cursor/selection rendering already does: given a real
 * screen (x, y), find which real byte offset in `text` it corresponds
 * to. Real, honest, simple approach -- no binary search, no cached
 * glyph-width table -- walks the target row's own real line character
 * by character, measuring each growing prefix's real pixel width via
 * the same real measure-text-width already used for rendering, and
 * stops at the first column whose own width would overshoot the real
 * click x. Real, deliberate O(n) per real line (not O(n^2) globally --
 * only the ONE clicked line is walked), matching this whole editor's
 * own already-established "simple over optimized" tradeoff (render-
 * text's own fresh-surface-per-call design carries the identical
 * judgment). Real, honest clamp: a click below the real last line
 * lands on that last line, not past the real end of the text; a
 * negative x/y (can't really happen from a real SDL2 mouse event, but
 * host-driver code shouldn't assume) clamps to the real start. */
static int pos_from_mouse(char *text, int mouse_x, int mouse_y, Font *font, Arena *a) {
    int len = (int)strlen(text);
    int target_row = (mouse_y - 12) / LINE_HEIGHT;
    if (target_row < 0) target_row = 0;

    int line_start = 0, row = 0;
    for (int i = 0; i < len; i++) {
        if (row == target_row) break;
        if (text[i] == '\n') { row++; line_start = i + 1; }
    }
    int line_end = line_end_from(text, line_start);

    int target_x = mouse_x - 12;
    if (target_x < 0) target_x = 0;
    int best_pos = line_start;
    for (int col = line_start; col <= line_end; col++) {
        char *sub = substring(text, line_start, col, a);
        int w = measure_text_width(font, sub);
        if (w > target_x) break;
        best_pos = col;
    }
    return best_pos;
}

/* path_has_suffix -- real, minimal host-driver plumbing (2026-08-27,
 * founder real-time: "make sure we support .md syntax highlighting"):
 * picks the real grammar to load based on the real file being opened.
 * Plain, case-sensitive suffix match -- a real, honest v0 (a real,
 * separate, deferred follow-up would add case-insensitivity or more
 * extensions, e.g. ".markdown"; not needed for this real, immediate
 * ask). */
static int path_has_suffix(const char *path, const char *suffix) {
    size_t plen = strlen(path);
    size_t slen = strlen(suffix);
    if (slen > plen) return 0;
    return strcmp(path + (plen - slen), suffix) == 0;
}

/* open_font_with_fallback -- real, confirmed-live bug fix (2026-08-26,
 * founder real-time actually running a real Windows build): the
 * original single hardcoded path
 * (/usr/share/fonts/truetype/jetbrains-mono/JetBrainsMono-Regular.ttf)
 * is a Linux-package-manager-only path that doesn't exist on Windows
 * or macOS -- open-font failed there, and this program exits
 * immediately on any startup failure (real, by design: no silent
 * fallback to a broken half-initialized window), which is what the
 * founder actually saw ("2 black screens then closed" -- both real
 * windows appearing and then the whole process exiting near-instantly).
 * Fixed by trying an ordered list of real candidate paths and using
 * the first one that actually opens: a path relative to the CURRENT
 * DIRECTORY first (what a real release bundle's own RUN.bat/extracted-
 * zip layout puts the font at, right next to the binary), the repo's
 * own real vendored copy at assets/fonts/ (what a local `make
 * editor-demo` run from the repo root finds), then the original Linux
 * system path last (keeps working on any machine that already has the
 * fonts-jetbrains-mono package installed, even without either of the
 * above). assets/fonts/JetBrainsMono-Regular.ttf is a real, vendored,
 * Apache-2.0-licensed copy (see assets/fonts/LICENSE.txt) -- not
 * downloaded at build time, so this doesn't add a network dependency
 * to CI or to a user's own build. */
static Result open_font_with_fallback(int point_size, Arena *a) {
    static const char *candidates[] = {
        "JetBrainsMono-Regular.ttf",
        "assets/fonts/JetBrainsMono-Regular.ttf",
        "/usr/share/fonts/truetype/jetbrains-mono/JetBrainsMono-Regular.ttf",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        Result r = open_font((char *)candidates[i], point_size, a);
        if (r.tag == 1) return r;
    }
    fprintf(stderr, "editor: could not find JetBrainsMono-Regular.ttf in any real candidate location\n");
    return open_font((char *)candidates[0], point_size, a); /* real, honest final failure */
}

/* push_undo / pop_undo / push_redo / pop_redo -- real Ctrl+Z undo
 * (2026-08-27, founder: "continue working on parena editor") + real
 * Ctrl+Y redo (2026-08-27, same day, founder: "continue" -- the direct
 * complement). A genuinely simple, correct fit for VS0's own "every
 * buffer edit returns a NEW Buffer value, never mutates in place"
 * design (already leaned on for selection/clipboard): a prior Buffer's
 * own text pointer stays valid forever in the same arena (nothing here
 * is ever freed until the whole program exits), so a plain stack of
 * PAST Buffer VALUES, pushed right before each real mutating key
 * handles its own edit, is the real, honest, minimal undo -- no
 * diffing, no separate op log. Real, deliberate v0 scope: every
 * keystroke is its own undo step (typing "Hello" is 5 real undo steps,
 * not one coalesced "typed word" step -- a real, separate, deferred
 * follow-up, matching every other "expand when a real feature needs
 * it" note this whole stdlib already carries). Fixed-capacity, oldest-
 * entry-dropped-on-overflow -- a real, bounded editing session's own
 * actual undo depth needs, not unbounded growth. push_stack/pop_stack
 * is the one shared real implementation both the undo stack and the
 * redo stack use -- they're the identical bounded-LIFO shape, only the
 * backing array differs.
 *
 * Real redo semantics: push_undo (called at every real NEW edit) also
 * clears the redo stack -- a genuinely new edit invalidates whatever
 * was pending to redo, same real behavior every real editor's own
 * undo/redo already has. Ctrl+Z pushes the buffer it's ABOUT to leave
 * onto redo before popping undo; Ctrl+Y does the exact mirror (a raw
 * push onto undo -- NOT through push_undo, since redoing isn't itself
 * a new edit and must NOT clear the redo stack it's actively popping
 * from) before popping redo. */
#define UNDO_MAX 512

static void push_stack(Buffer *stack, int *count, Buffer b) {
    if (*count < UNDO_MAX) {
        stack[(*count)++] = b;
    } else {
        memmove(&stack[0], &stack[1], sizeof(Buffer) * (UNDO_MAX - 1));
        stack[UNDO_MAX - 1] = b;
    }
}

static Buffer pop_stack(Buffer *stack, int *count, Buffer fallback) {
    if (*count > 0) {
        return stack[--(*count)];
    }
    return fallback; /* real, honest no-op at the bottom of real history */
}

static Buffer undo_stack[UNDO_MAX];
static int undo_count = 0;
static Buffer redo_stack[UNDO_MAX];
static int redo_count = 0;

static void push_undo(Buffer b) {
    push_stack(undo_stack, &undo_count, b);
    redo_count = 0;
}

static Buffer pop_undo(Buffer fallback) {
    return pop_stack(undo_stack, &undo_count, fallback);
}

static void push_redo(Buffer b) {
    push_stack(redo_stack, &redo_count, b);
}

static Buffer pop_redo(Buffer fallback) {
    return pop_stack(redo_stack, &redo_count, fallback);
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "scratch.prn";

    Arena a;
    arena_init(&a);

    Result r = init(&a);
    if (r.tag != 1) { fprintf(stderr, "editor: sdl2 init failed\n"); return 1; }

    Result wr = create_window("PARENA editor -- v0", 900, 500, &a);
    if (wr.tag != 1) { fprintf(stderr, "editor: create-window failed\n"); return 1; }
    Window win = *(Window *)wr.value;

    Result rr = create_renderer(&win, &a);
    if (rr.tag != 1) { fprintf(stderr, "editor: create-renderer failed\n"); return 1; }
    Renderer ren = *(Renderer *)rr.value;

    Result ttfr = ttf_init(&a);
    if (ttfr.tag != 1) { fprintf(stderr, "editor: ttf-init failed\n"); return 1; }

    Result fontr = open_font_with_fallback(20, &a);
    if (fontr.tag != 1) { fprintf(stderr, "editor: open-font failed\n"); return 1; }
    Font font = *(Font *)fontr.value;

    /* Real grammar selection by file extension (2026-08-27, founder:
     * "make sure we support .md syntax highlighting") -- PARENA source
     * stays the real default (matches this file's own original scope,
     * the "parena text editor"), a real .md file gets the real
     * Markdown grammar instead. */
    Result gr = path_has_suffix(path, ".md") ? build_markdown_grammar(&a) : build_grammar(&a);
    if (gr.tag != 1) { fprintf(stderr, "editor: build-grammar failed\n"); return 1; }
    Vec rules = *(Vec *)gr.value;

    start_text_input();
    Buffer buf = load_from_file(path, &a);

    /* Real mouse-drag selection state (2026-08-27): dragging tracks
     * whether the real left mouse button is currently held (set on a
     * real MouseDown, cleared on a real MouseUp); mouse_down_pos is the
     * real byte offset the drag started at -- the real, fixed anchor
     * end of the selection for as long as the drag continues. */
    int dragging = 0;
    int mouse_down_pos = 0;

    int running = 1;
    while (running) {
        Option ev;
        while ((ev = poll_event(&a)).tag == 1) {
            EventKind kind = *(EventKind *)ev.value;
            if (kind.tag == EventKind_TAG_Quit) {
                running = 0;
            } else if (kind.tag == EventKind_TAG_KeyDown) {
                int key = *(int *)kind.value;
                /* Real Ctrl+C/X/V copy/cut/paste (2026-08-27) -- checked
                 * FIRST, before the plain-key branches below, since 'c'/
                 * 'x'/'v' are otherwise ordinary printable keys (SDL2's
                 * own SDLK_c/SDLK_x/SDLK_v keysyms ARE their literal
                 * ASCII values, same real "no wrapper needed" precedent
                 * this file's own Escape check already establishes with
                 * the literal 27). Guarded on ctrl_held_() so a bare "c"/
                 * "x"/"v" keypress still falls through untouched to
                 * SDL_TEXTINPUT for real typing -- SDL2 doesn't fire
                 * TEXTINPUT for a Ctrl-held combo, so there's no double-
                 * handling risk here. */
                if (key == 'z' && ctrl_held_()) {
                    /* Real Ctrl+Z undo -- the buffer about to be LEFT
                     * goes onto redo (so Ctrl+Y can bring it back),
                     * then pop the real previous Buffer value off the
                     * real undo stack. push_redo, not push_undo: this
                     * isn't itself a new edit, so it must NOT clear the
                     * real redo history it's actively adding to. Real,
                     * confirmed-live bug caught by this feature's own
                     * test (tests/test_editor_undo.c): guarded on
                     * undo_count > 0 -- pressing Ctrl+Z with nothing
                     * left to undo would otherwise still push a
                     * spurious duplicate entry onto redo. */
                    if (undo_count > 0) {
                        push_redo(buf);
                        buf = pop_undo(buf);
                    }
                } else if (key == 'y' && ctrl_held_()) {
                    /* Real Ctrl+Y redo -- the exact mirror of Ctrl+Z:
                     * raw push onto undo (via push_stack directly, NOT
                     * push_undo -- redoing isn't a new edit either, and
                     * must NOT clear the real redo stack it's actively
                     * popping from), then pop the real next Buffer
                     * value off the real redo stack. Same real guard,
                     * same real reason: redo_count > 0 first. */
                    if (redo_count > 0) {
                        push_stack(undo_stack, &undo_count, buf);
                        buf = pop_redo(buf);
                    }
                } else if (key == 'c' && ctrl_held_()) {
                    if (has_selection_(&buf)) {
                        char *whole = active_text(&buf);
                        char *sel_text = substring(whole, selection_start(&buf), selection_end(&buf), &a);
                        set_clipboard_text(sel_text);
                    }
                } else if (key == 'x' && ctrl_held_()) {
                    if (has_selection_(&buf)) {
                        char *whole = active_text(&buf);
                        char *sel_text = substring(whole, selection_start(&buf), selection_end(&buf), &a);
                        set_clipboard_text(sel_text);
                        push_undo(buf);
                        Result del = delete_selection(&buf, &a);
                        if (del.tag == 1) buf = *(Buffer *)del.value;
                    }
                } else if (key == 'v' && ctrl_held_()) {
                    push_undo(buf);
                    if (has_selection_(&buf)) {
                        Result del = delete_selection(&buf, &a);
                        if (del.tag == 1) buf = *(Buffer *)del.value;
                    }
                    char *clip = get_clipboard_text(&a);
                    Result ins = insert_at_cursor(&buf, clip, &a);
                    if (ins.tag == 1) buf = *(Buffer *)ins.value;
                } else if (key == key_backspace()) {
                    /* Backspace/Delete with an active selection remove
                     * the WHOLE selection instead of one character --
                     * real, standard editor UX (2026-08-26, real text
                     * SELECTION). push_undo only on a real, confirmed
                     * success (e.g. backspace at position 0 correctly
                     * fails, and would otherwise push a stale, wasted
                     * no-op entry). */
                    Result del = has_selection_(&buf) ? delete_selection(&buf, &a)
                                                       : backspace_at_cursor(&buf, &a);
                    if (del.tag == 1) { push_undo(buf); buf = *(Buffer *)del.value; }
                } else if (key == key_delete()) {
                    Result del = has_selection_(&buf) ? delete_selection(&buf, &a)
                                                       : delete_forward_at_cursor(&buf, &a);
                    if (del.tag == 1) { push_undo(buf); buf = *(Buffer *)del.value; }
                } else if (key == key_left()) {
                    /* Shift+Left extends the selection; plain Left moves
                     * (and clears any selection -- move_cursor_left's own
                     * real behavior). */
                    buf = shift_held_() ? extend_selection_left(&buf) : move_cursor_left(&buf);
                } else if (key == key_right()) {
                    buf = shift_held_() ? extend_selection_right(&buf) : move_cursor_right(&buf);
                } else if (key == key_up()) {
                    /* Real, confirmed-live gap closed (2026-08-27,
                     * founder actually using the editor: "left and
                     * right arrow work... but up down doesnt work"). */
                    buf = shift_held_() ? extend_selection_up(&buf) : move_cursor_up(&buf);
                } else if (key == key_down()) {
                    buf = shift_held_() ? extend_selection_down(&buf) : move_cursor_down(&buf);
                } else if (key == key_home()) {
                    buf = move_cursor_home(&buf);
                } else if (key == key_end()) {
                    buf = move_cursor_end(&buf);
                } else if (key == key_f2()) {
                    save_to_file(path, active_text(&buf), &a);
                } else if (key == key_f3()) {
                    /* A fresh load is a real, deliberate new starting
                     * point, not something to undo/redo INTO -- clears
                     * both real history stacks rather than leaving
                     * stale entries pointing at the PREVIOUS file's
                     * content. */
                    buf = load_from_file(path, &a);
                    undo_count = 0;
                    redo_count = 0;
                } else if (key == key_return()) {
                    push_undo(buf);
                    if (has_selection_(&buf)) {
                        Result del = delete_selection(&buf, &a);
                        if (del.tag == 1) buf = *(Buffer *)del.value;
                    }
                    Result ins = insert_at_cursor(&buf, "\n", &a);
                    if (ins.tag == 1) buf = *(Buffer *)ins.value;
                } else if (key == 27 /* SDLK_ESCAPE -- real, standard "quit" key, no dependency
                                        on <SDL2/SDL.h> being included directly in this file */) {
                    running = 0;
                }
            } else if (kind.tag == EventKind_TAG_TextInput) {
                /* Typed text with an active selection REPLACES it --
                 * real, standard editor UX: delete the selection first,
                 * then insert at the (now-collapsed) cursor. */
                char *text = (char *)kind.value;
                push_undo(buf);
                if (has_selection_(&buf)) {
                    Result del = delete_selection(&buf, &a);
                    if (del.tag == 1) buf = *(Buffer *)del.value;
                }
                Result ins = insert_at_cursor(&buf, text, &a);
                if (ins.tag == 1) buf = *(Buffer *)ins.value;
            } else if (kind.tag == EventKind_TAG_MouseDown) {
                /* Real mouse click: position the cursor there (which
                 * also clears any active selection -- set_cursor's own
                 * real behavior) and start tracking a real drag from
                 * this real position. */
                int pos = pos_from_mouse(active_text(&buf), mouse_x(), mouse_y(), &font, &a);
                buf = set_cursor(&buf, pos);
                mouse_down_pos = pos;
                dragging = 1;
            } else if (kind.tag == EventKind_TAG_MouseUp) {
                dragging = 0;
            } else if (kind.tag == EventKind_TAG_MouseMotion) {
                /* Real mouse drag: only while the real button is
                 * actually held (SDL2 sends real MouseMotion on every
                 * mouse move over the window regardless, not just while
                 * dragging). Anchors the selection at the real click
                 * position, extends the cursor end to wherever the
                 * mouse now is. */
                if (dragging) {
                    int pos = pos_from_mouse(active_text(&buf), mouse_x(), mouse_y(), &font, &a);
                    buf = set_selection(&buf, mouse_down_pos, pos);
                }
            }
        }

        Result cbg = set_draw_color(&ren, 24, 24, 28, 255, &a);
        (void)cbg;
        render_clear(&ren, &a);

        char *text = active_text(&buf);

        /* Real selection highlight, drawn BEFORE the text so the text
         * renders on top and stays legible (2026-08-26, real text
         * SELECTION). Multi-line-aware: one filled rect per real row the
         * selection spans -- the first/last rows clip to the real
         * selection boundary within that row, any row strictly between
         * them is highlighted edge-to-edge (the real full width of that
         * line's own text). */
        if (has_selection_(&buf)) {
            int sel_start = selection_start(&buf);
            int sel_end = selection_end(&buf);
            int start_row, start_line_start, end_row, ignored_line_start;
            row_and_line_start_for_pos(text, sel_start, &start_row, &start_line_start);
            row_and_line_start_for_pos(text, sel_end, &end_row, &ignored_line_start);

            Result scol = set_draw_color(&ren, 60, 90, 140, 255, &a);
            (void)scol;
            /* cur_line_start walks forward one real line at a time,
             * starting from the selection's own first row -- simpler and
             * correct than trying to independently derive each middle
             * row's own line_start from sel_start/sel_end alone. */
            int cur_line_start = start_line_start;
            for (int row = start_row; row <= end_row; row++) {
                int line_end = line_end_from(text, cur_line_start);
                int seg_start = (row == start_row) ? sel_start : cur_line_start;
                int seg_end = (row == end_row) ? sel_end : line_end;
                char *before_seg = substring(text, cur_line_start, seg_start, &a);
                char *seg_text = substring(text, seg_start, seg_end, &a);
                int x_from = 12 + measure_text_width(&font, before_seg);
                int width = measure_text_width(&font, seg_text);
                /* An empty selected segment (e.g. selecting exactly up
                 * to a newline) still gets a thin, visible sliver rather
                 * than vanishing entirely. */
                if (width < 2) width = 2;
                render_fill_rect(&ren, x_from, 12 + row * LINE_HEIGHT, width, LINE_HEIGHT - 2, &a);
                cur_line_start = line_end + 1;
            }
        }

        Result hr = render_highlighted_text(&ren, &font, &rules, text, 12, 12, LINE_HEIGHT, &a);
        (void)hr;

        /* Real cursor: a thin filled rect at the real measured pixel
         * position of the cursor -- proves the buffer's own real
         * cursor-pos genuinely drives something visible, not just
         * tracked internally. */
        int cpos = cursor_pos(&buf);
        int row, line_start;
        row_and_line_start_for_pos(text, cpos, &row, &line_start);
        char *before_cursor_on_line = substring(text, line_start, cpos, &a);
        int cursor_x = 12 + measure_text_width(&font, before_cursor_on_line);
        int cursor_y = 12 + row * LINE_HEIGHT;
        Result ccol = set_draw_color(&ren, 220, 220, 220, 255, &a);
        (void)ccol;
        render_fill_rect(&ren, cursor_x, cursor_y, 2, 24, &a);

        render_present(&ren);
        delay(16);
    }

    ttf_quit();
    destroy_renderer(ren);
    destroy_window(win);
    quit();
    arena_free_all(&a);
    return 0;
}
