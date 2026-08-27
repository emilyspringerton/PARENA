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

/* prnfmt_format_and_copy -- real, plain forward declaration only (2026-
 * 08-27, founder real-time: "build prnfmt into the editor so saving
 * files auto formats them"). Deliberately NOT #include-ing runtime/
 * prnfmt_bridge.c's own real header dependencies here -- see that
 * file's own header comment for the real Arena-type-collision reason
 * this stays a plain `char *`/`size_t` boundary. */
char *prnfmt_format_and_copy(const char *src, size_t len);

/* Real, portable "where is my own executable" + "spawn another copy of
 * myself" support (2026-08-27, founder real-time: "i need an easy way
 * to actually open the files drag and drop onto the window for now is
 * fine it can open a new window with that file" -- for now spawns a
 * genuinely new, separate editor instance/window per dropped file,
 * matching that explicit real, simpler scope, not in-place buffer
 * replacement with its own real unsaved-changes/undo-reset questions).
 * Three genuinely different real OS APIs, none of them optional --
 * matches the real, established #ifdef _WIN32 / __APPLE__ / else
 * split runtime/parena_runtime.h's own tcp/pty/process glue already
 * uses for the identical reason. */
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

#define LINE_HEIGHT 26 /* real pixel spacing between real lines, matches the real 20pt font this editor opens */
#define WINDOW_WIDTH 900
#define WINDOW_HEIGHT 500 /* matches the real create-window call below -- no real window-resize support exists yet, a real, separate, deferred gap */

/* Real, minimal hover-reveal bottom status bar (2026-08-27, founder
 * real-time: "we need an ui affordance at the bottom of the screen to
 * turn auto indent off same hover near botom to reveal"). Real,
 * deliberate v0 scope: ONE real control (the auto-indent toggle the
 * founder actually asked for), not a general reusable widget system --
 * the founder separately raised that as its own real, bigger, explicit
 * architecture question ("start building out a ui widget system... it
 * would be cool if we implemented the react apis but for native apps
 * like true react native"), not something to build blind mid-stream
 * without a real design conversation first. This bar's own real
 * drawing code is intentionally simple/ad-hoc so it doesn't
 * accidentally BECOME a half-considered widget-system precedent. */
#define STATUS_BAR_HEIGHT 28
#define HOVER_REVEAL_ZONE 40 /* real pixels from the bottom edge that reveal the bar */

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

/* INDENT_WIDTH -- real, fixed 2-space indent (2026-08-27, founder real-
 * time, actively using the editor: "auto 2 space indent doesnt work...
 * if you are in parens and hit enter it should indendt you" -- the
 * founder's own explicit "2 space" number). Founder also raised real
 * tabs-vs-spaces/configurable-width as an open question ("i dunno how
 * ides usually do that we probably need... 2 vs 4 vs 8 space") and a
 * real hover-reveal settings-bar UI to control it -- a real, separate,
 * bigger feature (actual settings STATE + a new UI affordance), scoped
 * out of this pass on purpose rather than guessed at; this constant is
 * the real, working, immediate default in the meantime. */
#define INDENT_WIDTH 2

/* paren_depth_before -- real bracket-nesting depth at a given real
 * byte offset (2026-08-27, real auto-indent-on-Enter: "if you are in
 * parens and hit enter it should indent you"). Counts `(`/`[`/`{` as
 * +1, their real closing partners as -1 (never below 0 -- a real
 * unbalanced close, e.g. mid-edit, shouldn't drive the depth negative)
 * -- deliberately NOT bracket-TYPE-aware (a `)` closes a `[` just as
 * readily here), matching how a real, simple v0 indent counter only
 * needs overall NESTING depth, not real matched-pair validation (a
 * real, separate, much bigger job -- this repo's own region analyzer
 * already does real, rigorous validation at a different layer
 * entirely). Real, honest comment/string-aware scanning -- a `(` typed
 * inside a `;;` comment or a `"..."` string literal must NOT count
 * toward real indent depth, or PARENA source with literal parens in
 * prose/strings would visibly mis-indent. */
