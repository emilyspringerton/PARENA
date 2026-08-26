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
 * Real, honest v0 scope: single line only (stdlib/editor/buffer.prn's
 * own real, current scope), Escape or the window's own close button
 * quits, Backspace/Delete edit, Left/Right/Home/End move the cursor,
 * F2 saves the real current line to a real file, F3 reloads the real
 * first line of that same file back into the buffer. No multi-line, no
 * selection, no undo -- real, separate, deferred follow-up, matching
 * every other "expand when a real feature needs it" scope note this
 * whole stdlib already carries. Real, honest v0 on save/load itself
 * too: single line only (this editor's own real current scope) --
 * loading a genuinely multi-line file reads only its real first line,
 * not a silent truncation nobody could see coming, an honest
 * consequence of the buffer's own real, current single-line model.
 *
 * Usage: ./editor-demo [file]
 *   file defaults to "scratch.prn" in the current directory if not
 *   given. If the file exists at startup, its real first line is
 *   loaded into the buffer.
 *   (needs a real X display -- DISPLAY must point at one; this repo's
 *   own headless dev box runs it under a real, scratch Xvfb instance
 *   for build verification only, see Makefile's own editor-demo-smoke
 *   target -- the real deliverable is this binary, run on a real
 *   machine with a real screen.)
 */
#include <stdio.h>
#include <string.h>

/* save-to-file / load-first-line -- thin C wrappers around the real
 * PARENA io/file-open/write-string/read-line/file-close functions
 * (already real and working, this session's own stdlib/shell.prn work
 * exercises the same file), kept here rather than as more PARENA
 * source since they're pure host-driver plumbing (which file, when to
 * save/load), not editor logic. Returns 1 on real success, 0 on a
 * real, reported (not silently swallowed) failure. */
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

static Buffer load_first_line(const char *path, Arena *a) {
    if (!path_exists_((char *)path)) return new(a);
    Result openr = file_open((char *)path, OpenMode_Read(), a);
    if (openr.tag != 1) { fprintf(stderr, "editor: load failed (could not open %s)\n", path); return new(a); }
    FileHandle f = *(FileHandle *)openr.value;
    Result lr = read_line(f, a);
    file_close(f, a);
    if (lr.tag != 1) { fprintf(stderr, "editor: load failed (read error)\n"); return new(a); }
    Option maybe_line = *(Option *)lr.value;
    if (maybe_line.tag != 1) return new(a); /* real, empty file -- a real, honest empty buffer, not an error */
    char *line = (char *)maybe_line.value;
    fprintf(stderr, "editor: loaded from %s\n", path);
    return from_text(line);
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "scratch.prn";

    Arena a;
    arena_init(&a);

    Result r = init(&a);
    if (r.tag != 1) { fprintf(stderr, "editor: sdl2 init failed\n"); return 1; }

    Result wr = create_window("PARENA editor -- v0", 900, 200, &a);
    if (wr.tag != 1) { fprintf(stderr, "editor: create-window failed\n"); return 1; }
    Window win = *(Window *)wr.value;

    Result rr = create_renderer(&win, &a);
    if (rr.tag != 1) { fprintf(stderr, "editor: create-renderer failed\n"); return 1; }
    Renderer ren = *(Renderer *)rr.value;

    Result ttfr = ttf_init(&a);
    if (ttfr.tag != 1) { fprintf(stderr, "editor: ttf-init failed\n"); return 1; }

    Result fontr = open_font("/usr/share/fonts/truetype/jetbrains-mono/JetBrainsMono-Regular.ttf", 20, &a);
    if (fontr.tag != 1) { fprintf(stderr, "editor: open-font failed\n"); return 1; }
    Font font = *(Font *)fontr.value;

    Result gr = build_grammar(&a);
    if (gr.tag != 1) { fprintf(stderr, "editor: build-grammar failed\n"); return 1; }
    Vec rules = *(Vec *)gr.value;

    start_text_input();
    Buffer buf = load_first_line(path, &a);

    int running = 1;
    while (running) {
        Option ev;
        while ((ev = poll_event(&a)).tag == 1) {
            EventKind kind = *(EventKind *)ev.value;
            if (kind.tag == EventKind_TAG_Quit) {
                running = 0;
            } else if (kind.tag == EventKind_TAG_KeyDown) {
                int key = *(int *)kind.value;
                if (key == key_backspace()) {
                    Result del = backspace_at_cursor(&buf, &a);
                    if (del.tag == 1) buf = *(Buffer *)del.value;
                } else if (key == key_delete()) {
                    Result del = delete_forward_at_cursor(&buf, &a);
                    if (del.tag == 1) buf = *(Buffer *)del.value;
                } else if (key == key_left()) {
                    buf = move_cursor_left(&buf);
                } else if (key == key_right()) {
                    buf = move_cursor_right(&buf);
                } else if (key == key_home()) {
                    buf = move_cursor_home(&buf);
                } else if (key == key_end()) {
                    buf = move_cursor_end(&buf);
                } else if (key == key_f2()) {
                    save_to_file(path, active_text(&buf), &a);
                } else if (key == key_f3()) {
                    buf = load_first_line(path, &a);
                } else if (key == 27 /* SDLK_ESCAPE -- real, standard "quit" key, no dependency
                                        on <SDL2/SDL.h> being included directly in this file */) {
                    running = 0;
                }
            } else if (kind.tag == EventKind_TAG_TextInput) {
                char *text = (char *)kind.value;
                Result ins = insert_at_cursor(&buf, text, &a);
                if (ins.tag == 1) buf = *(Buffer *)ins.value;
            }
        }

        Result cbg = set_draw_color(&ren, 24, 24, 28, 255, &a);
        (void)cbg;
        render_clear(&ren, &a);

        char *text = active_text(&buf);
        Result hr = render_highlighted_line(&ren, &font, &rules, text, 12, 12, &a);
        (void)hr;

        /* Real cursor: a thin filled rect at the real measured pixel
         * width of the text before the cursor -- proves the buffer's
         * own real cursor-pos is actually driving something visible,
         * not just tracked internally. */
        int cpos = cursor_pos(&buf);
        char *before_cursor = substring(text, 0, cpos, &a);
        int cursor_x = 12 + measure_text_width(&font, before_cursor);
        Result ccol = set_draw_color(&ren, 220, 220, 220, 255, &a);
        (void)ccol;
        render_fill_rect(&ren, cursor_x, 12, 2, 24, &a);

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