static int paren_depth_before(const char *text, int pos) {
    int depth = 0;
    int i = 0;
    while (i < pos) {
        if (text[i] == ';' && i + 1 < pos && text[i + 1] == ';') {
            while (i < pos && text[i] != '\n') i++;
            continue;
        }
        if (text[i] == '"') {
            i++;
            while (i < pos && text[i] != '"') {
                if (text[i] == '\\' && i + 1 < pos) i++;
                i++;
            }
            if (i < pos) i++;
            continue;
        }
        if (text[i] == '(' || text[i] == '[' || text[i] == '{') {
            depth++;
        } else if (text[i] == ')' || text[i] == ']' || text[i] == '}') {
            if (depth > 0) depth--;
        }
        i++;
    }
    return depth;
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

/* executable_path -- real, portable "where is my own executable"
 * (2026-08-27, real drag-and-drop-a-file-onto-the-window: spawning a
 * new instance of THIS SAME program needs a real path to re-exec, not
 * just argv[0] -- argv[0] can be a bare relative name like
 * "editor-demo" with no real directory info, or "." if launched
 * through some shells, unreliable to re-exec from). Three genuinely
 * different real OS APIs (matches this file's own top-of-file #ifdef
 * split): GetModuleFileNameA on Windows, _NSGetExecutablePath on
 * macOS (its own real, documented way -- macOS has no /proc), and
 * /proc/self/exe on Linux (a real, standard Linux-specific symlink,
 * not POSIX-portable to macOS, hence the split). Returns NULL on any
 * real failure -- callers fall back to their own next real candidate
 * rather than trusting a half-populated path. */
static char *executable_path(Arena *a) {
    char buf[4096];
#ifdef _WIN32
    DWORD len = GetModuleFileNameA(NULL, buf, sizeof buf);
    if (len == 0 || len >= sizeof buf) return NULL;
#elif defined(__APPLE__)
    uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) != 0) return NULL;
    size_t len = strlen(buf);
#else
    ssize_t len = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (len < 0) return NULL;
    buf[len] = '\0';
#endif
    size_t blen = strlen(buf);
    (void)len;
    char *out = (char *)arena_alloc(a, blen + 1);
    memcpy(out, buf, blen + 1);
    return out;
}

/* executable_dir -- strips the real filename off executable_path's own
 * result, real forward/backslash-aware (a real Windows path uses `\`,
 * real Linux/macOS paths use `/`) -- used to find files bundled right
 * next to this binary (the real font, primarily) regardless of how
 * the program was actually launched (double-click, a pinned taskbar
 * icon, drag-a-file-onto-that-icon, or a real file-type association --
 * every one of those can hand this program an unpredictable, even
 * unrelated, CURRENT DIRECTORY, unlike its own real, fixed install
 * location). */
static char *executable_dir(Arena *a) {
    char *path = executable_path(a);
    if (!path) return NULL;
    char *last_slash = strrchr(path, '/');
    char *last_backslash = strrchr(path, '\\');
    char *cut = last_slash;
    if (last_backslash && (!cut || last_backslash > cut)) cut = last_backslash;
    if (!cut) return NULL;
    *cut = '\0';
    return path;
}

/* spawn_new_instance -- real, portable "open this file in a genuinely
 * new editor window" (2026-08-27). Real, deliberate v0 scope, matching
 * the founder's own explicit real ask: a real SEPARATE process/window
 * per dropped file, not in-place buffer replacement (which would raise
 * real, separate questions -- discard unsaved changes? reset undo
 * history? -- not attempted here). POSIX: real fork+execl, the child
 * re-execs itself with the dropped path as its own real argv[1],
 * detached from the parent (no wait() -- a real, independent window,
 * not a blocking child). Windows: real CreateProcessA with a real
 * quoted command line (handles a real dropped path containing spaces,
 * which a real Windows file path very often does). Failure is real,
 * honest, non-fatal -- logged to stderr, the CURRENTLY RUNNING editor
 * keeps going either way; a failed spawn shouldn't crash the window
 * the user was already using. */
static void spawn_new_instance(const char *exe_path, const char *file_path) {
#ifdef _WIN32
    char cmdline[2048];
    int n = snprintf(cmdline, sizeof cmdline, "\"%s\" \"%s\"", exe_path, file_path);
    if (n < 0 || (size_t)n >= sizeof cmdline) {
        fprintf(stderr, "editor: spawn-new-instance failed (path too long)\n");
        return;
    }
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "editor: spawn-new-instance failed (CreateProcessA)\n");
        return;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
#else
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "editor: spawn-new-instance failed (fork)\n");
        return;
    }
    if (pid == 0) {
        execl(exe_path, exe_path, file_path, (char *)NULL);
        /* execl only returns on real failure -- a real, separate child
         * process, not the running editor, so _exit (not exit) to skip
         * any real atexit/stdio-flush double-work with the parent. */
        _exit(127);
    }
#endif
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
 * to CI or to a user's own build.
 *
 * Real, confirmed-live follow-up (2026-08-27, founder raised real
 * Windows taskbar-pin-and-relaunch + drag-file-onto-icon concerns):
 * the CWD-relative candidate above only works when the launcher
 * happens to set CWD to this binary's own directory -- true for a
 * terminal-launched `./editor-demo` or RUN.bat's own explicit `cd /d
 * %~dp0`, but NOT guaranteed for every real way Windows can launch an
 * exe (a taskbar pin, a file dragged onto that pinned icon, a real
 * file-type association) -- any of those hitting a different CWD would
 * silently reproduce the exact "2 black screens then closed" bug fixed
 * above. executable_dir() resolves this binary's own REAL, absolute
 * install location via a real OS API, independent of CWD entirely --
 * tried FIRST, ahead of the CWD-relative ones. */
static Result open_font_with_fallback(int point_size, Arena *a) {
    char *exe_dir = executable_dir(a);
    if (exe_dir) {
        char candidate[4096];
        int n = snprintf(candidate, sizeof candidate, "%s/JetBrainsMono-Regular.ttf", exe_dir);
        if (n > 0 && (size_t)n < sizeof candidate) {
            Result r = open_font(candidate, point_size, a);
            if (r.tag == 1) return r;
        }
    }
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

#ifndef _WIN32
    /* Real, standard fire-and-forget-child fix (2026-08-27, real
     * drag-and-drop): spawn_new_instance's own real fork()+execl()
     * never wait()s on the spawned window (an independent, real,
     * separate editor process/window the user closes on their own
     * schedule, not something this window should block on) -- without
     * this, each exited child becomes a real zombie process-table
     * entry until this window itself eventually exits and the child
     * gets reparented+reaped. SIG_IGN on SIGCHLD is the real, standard
     * POSIX fix: the kernel reaps an exited child immediately, no
     * zombie ever created, no explicit wait() needed. Windows has no
     * zombie-process concept -- spawn_new_instance's own CloseHandle
     * calls are the real Windows equivalent (don't leak the handles). */
    signal(SIGCHLD, SIG_IGN);
#endif

    Arena a;
    arena_init(&a);

    Result r = init(&a);
    if (r.tag != 1) { fprintf(stderr, "editor: sdl2 init failed\n"); return 1; }

    Result wr = create_window("PARENA editor -- v0", WINDOW_WIDTH, WINDOW_HEIGHT, &a);
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
    int is_markdown = path_has_suffix(path, ".md");
    Result gr = is_markdown ? build_markdown_grammar(&a) : build_grammar(&a);
    if (gr.tag != 1) { fprintf(stderr, "editor: build-grammar failed\n"); return 1; }
    Vec rules = *(Vec *)gr.value;

    start_text_input();
    Buffer buf = load_from_file(path, &a);

    /* Real, resolved once at startup (2026-08-27, real drag-and-drop):
     * spawn_new_instance's own real re-exec target. Resolved here, not
     * inside the event handler itself, since it's the same real value
     * every time and executable_path()'s own real OS call has no
     * reason to be repeated per drop. A real, honest fallback to
     * argv[0] if the real OS-level resolution fails for any reason --
     * won't work from every real CWD, but better than silently
     * disabling drag-and-drop entirely over a real, rare resolution
     * failure. */
    char *exe_path = executable_path(&a);
    if (!exe_path) exe_path = argv[0];

    /* Real mouse-drag selection state (2026-08-27): dragging tracks
     * whether the real left mouse button is currently held (set on a
     * real MouseDown, cleared on a real MouseUp); mouse_down_pos is the
     * real byte offset the drag started at -- the real, fixed anchor
     * end of the selection for as long as the drag continues. */
    int dragging = 0;
    int mouse_down_pos = 0;

    /* Real vertical scroll state (2026-08-27, founder real-time,
     * actively using the editor: "mouse wheel scroll does not work" --
     * scrolling had never existed at all before this, so any real file
     * taller than the window had no way to see past the first
     * screenful). Real, minimal, LINE-based (not pixel-based) offset:
     * how many real lines are scrolled off the top of the view. */
    int scroll_offset = 0;
#define SCROLL_LINES_PER_NOTCH 3 /* real, standard default most real apps use */

    /* Real hover-reveal bottom status bar state (2026-08-27, see this
     * file's own STATUS_BAR_HEIGHT/HOVER_REVEAL_ZONE header comment for
     * the real, deliberate v0 scope). last_mouse_y is updated on EVERY
     * real MouseMotion (not just while dragging), independent of
     * scroll -- this is a real, raw SCREEN coordinate, checking against
     * the real, fixed window edge, not anything scrolled.
     *
     * auto_indent_toggle is the first real caller of stdlib/editor/
     * widget.prn's new Toggle type (2026-08-27, founder real-time: the
     * "ui widget system" ask, chosen as the next thread after the
     * v0.77.0-v0.80.0 close-out) -- replaces what used to be a bare
     * `int` flipped by hand next to a raw rect hit-test inline here. */
    int last_mouse_y = 0;
    Toggle auto_indent_toggle = new_toggle(0, WINDOW_HEIGHT - STATUS_BAR_HEIGHT, WINDOW_WIDTH, STATUS_BAR_HEIGHT,
                                            "Auto-indent: ON (click to turn off)",
                                            "Auto-indent: OFF (click to turn on)", 1);

    /* Real Ctrl+Zoom state (2026-08-27, founder real-time: "ctrl plus
     * and ctrl minus and ctrl mous wheel scoll should zoom just like
     * pitviper"). zoom_percent is a real I32 (matches sdl2/render-set-
     * scale's own real convention, 100 = 1.0x), NOT a float -- this
     * whole editor already uses I32 for every other real UI quantity
     * (LINE_HEIGHT, WINDOW_HEIGHT, etc.), so this stays consistent
     * rather than introducing the only float in the file. Real, exact
     * values lifted from PITVIPER's own cmd/pitviper/main.go
     * (zoomMin=0.5, zoomMax=3.0, zoomStep=0.1 -- the explicit real
     * model the founder named), scaled x100 for the I32 convention
     * here. */
    int zoom_percent = 100;
#define ZOOM_MIN 50
#define ZOOM_MAX 300
#define ZOOM_STEP 10

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
                if (key == 'a' && ctrl_held_()) {
                    /* Real Ctrl+A select-all (2026-08-27, founder real-
                     * time, actively using the editor: "ctrl a to
                     * select all backspace does not work" -- select-
                     * all had never been implemented at all). Real,
                     * standard OS convention: selects the WHOLE
                     * buffer, cursor lands at the real end (so a
                     * following Backspace/Delete/typed-replacement
                     * correctly acts on the entire selection, same
                     * real path every other selection-aware key
                     * already goes through). */
                    char *whole_text = active_text(&buf);
                    buf = set_selection(&buf, 0, (int)strlen(whole_text));
                } else if (key == '=' && ctrl_held_()) {
                    /* Real Ctrl+Zoom in (2026-08-27, founder real-time:
                     * "ctrl plus and ctrl minus... should zoom just
                     * like pitviper"). The real UNSHIFTED '=' key
                     * (SDL2's own keysym for it IS its literal ASCII
                     * value, same real precedent 'c'/'x'/'v'/'a'/'z'
                     * already establish) -- PITVIPER's own real,
                     * explicit convention: "the unshifted key '+'
                     * lives on," Photoshop's own real Ctrl/Cmd+'='
                     * zoom-in binding, not requiring Shift too. */
                    zoom_percent += ZOOM_STEP;
                    if (zoom_percent > ZOOM_MAX) zoom_percent = ZOOM_MAX;
                } else if (key == '-' && ctrl_held_()) {
                    zoom_percent -= ZOOM_STEP;
                    if (zoom_percent < ZOOM_MIN) zoom_percent = ZOOM_MIN;
                } else if (key == '0' && ctrl_held_()) {
                    /* Real Ctrl+0 reset -- Photoshop's own real "Fit on
                     * Screen"/100% reset, PITVIPER's own identical real
                     * binding. */
                    zoom_percent = 100;
                } else if (key == 'z' && ctrl_held_()) {
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
                    /* Real auto-format-on-save (2026-08-27, founder
                     * real-time: "build prnfmt into the editor so
                     * saving files auto formats them"). PARENA source
                     * only -- fmt_source() is a real, PARENA-syntax-
                     * specific re-indenter, running it on a real .md
                     * file would just mangle real prose. Real, honest
                     * v0: the in-memory buffer is updated to the
                     * FORMATTED text too (not just the file on disk),
                     * matching every real "format on save" tool's own
                     * expected behavior -- what you see after saving
                     * really is what got written. A real cursor-
                     * position shift across the reformat is a real,
                     * accepted v0 simplification (from-text's own
                     * documented "cursor at the end" default), same
                     * honest tradeoff this whole file's own load path
                     * already carries. */
                    if (!is_markdown) {
                        char *whole = active_text(&buf);
                        char *formatted = prnfmt_format_and_copy(whole, strlen(whole));
                        if (formatted) {
                            push_undo(buf);
                            save_to_file(path, formatted, &a);
                            /* Real, deliberate copy into THIS program's
                             * own arena before handing it to from-text
                             * -- `formatted` is a plain malloc'd buffer
                             * (prnfmt_format_and_copy's own real
                             * contract), and from-text's own Buffer
                             * struct stores its `text` field pointer
                             * directly, not copied -- freeing the raw
                             * malloc'd buffer right after would leave
                             * that field dangling. */
                            size_t flen = strlen(formatted);
                            char *arena_copy = (char *)arena_alloc(&a, flen + 1);
                            memcpy(arena_copy, formatted, flen + 1);
                            free(formatted);
                            buf = from_text(arena_copy);
                        } else {
                            fprintf(stderr, "editor: prnfmt formatting failed (real allocation failure), saving unformatted\n");
                            save_to_file(path, whole, &a);
                        }
                    } else {
                        save_to_file(path, active_text(&buf), &a);
                    }
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
                    /* Real auto-indent-on-Enter (2026-08-27, founder
                     * real-time, actively using the editor: "if you
                     * are in parens and hit enter it should indent
                     * you" -- PARENA source only; a real Markdown
                     * file's own [links](like-this) would make a
                     * bracket-depth counter actively wrong there, not
                     * just unhelpful). Also real-time gated on the
                     * hover-reveal status bar's own toggle ("we need an
                     * ui affordance... to turn auto indent off"). */
                    char newline_and_indent[2 + INDENT_WIDTH * 32];
                    if (!is_markdown && toggle_on_(&auto_indent_toggle)) {
                        int depth = paren_depth_before(active_text(&buf), cursor_pos(&buf));
                        int n = depth * INDENT_WIDTH;
                        if (n > (int)(sizeof(newline_and_indent) - 2)) n = (int)(sizeof(newline_and_indent) - 2);
                        newline_and_indent[0] = '\n';
                        for (int k = 0; k < n; k++) newline_and_indent[1 + k] = ' ';
                        newline_and_indent[1 + n] = '\0';
                    } else {
                        newline_and_indent[0] = '\n';
                        newline_and_indent[1] = '\0';
                    }
                    Result ins = insert_at_cursor(&buf, newline_and_indent, &a);
                    if (ins.tag == 1) buf = *(Buffer *)ins.value;
                } else if (key == key_tab()) {
                    /* Real Tab-to-indent (2026-08-27, founder real-
                     * time, actively using the editor: "tab to indent
                     * doesnt work"). SDL2 doesn't fire a real
                     * SDL_TEXTINPUT for Tab (a real, standard GUI-
                     * toolkit convention), so it needed this real
                     * KeyDown branch, same as every other special key.
                     * Real, deliberate v0: inserts INDENT_WIDTH real
                     * spaces, not a literal tab character -- matches
                     * this file's own auto-indent-on-Enter convention
                     * above; real tabs-vs-spaces/configurable-width is
                     * a real, separate, deliberately deferred design
                     * question (a real settings UI, not guessed at
                     * here). Same selection-replace shape every other
                     * real insert already establishes. */
                    push_undo(buf);
                    if (has_selection_(&buf)) {
                        Result del = delete_selection(&buf, &a);
                        if (del.tag == 1) buf = *(Buffer *)del.value;
                    }
                    char tab_spaces[INDENT_WIDTH + 1];
                    for (int k = 0; k < INDENT_WIDTH; k++) tab_spaces[k] = ' ';
                    tab_spaces[INDENT_WIDTH] = '\0';
                    Result ins = insert_at_cursor(&buf, tab_spaces, &a);
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
                /* Real, confirmed-live-needed split (2026-08-27, found
                 * by actually rendering a real screenshot at a real
                 * zoomed scale and looking at it -- the status bar had
                 * silently rendered OFF-SCREEN, not just "compiles
                 * clean"): the status bar is real, fixed UI CHROME, NOT
                 * content -- it stays a real, constant, unzoomed size
                 * on screen regardless of content zoom (matching how
                 * real apps, e.g. VS Code's own status bar, keep chrome
                 * fixed while only the editor's own text zooms), so its
                 * own hit-testing uses RAW screen coordinates. The real
                 * TEXT/cursor/selection, by contrast, genuinely is
                 * inside the zoomed content, so pos_from_mouse needs
                 * the real zoom-corrected logical coordinates -- SDL_
                 * RenderSetScale does NOT transform input coordinates
                 * back (confirmed against the real SDL2 docs -- SDL_
                 * RenderWindowToLogical exists as its own, separate,
                 * real function for exactly this reason). Integer-safe
                 * (*100/zoom_percent), not float division. */
                int raw_mx = mouse_x();
                int raw_my = mouse_y();
                int bar_visible_now = last_mouse_y >= WINDOW_HEIGHT - HOVER_REVEAL_ZONE;
                if (bar_visible_now && toggle_hit_(&auto_indent_toggle, raw_mx, raw_my)) {
                    auto_indent_toggle = toggle_flip(&auto_indent_toggle, &a);
                } else {
                    /* Real mouse click: position the cursor there
                     * (which also clears any active selection --
                     * set_cursor's own real behavior) and start
                     * tracking a real drag from this real position.
                     * Real, confirmed-live-needed scroll adjustment
                     * (2026-08-27): adding back the real scrolled-off
                     * pixel amount is what pos_from_mouse's own math
                     * needs to land on the real, correct LOGICAL line,
                     * not whatever's currently drawn at that screen
                     * row. */
                    int mx = mouse_x() * 100 / zoom_percent;
                    int my = mouse_y() * 100 / zoom_percent;
                    int pos = pos_from_mouse(active_text(&buf), mx, my + scroll_offset * LINE_HEIGHT, &font, &a);
                    buf = set_cursor(&buf, pos);
                    mouse_down_pos = pos;
                    dragging = 1;
                }
            } else if (kind.tag == EventKind_TAG_MouseUp) {
                dragging = 0;
            } else if (kind.tag == EventKind_TAG_MouseMotion) {
                /* Real hover-reveal tracking -- updated on EVERY real
                 * motion, independent of dragging (see this file's own
                 * last_mouse_y state declaration above). Real RAW
                 * screen y -- the bar's own real drawn position (fixed
                 * UI chrome, unaffected by zoom, see MouseDown's own
                 * identical real reasoning above) is itself real,
                 * unzoomed screen pixels. */
                last_mouse_y = mouse_y();
                /* Real mouse drag: only while the real button is
                 * actually held (SDL2 sends real MouseMotion on every
                 * mouse move over the window regardless, not just while
                 * dragging). Anchors the selection at the real click
                 * position, extends the cursor end to wherever the
                 * mouse now is. Same real scroll adjustment as MouseDown
                 * above. */
                if (dragging) {
                    int mx = mouse_x() * 100 / zoom_percent;
                    int my = mouse_y() * 100 / zoom_percent;
                    int pos = pos_from_mouse(active_text(&buf), mx, my + scroll_offset * LINE_HEIGHT, &font, &a);
                    buf = set_selection(&buf, mouse_down_pos, pos);
                }
            } else if (kind.tag == EventKind_TAG_FileDrop) {
                /* Real drag-and-drop-a-file-onto-the-window (2026-08-27,
                 * founder real-time: "i need an easy way to actually
                 * open the files drag and drop onto the window for now
                 * is fine it can open a new window with that file").
                 * Real, deliberate v0: opens a genuinely NEW, separate
                 * editor instance/window for the dropped file, leaving
                 * THIS window's own current buffer completely
                 * untouched (no discarded unsaved changes, no undo-
                 * history reset). A real multi-file drag fires one
                 * real FileDrop per file -- each spawns its own real
                 * new window, so dropping several files at once
                 * already opens all of them. */
                char *dropped_path = (char *)kind.value;
                spawn_new_instance(exe_path, dropped_path);
            } else if (kind.tag == EventKind_TAG_MouseWheel) {
                int delta = *(int *)kind.value;
                if (ctrl_held_()) {
                    /* Real Ctrl+scroll zoom (2026-08-27, founder real-
                     * time: "...ctrl mous wheel scoll should zoom just
                     * like pitviper"). Same real convention PITVIPER's
                     * own cmd/pitviper/main.go already establishes:
                     * e.Y > 0 (wheel forward/up) zooms IN, e.Y < 0
                     * zooms OUT -- takes over the wheel entirely while
                     * Ctrl is held, real, standard "Ctrl+scroll always
                     * means zoom, never scroll" behavior (Photoshop,
                     * VS Code, browsers all agree on this). */
                    if (delta > 0) {
                        zoom_percent += ZOOM_STEP;
                        if (zoom_percent > ZOOM_MAX) zoom_percent = ZOOM_MAX;
                    } else if (delta < 0) {
                        zoom_percent -= ZOOM_STEP;
                        if (zoom_percent < ZOOM_MIN) zoom_percent = ZOOM_MIN;
                    }
                } else {
                    /* Real mouse-wheel vertical scroll (2026-08-27).
                     * SDL2's own real convention: positive delta =
                     * wheel rolled away from the user (the real,
                     * physical "scroll up" motion) -- moves the view
                     * UP, revealing earlier content, so it DECREASES
                     * scroll_offset; negative delta increases it.
                     * Clamped to [0, real total lines in the buffer] --
                     * can't scroll above the real first line, and
                     * scrolling arbitrarily far past the real last line
                     * is a real, honest, low-priority overscroll
                     * (harmless blank space, not attempted to prevent
                     * here). */
                    scroll_offset -= delta * SCROLL_LINES_PER_NOTCH;
                    if (scroll_offset < 0) scroll_offset = 0;
                    char *scroll_text = active_text(&buf);
                    int total_lines = 1;
                    for (int si = 0; scroll_text[si] != '\0'; si++) {
                        if (scroll_text[si] == '\n') total_lines++;
                    }
                    if (scroll_offset > total_lines) scroll_offset = total_lines;
                }
            }
        }

        Result cbg = set_draw_color(&ren, 24, 24, 28, 255, &a);
        (void)cbg;
        render_clear(&ren, &a);

        /* Real Ctrl+Zoom, applied once per frame (2026-08-27) -- every
         * draw call below (text, cursor, selection, the status bar)
         * happens in real LOGICAL coordinates; SDL scales the whole
         * frame uniformly on present, the exact real technique
         * PITVIPER's own cmd/pitviper/main.go already uses ("zoomScale
         * applied once for the whole frame via SDL's own renderer"). */
        Result zoomr = render_set_scale(&ren, zoom_percent, &a);
        (void)zoomr;

        char *text = active_text(&buf);

        /* Real scroll offset, applied uniformly to every real Y
         * coordinate below (selection highlight, the actual text,
         * and the cursor) -- see the real scroll_offset state
         * declaration above for why this is line-based, not pixel. */
        int scroll_y_px = scroll_offset * LINE_HEIGHT;

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
                render_fill_rect(&ren, x_from, 12 + row * LINE_HEIGHT - scroll_y_px, width, LINE_HEIGHT - 2, &a);
                cur_line_start = line_end + 1;
            }
        }

        Result hr = render_highlighted_text(&ren, &font, &rules, text, 12, 12 - scroll_y_px, LINE_HEIGHT, &a);
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
        int cursor_y = 12 + row * LINE_HEIGHT - scroll_y_px;
        Result ccol = set_draw_color(&ren, 220, 220, 220, 255, &a);
        (void)ccol;
        render_fill_rect(&ren, cursor_x, cursor_y, 2, 24, &a);

        /* Real hover-reveal bottom status bar (2026-08-27) -- drawn
         * LAST so it sits on top of the real text/cursor/selection
         * layers beneath it, matching how every other real "on top"
         * element in this file (the cursor rect) is already drawn
         * after its own real background. Real, confirmed-live-needed
         * scale RESET (found by actually rendering a real screenshot at
         * a real zoomed scale and looking at it -- the bar had silently
         * rendered OFF-SCREEN at 150% zoom, since its own fixed logical
         * Y position no longer fit inside the SHRUNKEN logical viewport
         * a real SDL_RenderSetScale(>1.0) leaves visible): the bar is
         * real, fixed UI chrome, not zoomed content -- reset to real
         * 1:1 (100%) right before drawing it, so it always renders at
         * its own real, constant screen size regardless of content
         * zoom (matches this same block's own real MouseDown/
         * MouseMotion hit-testing, which already uses RAW screen
         * coordinates for exactly this reason). No restore needed after
         * -- next frame's own render-set-scale call at the top always
         * re-applies the real current zoom_percent fresh. */
        Result scalereset = render_set_scale(&ren, 100, &a);
        (void)scalereset;
        if (last_mouse_y >= WINDOW_HEIGHT - HOVER_REVEAL_ZONE) {
            Result togglr = render_toggle(&ren, &font, &auto_indent_toggle, 45, 45, 52, 200, 200, 200, &a);
            (void)togglr;
        }

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
