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
 * Real NERDTree-style file-tree sidebar (2026-08-27, same day, founder:
 * "we are going to need to do the nerd tree style implementation to
 * display a tree of files"): lists the real current working directory
 * (one flat level, via the new stdlib/io.prn list-dir), toggled via a
 * second real Toggle widget in the status bar, clicking an entry opens
 * it in a real new window (same spawn_new_instance drag-and-drop
 * already uses) -- see this file's own SIDEBAR_WIDTH header comment
 * for the full real, honest v0 scope.
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
#include <sys/stat.h>

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
#define WINDOW_WIDTH 1040 /* widened from 900 (2026-08-27) to fit a real
                            fourth bottom-bar toggle (terminal_toggle,
                            800-1020) alongside auto-indent/file-tree/
                            settings -- WINDOW_WIDTH has exactly one
                            other real use in this file (create_window
                            below); every bottom-bar toggle already uses
                            fixed screen x-positions, not a WINDOW_WIDTH-
                            relative layout, so nothing else depends on
                            this number. */
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

/* Real, minimal NERDTree-style file-tree sidebar (2026-08-27, founder
 * real-time: "we are going to need to do the nerd tree style
 * implementation to display a tree of files" -- picked as the next
 * real increment continuing this session's own PARENA editor thread,
 * building on the just-shipped Toggle widget for its own visibility
 * control). Real, honest v0 scope: lists the real CURRENT WORKING
 * DIRECTORY (not the open file's own directory -- a real, separate,
 * deferred refinement) ONE FLAT LEVEL (files and subdirectories both,
 * no expand/collapse -- stdlib/io.prn's own list-dir is itself
 * documented as one flat level only), fixed real screen width
 * regardless of content zoom (same "fixed UI chrome" treatment the
 * status bar already established), clicking any entry spawns a real
 * new editor window for it via the same spawn_new_instance() drag-and-
 * drop already uses -- clicking a SUBDIRECTORY entry currently opens
 * it as if it were a file (load_from_file's own real, honest failure
 * path: an empty scratch buffer, not a crash -- real recursive tree
 * expansion is separate, deferred UI work). Directory/source TABS (the
 * founder's own separate, related ask, "tabs to switch between the
 * current directory open vs the source of the editor") stays deferred
 * -- depends on this file tree existing first. */
#define SIDEBAR_WIDTH 180
#define FILE_TREE_ROW_HEIGHT 20

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

/* recompute_spotlight -- real, explicit-args wrapper around
 * run-providers (2026-08-27), since spotlight_query/spotlight_results
 * are real, mutable local state inside main() -- no closures in this
 * dialect, so the recompute logic takes them as plain arguments
 * instead of capturing them. Called whenever the real query text
 * actually changes (opened, typed into, backspaced), not every
 * frame. */
static Vec recompute_spotlight(const char *root, const char *query, Arena *a) {
    return run_providers((char *)query, (char *)root, a);
}

/* spotlight_keep_selection_visible -- real, new (2026-08-27, founder
 * real-time: "ctrl t needs to scroll when you do down and it scrolls
 * past the view"): called after every real change to the selected
 * row, adjusts the real scroll offset (by reference) just enough to
 * bring the selection back inside [offset, offset+max_visible-1] --
 * scrolls DOWN by the minimum amount when the selection moved past the
 * bottom of the visible window, UP by the minimum amount when it moved
 * above the top (Up from row 0 of the current window). Takes
 * max_visible as a real parameter rather than reading the SPOTLIGHT_
 * MAX_VISIBLE_ROWS macro directly -- that's defined inside main() itself,
 * after this function, so referencing it here isn't textually possible. */
static void spotlight_keep_selection_visible(int selected, int *scroll_offset, int max_visible) {
    if (selected < *scroll_offset) {
        *scroll_offset = selected;
    } else if (selected >= *scroll_offset + max_visible) {
        *scroll_offset = selected - max_visible + 1;
    }
}

/* looks_like_binary -- real, minimal, standard "is this text" heuristic
 * (2026-08-27, real bug found live: founder clicked a real .exe in the
 * file-tree sidebar, "it just says like MZ" -- the real DOS/PE
 * executable magic header, rendered as raw garbage bytes since
 * load_from_file had no binary detection at all, same real gap every
 * text editor has to close). Same real, standard "a NUL byte in the
 * first N bytes means binary" check every real editor (vim, VS Code,
 * git's own diff heuristic) uses -- genuine UTF-8/ASCII text never
 * legitimately contains a raw NUL byte; a compiled binary, image, or
 * archive almost always does within its first few hundred bytes. Real,
 * honest, narrow: doesn't attempt real encoding detection (UTF-16,
 * etc.) or a full binary/text classifier -- just the one, cheap,
 * standard signal that actually matters for "don't render an .exe as
 * garbage text." */
/* Real, confirmed-live bug in an earlier draft of this same fix, caught
 * before shipping: read-string's own return value is a real, plain,
 * NUL-terminated C string -- if the real on-disk file has an EMBEDDED
 * NUL byte partway through (the common case for a real binary), the
 * string implicitly ends right there; strlen() on it can never then
 * "find" that same NUL within [0, strlen) by definition, since
 * everything up to strlen is exactly the bytes that AREN'T NUL. So the
 * real, correct check isn't scanning the (already-truncated) string
 * for a NUL -- it's comparing the string's own real length against the
 * REAL on-disk file size: they only differ when an embedded NUL cut
 * the string short. */
static int looks_like_binary(const char *path, const char *text) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (size_t)st.st_size != strlen(text);
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
    if (looks_like_binary(path, text)) {
        fprintf(stderr, "editor: %s looks like a binary file -- not opening it as text\n", path);
        return from_text("(this looks like a binary file -- PARENA editor does not open binary files)\n");
    }
    fprintf(stderr, "editor: loaded from %s\n", path);
    return from_text(text);
}

/* parent_dir_of -- real, minimal host-driver plumbing for the file-tree
 * sidebar's own real directory navigation (2026-08-27, closing v0.83.0's
 * own honest "clicking a subdirectory opens it as a file" gap). Strips
 * the last '/'-delimited path segment off `dir`. Real, DELIBERATELY
 * bounded scope, not a general path-normalization routine: "." maps to
 * "..", and stripping a segment off a path with no '/' at all (which
 * only "..", ".", or a truly root-relative single name can be, given
 * every OTHER path this file ever builds is produced by this same
 * function or by snprintf-ing "%s/%s" onto an existing one) falls back
 * to ".". This means going up from "." twice in a row lands back on
 * "." rather than genuinely escaping two levels above the real launch
 * CWD via a real ".." chain (".." has no '/' in it either, so its own
 * "parent" is the same "." fallback) -- a real, deliberate, safe,
 * DOCUMENTED cap (bounces between "." and ".." rather than mis-parsing
 * ".."-chains into the wrong directory), not a bug nobody noticed;
 * real, unbounded multi-level upward traversal is separate, deferred
 * work (a real path-normalization pass, not attempted here). */
static char *parent_dir_of(const char *dir, Arena *a) {
    if (strcmp(dir, ".") == 0) {
        char *out = (char *)arena_alloc(a, 3);
        memcpy(out, "..", 3);
        return out;
    }
    int len = (int)strlen(dir);
    int last_slash = -1;
    for (int i = len - 1; i >= 0; i--) {
        if (dir[i] == '/') { last_slash = i; break; }
    }
    if (last_slash < 0) {
        char *out = (char *)arena_alloc(a, 2);
        out[0] = '.'; out[1] = '\0';
        return out;
    }
    if (last_slash == 0) {
        /* A real absolute path one level below the real root (e.g.
         * "/home") -- last_slash==0 means the ONLY '/' is the leading
         * one, so the real parent is the root itself, not the "."
         * fallback above (which is for a path with NO '/' at all --
         * genuinely unreachable from THIS editor's own real
         * file_tree_dir state space, which only ever holds "." or a
         * "./..."-prefixed path, but this function reads as general-
         * purpose, so it's handled correctly regardless). */
        char *out = (char *)arena_alloc(a, 2);
        out[0] = '/'; out[1] = '\0';
        return out;
    }
    char *out = (char *)arena_alloc(a, (size_t)last_slash + 1);
    memcpy(out, dir, (size_t)last_slash);
    out[last_slash] = '\0';
    return out;
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
 * host-driver code shouldn't assume) clamps to the real start.
 *
 * x_origin -- real, added 2026-08-27 (file-tree sidebar): the real
 * logical x where text content actually starts, shifted right of the
 * plain 12px margin by the sidebar's own real width when it's visible
 * (see this file's own text_x_origin computation at each call site) --
 * without this, every click would still measure from the old fixed
 * margin even though the text itself visibly starts further right. */
static int pos_from_mouse(char *text, int mouse_x, int mouse_y, int x_origin, Font *font, Arena *a) {
    int len = (int)strlen(text);
    int target_row = (mouse_y - 12) / LINE_HEIGHT;
    if (target_row < 0) target_row = 0;

    int line_start = 0, row = 0;
    for (int i = 0; i < len; i++) {
        if (row == target_row) break;
        if (text[i] == '\n') { row++; line_start = i + 1; }
    }
    int line_end = line_end_from(text, line_start);

    int target_x = mouse_x - x_origin;
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

    /* frame_arena -- real, confirmed-live fix (2026-08-27, founder
     * real-time: opening a large file "still crashes... read some
     * into memory but not too much... scan or whatever?", and
     * separately "tryna open a md file... it just beechballed").
     * Directly measured via a real Xvfb repro + a debug arena-block
     * counter (not guessed): even after windowing the actual render
     * call to just the visible line range (see window_start/
     * window_end below), the render section still allocated ~150-175
     * fresh 64KB arena blocks (~10-11MB) in the first 60 frames alone
     * -- and, critically, the SAME order of magnitude for a tiny
     * 20-line plain-text file as for the real 58k-line construct
     * file, proving the cost is "re-tokenize + re-render every single
     * frame regardless of whether anything changed," not something
     * that scales with file size at all. `a` is shared by everything
     * genuinely persistent (the buffer's own text, undo/redo stacks,
     * spotlight results, file-tree entries, ...) and NEVER freed
     * mid-run by design (this whole program's own bump-allocator
     * model) -- correct for state that must survive across frames,
     * wrong for pure per-frame drawing garbage (a tokenized Vec of
     * Tokens, a handful of Result wrappers) that's produced and
     * discarded 60 times a second forever. frame_arena is reset
     * (arena_free_all + arena_init) at the top of every single frame
     * (see the real event loop below) and used ONLY for the render
     * section's own ephemeral calls (never for anything the next
     * frame, or any later frame, needs to still be valid) -- bounds
     * real per-frame memory to roughly one frame's own drawing cost
     * instead of every frame ever rendered accumulating forever. A
     * real, separate, deferred follow-up remains: WHY tokenization
     * costs this much per byte at all (an actual glyph/token cache
     * across frames, not just a bounded-and-freed one) needs real
     * SDL2 render-to-texture host glue that doesn't exist in this
     * stdlib yet (sdl2.prn's own header comment already documents
     * "render-text is deliberately NOT a glyph-atlas/texture-cache
     * system" as a stated v0 tradeoff) -- out of scope here; this fix
     * closes the actual reported crash (unbounded growth), not the
     * separate, now-harmless-since-bounded CPU cost. */
    Arena frame_arena;
    arena_init(&frame_arena);

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

    /* Real, separate scroll state for the file-tree sidebar (2026-08-27,
     * founder real-time: "nerd tree needs to scroll with mouse wheel if
     * im hovering over nerd tree vs hovering over text editor"). Same
     * real line-count-offset shape scroll_offset above already
     * establishes, just for the sidebar's own real row list instead of
     * the code buffer's own real lines. Reset to 0 on real directory
     * navigation (both the ".." and into-a-subdirectory real MouseDown
     * branches below) -- a freshly-listed directory should always open
     * scrolled to its own real top, not wherever the PREVIOUS
     * directory's own scroll happened to land. */
    int file_tree_scroll_offset = 0;

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
    Toggle auto_indent_toggle = new_toggle(0, WINDOW_HEIGHT - STATUS_BAR_HEIGHT, 340, STATUS_BAR_HEIGHT,
                                            "Auto-indent: ON (click to turn off)",
                                            "Auto-indent: OFF (click to turn on)", 1);

    /* file_tree_toggle -- the widget system's real SECOND caller
     * (2026-08-27, continuing straight off the first: the auto-indent
     * control above). Placed right after auto_indent_toggle's own
     * real fixed width, same status bar, off by default -- matches
     * this bar's own established "hidden/minimal unless you ask"
     * taste (hover-reveal itself, auto-indent defaulting ON because
     * it's a behavior change vs. this being a supplementary VIEW). */
    Toggle file_tree_toggle = new_toggle(350, WINDOW_HEIGHT - STATUS_BAR_HEIGHT, 220, STATUS_BAR_HEIGHT,
                                          "Files: ON (click to hide)",
                                          "Files: OFF (click to show)", 0);

    /* settings_toggle -- Linnen's own real, first-named-as-such caller
     * (2026-08-27, founder real-time: "we need a widget at the bottom
     * to open settings" -- see docs/NORTHSTAR_LINNEN.md). Same real
     * bottom-bar Toggle shape auto_indent_toggle/file_tree_toggle
     * already establish, placed right after file_tree_toggle's own
     * fixed width. Off by default, same "hidden/minimal unless you
     * ask" taste this bar already has. */
    Toggle settings_toggle = new_toggle(575, WINDOW_HEIGHT - STATUS_BAR_HEIGHT, 220, STATUS_BAR_HEIGHT,
                                         "Settings: ON (click to hide)",
                                         "Settings: OFF (click to show)", 0);
    /* Settings panel geometry -- real fixed screen coords, same "UI
     * chrome, not zoomed content" shape SPOTLIGHT_BOX_X/W already use
     * (docs/NORTHSTAR_LINNEN.md's own "matching the Spotlight overlay's
     * own real modal box precedent"). Defined here, ahead of both the
     * MouseDown click-region test and the render block below, so both
     * real call sites share the one set of numbers instead of two
     * that could drift. */
#define SETTINGS_BOX_X 650
#define SETTINGS_BOX_W 340
#define SETTINGS_ZOOM_ROW_H 40

    /* terminal_toggle -- the real editor/terminal toggle button
     * (2026-08-27, founder real-time: "can we add a new button to the
     * bottom of the parena editor to toggle between terminal and
     * deditor? unifying pitviper and the parena editor - have it work
     * just like pitviper where it auto finds git bash for the
     * terminal"). Backend already shipped (Apple #16430: pty-poll-
     * read + shell/spawn); this is the UI wiring. Fourth real
     * bottom-bar Toggle, placed right after settings_toggle's own
     * fixed width -- same "hidden/minimal unless you ask" taste,
     * off by default. */
    Toggle terminal_toggle = new_toggle(800, WINDOW_HEIGHT - STATUS_BAR_HEIGHT, 220, STATUS_BAR_HEIGHT,
                                         "Terminal: ON (click to hide)",
                                         "Terminal: OFF (click to show)", 0);
    /* Real terminal-session state -- the pty is spawned ONCE, the
     * first time terminal_toggle is switched on, and stays alive
     * across later off/on toggles (a real, persistent shell session,
     * matching how a real terminal app or PITVIPER itself behaves --
     * toggling the VIEW off doesn't kill the shell). term_output is a
     * plain, fixed-size scrollback buffer (same real "fixed C buffer,
     * not a growing arena allocation every frame" judgment
     * spotlight_query already uses) -- when full, the oldest half is
     * discarded to make room, a real, honest v0 "recent scrollback
     * only", not unbounded history.
     *
     * Real, honest, NOT-yet-done gap, matching this thread's own
     * BACKLOG.md scope note: the spawned shell inherits THIS process's
     * cwd, not the file-tree sidebar's own browsed directory --
     * pty-open/shell/spawn take no cwd argument (confirmed by reading
     * runtime/parena_runtime.h's own pty_open_impl: forkpty+execlp,
     * no chdir). Real cwd-sharing is separate, unstarted follow-up. */
    int term_spawned = 0;
    Pty term_pty;
    memset(&term_pty, 0, sizeof term_pty);
#define TERM_OUTPUT_CAP 65536
    char term_output[TERM_OUTPUT_CAP];
    int term_output_len = 0;
    term_output[0] = '\0';

    /* file_tree_dir/file_tree_entries -- real CURRENT WORKING DIRECTORY
     * at startup, not dirname(path) -- see this file's own
     * SIDEBAR_WIDTH header comment for the real reasoning.
     * file_tree_entries is re-listed every time file_tree_dir changes
     * (real directory navigation, 2026-08-27 -- see the MouseDown
     * handler below), NOT every frame -- a real, honest, deliberate v0
     * tradeoff, matching load_from_file's own "read once, not
     * continuously polled" scope; a file created/deleted elsewhere in
     * the CURRENTLY-listed directory while this window stays open
     * won't appear until you navigate away and back, real, separate,
     * deferred follow-up, same class of gap this file already carries
     * for glyph-atlas caching etc. Row 0 in the sidebar is always a
     * real synthetic ".." entry (not part of file_tree_entries itself
     * -- see the render/click-handling code below), letting you
     * navigate back up; parent_dir_of's own real, honest, bounded scope
     * (see its header comment) means going up stops making further
     * progress two levels above the real launch CWD rather than
     * escaping arbitrarily far via mis-parsed ".." chains -- a real,
     * deliberate correctness-over-generality tradeoff, not an
     * oversight. */
    char *file_tree_dir = ".";
    Vec file_tree_entries = list_dir(file_tree_dir, &a);

    /* Real Spotlight overlay state (2026-08-27, founder real-time:
     * "quick open via ctrl+t windows and linux or cmd+t for mac" ->
     * "thats going to be a magic spotlight feature"). spotlight_query
     * is a real, fixed-size C buffer (not a PARENA Buffer/String) --
     * this is genuinely mutable, keystroke-by-keystroke text entry the
     * same class of state auto_indent_toggle's own real C `int`
     * already is, not something PARENA's own no-mutation functional-
     * update convention needs to own. spotlight_results is real
     * stdlib/editor/spotlight.prn output, recomputed via run-providers
     * every time the query text actually changes (not every frame --
     * same "recompute on change, not continuously" tradeoff
     * file_tree_entries' own header comment above already documents),
     * spotlight_selected is the real, currently-highlighted row
     * (Up/Down moves it, Enter activates it). */
    int spotlight_visible = 0;
    char spotlight_query[256] = "";
    int spotlight_query_len = 0;
    Vec spotlight_results = vec_new(&a);
    int spotlight_selected = 0;
    /* Real, new (2026-08-27, founder real-time: "ctrl t needs to
     * scroll when you do down and it scrolls past the view"):
     * spotlight_selected used to be clamped to the real result count
     * but the render loop below always drew rows 0..SPOTLIGHT_MAX_
     * VISIBLE_ROWS-1 with no scroll offset at all -- pressing Down past
     * row 11 kept moving the real selection, just off-screen, with
     * nothing on screen ever showing it had moved. spotlight_scroll_
     * offset is adjusted (spotlight_keep_selection_visible, below)
     * every time spotlight_selected changes, the same real "keep the
     * selection inside the visible window" behavior every real list/
     * menu widget needs. */
    int spotlight_scroll_offset = 0;
#define SPOTLIGHT_MAX_VISIBLE_ROWS 12
#define SPOTLIGHT_ROW_HEIGHT 26

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
        /* Reset the real, per-frame render arena FIRST, before this
         * frame does any of its own drawing (frame_arena's own header
         * comment above has the full reasoning) -- frees every
         * allocation the PREVIOUS frame's render section made, so
         * this frame starts from a clean, bounded slate rather than
         * accumulating on top of every frame ever rendered. */
        arena_free_all(&frame_arena);
        arena_init(&frame_arena);

        Option ev;
        while ((ev = poll_event(&a)).tag == 1) {
            EventKind kind = *(EventKind *)ev.value;
            if (kind.tag == EventKind_TAG_Quit) {
                running = 0;
            } else if (kind.tag == EventKind_TAG_KeyDown) {
                int key = *(int *)kind.value;
                /* Real Spotlight overlay (2026-08-27, founder real-time:
                 * "quick open via ctrl+t windows and linux or cmd+t for
                 * mac"). Checked FIRST, same real reasoning the Ctrl+C/
                 * X/V comment just below already establishes: while the
                 * overlay is open, every KeyDown is swallowed here and
                 * never reaches the ordinary editor keybinds below (a
                 * real, deliberate modal -- typing "s" to search
                 * shouldn't also fall through to whatever "s" does in
                 * the code buffer). gui_held_() is Cmd on macOS (real
                 * KMOD_GUI, see runtime/parena_runtime.h's own
                 * sdl2_gui_held_impl header comment). */
                if (spotlight_visible) {
                    if (key == 27 /* Escape closes the overlay, does NOT quit -- see the
                                      real quit-Escape branch far below, now guarded on
                                      !spotlight_visible for exactly this reason. */) {
                        spotlight_visible = 0;
                    } else if (key == key_backspace()) {
                        if (spotlight_query_len > 0) {
                            spotlight_query_len--;
                            spotlight_query[spotlight_query_len] = '\0';
                            spotlight_results = recompute_spotlight(file_tree_dir, spotlight_query, &a);
                            spotlight_selected = 0;
                            spotlight_scroll_offset = 0;
                        }
                    } else if (key == key_up()) {
                        if (spotlight_selected > 0) spotlight_selected--;
                        spotlight_keep_selection_visible(spotlight_selected, &spotlight_scroll_offset, SPOTLIGHT_MAX_VISIBLE_ROWS);
                    } else if (key == key_down()) {
                        if (spotlight_selected < vec_len(&spotlight_results) - 1) spotlight_selected++;
                        spotlight_keep_selection_visible(spotlight_selected, &spotlight_scroll_offset, SPOTLIGHT_MAX_VISIBLE_ROWS);
                    } else if (key == key_return()) {
                        /* Real, deliberate v0 scope: activates the real
                         * currently-selected row. A File result opens it
                         * the same real way the file-tree sidebar's own
                         * click-to-open already does (load_from_file +
                         * reset undo/redo, matching that call site's
                         * own real header comment on why a fresh load
                         * isn't something to undo/redo INTO); a
                         * Calculator result has already shown its real
                         * computed value in the list, so Enter there
                         * just closes the overlay. */
                        if (spotlight_selected >= 0 && spotlight_selected < vec_len(&spotlight_results)) {
                            SpotlightResult *sel = (SpotlightResult *)vec_get(&spotlight_results, spotlight_selected);
                            if (sel->kind.tag == SpotlightKind_TAG_SKFile) {
                                buf = load_from_file(sel->path, &a);
                                undo_count = 0;
                                redo_count = 0;
                            }
                        }
                        spotlight_visible = 0;
                    }
                    /* every other key is real, deliberately swallowed
                       here -- see this branch's own header comment. */
                } else if (toggle_on_(&terminal_toggle)) {
                    /* Real terminal-mode key routing (2026-08-27) --
                     * checked before every ordinary editor keybind
                     * below, same real modal precedent the
                     * spotlight_visible branch above already
                     * establishes: while the terminal panel is
                     * showing, every real key maps to the byte
                     * sequence a real terminal emulator would send
                     * the pty, not to any editor keybind (including
                     * Escape, deliberately NOT treated as "quit" here
                     * -- a real shell program running inside, e.g.
                     * vim/less, needs a real Escape byte to reach it).
                     * Plain printable characters arrive via
                     * SDL_TEXTINPUT below, not here -- same real split
                     * this file's own editor keybinds already rely on. */
                    if (term_spawned) {
                        char *term_key_seq = NULL;
                        if (key == key_backspace()) {
                            term_key_seq = "\x7f";
                        } else if (key == key_return()) {
                            term_key_seq = "\r";
                        } else if (key == key_tab()) {
                            term_key_seq = "\t";
                        } else if (key == key_left()) {
                            term_key_seq = "\x1b[D";
                        } else if (key == key_right()) {
                            term_key_seq = "\x1b[C";
                        } else if (key == key_up()) {
                            term_key_seq = "\x1b[A";
                        } else if (key == key_down()) {
                            term_key_seq = "\x1b[B";
                        } else if (key == 27 /* SDLK_ESCAPE */) {
                            term_key_seq = "\x1b";
                        } else if (key == 'c' && ctrl_held_()) {
                            term_key_seq = "\x03"; /* SIGINT */
                        } else if (key == 'd' && ctrl_held_()) {
                            term_key_seq = "\x04"; /* EOF */
                        }
                        if (term_key_seq) {
                            Result twr = pty_write(&term_pty, term_key_seq, &a);
                            (void)twr;
                        }
                    }
                } else if (key == 't' && (ctrl_held_() || gui_held_())) {
                    spotlight_visible = 1;
                    spotlight_query[0] = '\0';
                    spotlight_query_len = 0;
                    spotlight_selected = 0;
                    spotlight_scroll_offset = 0;
                    spotlight_results = recompute_spotlight(file_tree_dir, spotlight_query, &a);
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
                } else if (key == 'a' && ctrl_held_()) {
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
                char *text = (char *)kind.value;
                /* Real Spotlight overlay text routing (2026-08-27) --
                 * same modal reasoning the KeyDown branch above already
                 * documents: while the overlay is open, typed text goes
                 * into the real query buffer, never the code buffer.
                 * spotlight_query is a plain, fixed-size C buffer (this
                 * file's own header comment on it explains why), so
                 * this appends byte-by-byte with a real bounds check
                 * rather than assuming SDL2's own TextInput chunk
                 * always fits. */
                if (spotlight_visible) {
                    size_t tlen = strlen(text);
                    for (size_t ti = 0; ti < tlen && spotlight_query_len < (int)sizeof(spotlight_query) - 1; ti++) {
                        spotlight_query[spotlight_query_len++] = text[ti];
                    }
                    spotlight_query[spotlight_query_len] = '\0';
                    spotlight_results = recompute_spotlight(file_tree_dir, spotlight_query, &a);
                    spotlight_selected = 0;
                    spotlight_scroll_offset = 0;
                } else if (toggle_on_(&terminal_toggle)) {
                    /* Real terminal-mode text routing (2026-08-27) --
                     * same real split KeyDown's own terminal branch
                     * above documents: plain typed characters arrive
                     * here, not through KeyDown, and go straight to
                     * the pty as raw input, exactly like a real
                     * terminal emulator. */
                    if (term_spawned) {
                        Result twr = pty_write(&term_pty, text, &a);
                        (void)twr;
                    }
                } else {
                /* Typed text with an active selection REPLACES it --
                 * real, standard editor UX: delete the selection first,
                 * then insert at the (now-collapsed) cursor. */
                push_undo(buf);
                if (has_selection_(&buf)) {
                    Result del = delete_selection(&buf, &a);
                    if (del.tag == 1) buf = *(Buffer *)del.value;
                }
                Result ins = insert_at_cursor(&buf, text, &a);
                if (ins.tag == 1) buf = *(Buffer *)ins.value;
                }
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
                /* Real file-tree sidebar click (2026-08-27) -- same RAW-
                 * screen-coordinate treatment as the status bar's own
                 * toggle immediately below (fixed UI chrome, unaffected
                 * by content zoom). Checked before the status bar toggle
                 * since the sidebar occupies real screen space the
                 * status bar's own toggle rects don't overlap (sidebar
                 * is y in [0, WINDOW_HEIGHT-STATUS_BAR_HEIGHT), the bar
                 * itself is the strip below that), so order between
                 * them doesn't actually matter here -- checked first
                 * only because it's the more specific real region. */
                if (toggle_on_(&file_tree_toggle) && raw_mx >= 0 && raw_mx < SIDEBAR_WIDTH
                    && raw_my >= 0 && raw_my < WINDOW_HEIGHT - STATUS_BAR_HEIGHT) {
                    /* Real directory navigation (2026-08-27, closing
                     * v0.83.0's own honest gap): row 0 is always the
                     * real synthetic ".." entry -- NOT part of
                     * file_tree_entries itself, see parent_dir_of's own
                     * header comment for its real, deliberately bounded
                     * scope -- so real entries start at row_idx 1. entry_idx
                     * (2026-08-27) folds in file_tree_scroll_offset so a
                     * click still targets the real, currently-visible
                     * (post-scroll) entry, not whatever row_idx alone
                     * would mean at scroll_offset 0. */
                    int row_idx = (raw_my - 4) / FILE_TREE_ROW_HEIGHT;
                    int entry_idx = row_idx - 1 + file_tree_scroll_offset;
                    if (row_idx == 0) {
                        file_tree_dir = parent_dir_of(file_tree_dir, &a);
                        file_tree_entries = list_dir(file_tree_dir, &a);
                        file_tree_scroll_offset = 0;
                    } else if (row_idx >= 1 && entry_idx < vec_len(&file_tree_entries)) {
                        char *name = (char *)vec_get(&file_tree_entries, entry_idx);
                        char full_path[4096];
                        snprintf(full_path, sizeof full_path, "%s/%s", file_tree_dir, name);
                        if (is_dir_(full_path)) {
                            size_t plen = strlen(full_path) + 1;
                            char *dir_copy = (char *)arena_alloc(&a, plen);
                            memcpy(dir_copy, full_path, plen);
                            file_tree_dir = dir_copy;
                            file_tree_entries = list_dir(file_tree_dir, &a);
                            file_tree_scroll_offset = 0;
                        } else {
                            /* Real course-correction (2026-08-27):
                             * founder real-time first read as "double
                             * clicking a file in the nerd tree does not
                             * work" -> (this file's own first fix
                             * attempt switched this to load in-place,
                             * reasoning the new-window behavior itself
                             * was the mistake) -> founder real-time,
                             * directly correcting that: "no double
                             * click on nerd tree should open a new
                             * window it just doesnt actually open the
                             * real file". New-window IS the real,
                             * wanted behavior here after all --
                             * spawn_new_instance restored. The REAL bug
                             * (found by actually tracing spawn_new_
                             * instance's own real fork+execl end to
                             * end, confirmed live: the child process
                             * DOES correctly load the real target file)
                             * was in sdl2_create_window_impl (runtime/
                             * parena_runtime.h): every window this
                             * program creates used SDL_WINDOWPOS_
                             * CENTERED, so a spawned second window
                             * opens in the EXACT same screen position
                             * as the still-open first one, perfectly
                             * overlapping it -- reads exactly like
                             * "nothing happened" even though a real,
                             * correctly-loaded new window is really
                             * there. Fixed there, not here. */
                            spawn_new_instance(exe_path, full_path);
                        }
                    }
                } else if (bar_visible_now && toggle_hit_(&auto_indent_toggle, raw_mx, raw_my)) {
                    auto_indent_toggle = toggle_flip(&auto_indent_toggle, &a);
                } else if (bar_visible_now && toggle_hit_(&file_tree_toggle, raw_mx, raw_my)) {
                    file_tree_toggle = toggle_flip(&file_tree_toggle, &a);
                } else if (bar_visible_now && toggle_hit_(&settings_toggle, raw_mx, raw_my)) {
                    settings_toggle = toggle_flip(&settings_toggle, &a);
                } else if (bar_visible_now && toggle_hit_(&terminal_toggle, raw_mx, raw_my)) {
                    terminal_toggle = toggle_flip(&terminal_toggle, &a);
                    if (toggle_on_(&terminal_toggle) && !term_spawned) {
                        Result spr = spawn(80, 24, &a);
                        if (spr.tag == 1) {
                            term_pty = *(Pty *)spr.value;
                            term_spawned = 1;
                        } else {
                            fprintf(stderr, "editor: terminal spawn failed (real SpawnFailed from shell/spawn)\n");
                        }
                    }
                } else if (toggle_on_(&settings_toggle)
                           && raw_mx >= SETTINGS_BOX_X && raw_mx < SETTINGS_BOX_X + SETTINGS_BOX_W
                           && raw_my >= 70 && raw_my < 70 + SETTINGS_ZOOM_ROW_H) {
                    /* Real Zoom -/+ click regions (2026-08-27, Linnen's
                     * own first real Settings-panel control -- see
                     * docs/NORTHSTAR_LINNEN.md). Same real hand-rolled
                     * click-region shape the file-tree sidebar/Spotlight
                     * overlay's own row hit-testing already use, not a
                     * new pattern. Reuses the exact same zoom_percent
                     * adjust+clamp the existing Ctrl+scroll/Ctrl+-+
                     * keybinds already establish -- one real, shared
                     * value, three real ways to change it. */
                    int zoom_minus_x1 = SETTINGS_BOX_X + 150;
                    int zoom_plus_x0 = SETTINGS_BOX_X + 190;
                    if (raw_mx >= SETTINGS_BOX_X + 110 && raw_mx < zoom_minus_x1) {
                        zoom_percent -= ZOOM_STEP;
                        if (zoom_percent < ZOOM_MIN) zoom_percent = ZOOM_MIN;
                    } else if (raw_mx >= zoom_plus_x0 && raw_mx < zoom_plus_x0 + 40) {
                        zoom_percent += ZOOM_STEP;
                        if (zoom_percent > ZOOM_MAX) zoom_percent = ZOOM_MAX;
                    }
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
                     * row. text_x_origin (2026-08-27, file-tree
                     * sidebar): the real logical text-start x, shifted
                     * right of the plain 12px margin by the sidebar's
                     * own real (zoom-corrected) width when visible --
                     * same real integer-safe *100/zoom_percent
                     * convention mx/my already use. */
                    int mx = mouse_x() * 100 / zoom_percent;
                    int my = mouse_y() * 100 / zoom_percent;
                    int text_x_origin = 12 + (toggle_on_(&file_tree_toggle) ? (SIDEBAR_WIDTH * 100 / zoom_percent) : 0);
                    int pos = pos_from_mouse(active_text(&buf), mx, my + scroll_offset * LINE_HEIGHT, text_x_origin, &font, &a);
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
                    int text_x_origin = 12 + (toggle_on_(&file_tree_toggle) ? (SIDEBAR_WIDTH * 100 / zoom_percent) : 0);
                    int pos = pos_from_mouse(active_text(&buf), mx, my + scroll_offset * LINE_HEIGHT, text_x_origin, &font, &a);
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
                } else if (toggle_on_(&file_tree_toggle) && mouse_x() >= 0 && mouse_x() < SIDEBAR_WIDTH
                           && mouse_y() >= 0 && mouse_y() < WINDOW_HEIGHT - STATUS_BAR_HEIGHT) {
                    /* Real file-tree sidebar scroll (2026-08-27, founder
                     * real-time: "nerd tree needs to scroll with mouse
                     * wheel if im hovering over nerd tree vs hovering
                     * over text editor"). Same real RAW-screen-coordinate
                     * hit-test the file-tree's own MouseDown handler
                     * above already uses (fixed UI chrome, unaffected by
                     * content zoom) -- when the cursor is over the
                     * sidebar, the wheel scrolls file_tree_scroll_offset
                     * instead of the code buffer's own scroll_offset.
                     * Clamped to [0, real entry count] -- same real
                     * "can't scroll above the top, harmless overscroll
                     * past the bottom" shape scroll_offset's own clamp
                     * below already establishes. */
                    file_tree_scroll_offset -= delta * SCROLL_LINES_PER_NOTCH;
                    if (file_tree_scroll_offset < 0) file_tree_scroll_offset = 0;
                    int max_scroll = vec_len(&file_tree_entries);
                    if (file_tree_scroll_offset > max_scroll) file_tree_scroll_offset = max_scroll;
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

        /* Real per-frame, non-blocking pty drain (2026-08-27) -- runs
         * every frame regardless of terminal_toggle's own on/off state
         * (a real, persistent background shell session keeps producing
         * output whether or not its VIEW is showing, same real
         * behavior a real terminal app / tmux session has), using
         * pty-poll-read specifically because it's real, non-blocking
         * (see stdlib/pty.prn's own header comment) -- calling
         * pty-read here instead would freeze the WHOLE editor at an
         * idle shell prompt. Appended into the real, fixed-size
         * term_output scrollback buffer; when a new chunk would
         * overflow it, the oldest half is discarded first to make
         * room -- a real, honest "recent scrollback only" v0, not
         * unbounded history. */
        if (term_spawned) {
            Result tpr = pty_poll_read(&term_pty, &a);
            if (tpr.tag == 1) {
                char *chunk = (char *)tpr.value;
                size_t chunk_len = strlen(chunk);
                if (chunk_len > 0) {
                    if (term_output_len + (int)chunk_len >= TERM_OUTPUT_CAP) {
                        int keep = TERM_OUTPUT_CAP / 2;
                        int drop = term_output_len - keep;
                        if (drop < 0) drop = 0;
                        memmove(term_output, term_output + drop, term_output_len - drop);
                        term_output_len -= drop;
                    }
                    int copy_len = (int)chunk_len;
                    if (term_output_len + copy_len >= TERM_OUTPUT_CAP) copy_len = TERM_OUTPUT_CAP - 1 - term_output_len;
                    if (copy_len > 0) {
                        memcpy(term_output + term_output_len, chunk, (size_t)copy_len);
                        term_output_len += copy_len;
                        term_output[term_output_len] = '\0';
                    }
                }
            }
        }

        Result cbg = set_draw_color(&ren, 24, 24, 28, 255, &frame_arena);
        (void)cbg;
        render_clear(&ren, &frame_arena);

        /* Real Ctrl+Zoom, applied once per frame (2026-08-27) -- every
         * draw call below (text, cursor, selection, the status bar)
         * happens in real LOGICAL coordinates; SDL scales the whole
         * frame uniformly on present, the exact real technique
         * PITVIPER's own cmd/pitviper/main.go already uses ("zoomScale
         * applied once for the whole frame via SDL's own renderer"). */
        Result zoomr = render_set_scale(&ren, zoom_percent, &frame_arena);
        (void)zoomr;

        char *text = active_text(&buf);

        /* text_x_origin (2026-08-27, file-tree sidebar): real logical
         * x where text content starts, shifted right of the plain
         * 12px margin by the sidebar's own real, zoom-corrected width
         * when visible -- same integer-safe *100/zoom_percent
         * convention already used for mouse-coordinate conversion, so
         * the sidebar's own REAL screen-pixel width stays constant
         * (SIDEBAR_WIDTH) regardless of content zoom, matching how the
         * status bar itself stays a real, fixed screen size. */
        int text_x_origin = 12 + (toggle_on_(&file_tree_toggle) ? (SIDEBAR_WIDTH * 100 / zoom_percent) : 0);

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
        /* Real editor-vs-terminal content-area switch (2026-08-27) --
         * "toggle between terminal and editor" means these two views
         * are mutually exclusive in the main content area, not an
         * overlay (unlike Spotlight/Settings, which draw ON TOP of
         * whichever of these is showing -- both are drawn further
         * below, after this real either/or block). */
        if (!toggle_on_(&terminal_toggle)) {
        if (has_selection_(&buf)) {
            int sel_start = selection_start(&buf);
            int sel_end = selection_end(&buf);
            int start_row, start_line_start, end_row, ignored_line_start;
            row_and_line_start_for_pos(text, sel_start, &start_row, &start_line_start);
            row_and_line_start_for_pos(text, sel_end, &end_row, &ignored_line_start);

            Result scol = set_draw_color(&ren, 60, 90, 140, 255, &frame_arena);
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
                char *before_seg = substring(text, cur_line_start, seg_start, &frame_arena);
                char *seg_text = substring(text, seg_start, seg_end, &frame_arena);
                int x_from = text_x_origin + measure_text_width(&font, before_seg);
                int width = measure_text_width(&font, seg_text);
                /* An empty selected segment (e.g. selecting exactly up
                 * to a newline) still gets a thin, visible sliver rather
                 * than vanishing entirely. */
                if (width < 2) width = 2;
                render_fill_rect(&ren, x_from, 12 + row * LINE_HEIGHT - scroll_y_px, width, LINE_HEIGHT - 2, &frame_arena);
                cur_line_start = line_end + 1;
            }
        }

        /* Real windowed render (2026-08-27, founder real-time: opening
         * a real large file "still crashes... we need to open a
         * pointer and read some into memory but not too much ya
         * know? scan or whatever?" -- and separately, live: "tryna
         * open a md file... it just beechballed"). Confirmed live by
         * actually measuring it (Xvfb + a real local 2.7MB/58,524-
         * line PARENA_CONSTRUCT.txt): RSS climbed ~7.5MB/s with NO
         * bound over a sustained 60-second run -- render-highlighted-
         * text re-splits (string/split) AND re-tokenizes the WHOLE
         * file text every single frame regardless of scroll position,
         * and every one of those allocations lives in the arena
         * forever (this whole program's own bump-allocator model
         * never frees mid-run, same real tradeoff this file's own
         * spawn_new_instance-per-window/undo-stack already carry
         * elsewhere). Given enough time, or several windows open at
         * once (the file-tree's own double-click opens a NEW window
         * per file), that's a real, eventual OOM, not a false alarm.
         *
         * Real fix: only pass the VISIBLE window of lines to
         * render_highlighted_text -- window_start/window_end are
         * found with a plain byte-offset scan over `text` (counting
         * '\n' bytes, no allocation), bounding the real per-frame
         * split/tokenize/render cost -- and its arena cost -- to
         * however many rows actually fit on screen, independent of
         * real file size. Safe because tokenize-line is genuinely
         * stateless per line (stdlib/editor/textmate.prn's own header
         * comment: "No begin/end multi-line constructs" -- no cross-
         * line scope state an arbitrary window could cut through and
         * get wrong). visible_text_rows adds 2 rows of slack so a
         * partially-visible row at the very top/bottom still renders,
         * matching this file's own terminal-panel row-count math just
         * below in spirit. The window starts exactly at scroll_offset,
         * so it draws at a plain y=12 -- the real "- scroll_y_px"
         * shift that used to place the FULL text's own scroll_offset-
         * th line at the top is now already baked into window_start
         * itself. */
        int visible_text_rows = (WINDOW_HEIGHT - STATUS_BAR_HEIGHT - 24) / LINE_HEIGHT + 2;
        int window_start = 0;
        {
            int line_i = 0;
            while (text[window_start] != '\0' && line_i < scroll_offset) {
                if (text[window_start] == '\n') line_i++;
                window_start++;
            }
        }
        int window_end = window_start;
        {
            int line_i = 0;
            while (text[window_end] != '\0' && line_i < visible_text_rows) {
                if (text[window_end] == '\n') line_i++;
                window_end++;
            }
        }
        char *visible_text = substring(text, window_start, window_end, &frame_arena);
        Result hr = render_highlighted_text(&ren, &font, &rules, visible_text, text_x_origin, 12, LINE_HEIGHT, &frame_arena);
        (void)hr;

        /* Real cursor: a thin filled rect at the real measured pixel
         * position of the cursor -- proves the buffer's own real
         * cursor-pos genuinely drives something visible, not just
         * tracked internally. */
        int cpos = cursor_pos(&buf);
        int row, line_start;
        row_and_line_start_for_pos(text, cpos, &row, &line_start);
        char *before_cursor_on_line = substring(text, line_start, cpos, &frame_arena);
        int cursor_x = text_x_origin + measure_text_width(&font, before_cursor_on_line);
        int cursor_y = 12 + row * LINE_HEIGHT - scroll_y_px;
        Result ccol = set_draw_color(&ren, 220, 220, 220, 255, &frame_arena);
        (void)ccol;
        render_fill_rect(&ren, cursor_x, cursor_y, 2, 24, &frame_arena);
        } else {
            /* Real terminal panel content (2026-08-27) -- plain
             * monospace text, no syntax highlighting (a real shell's
             * own output isn't PARENA/Markdown source). Tails the
             * real term_output scrollback: shows only however many of
             * the MOST RECENT lines fit in the content area, same
             * real "recent output visible, older stuff scrolls off"
             * behavior any real terminal emulator has -- v0 has no
             * independent terminal scrollback UI yet (a real, separate
             * follow-up, same "expand only when something real needs
             * it" judgment this whole editor's own widget system
             * already applies elsewhere). */
            Result tbg = set_draw_color(&ren, 12, 12, 15, 255, &frame_arena);
            (void)tbg;
            render_fill_rect(&ren, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT - STATUS_BAR_HEIGHT, &frame_arena);
            int visible_term_rows = (WINDOW_HEIGHT - STATUS_BAR_HEIGHT - 24) / LINE_HEIGHT;
            if (visible_term_rows < 1) visible_term_rows = 1;
            int term_line_count = 1;
            for (int oi = 0; oi < term_output_len; oi++) {
                if (term_output[oi] == '\n') term_line_count++;
            }
            int skip_lines = term_line_count - visible_term_rows;
            if (skip_lines < 0) skip_lines = 0;
            int cur_line = 0;
            int line_start_i = 0;
            int draw_row = 0;
            for (int oi = 0; oi <= term_output_len; oi++) {
                if (oi == term_output_len || term_output[oi] == '\n') {
                    if (cur_line >= skip_lines) {
                        char term_line[512];
                        int seg_len = oi - line_start_i;
                        if (seg_len > (int)sizeof(term_line) - 1) seg_len = (int)sizeof(term_line) - 1;
                        if (seg_len > 0) memcpy(term_line, term_output + line_start_i, (size_t)seg_len);
                        term_line[seg_len < 0 ? 0 : seg_len] = '\0';
                        if (term_line[0] != '\0') {
                            Result trow = render_text(&ren, &font, term_line, 12, 12 + draw_row * LINE_HEIGHT, 200, 220, 200, &frame_arena);
                            (void)trow;
                        }
                        draw_row++;
                    }
                    cur_line++;
                    line_start_i = oi + 1;
                }
            }
            if (!term_spawned) {
                Result tmsg = render_text(&ren, &font, "(terminal not spawned -- shell/spawn failed, see stderr)", 12, 12, 220, 120, 120, &frame_arena);
                (void)tmsg;
            }
        }

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
        Result scalereset = render_set_scale(&ren, 100, &frame_arena);
        (void)scalereset;

        /* Real file-tree sidebar (2026-08-27) -- drawn in this same
         * real 100%-scale block as the status bar immediately below,
         * same "fixed UI chrome, not zoomed content" real reasoning.
         * Drawn BEFORE the status bar so the status bar's own real
         * hover-reveal strip stays on top at the bottom edge (they
         * don't actually overlap -- the sidebar's own real height
         * stops at WINDOW_HEIGHT - STATUS_BAR_HEIGHT -- but matching
         * draw order to the rest of this file's own "background things
         * first" convention regardless). */
        if (toggle_on_(&file_tree_toggle)) {
            Result ftbg = set_draw_color(&ren, 32, 32, 38, 255, &frame_arena);
            (void)ftbg;
            render_fill_rect(&ren, 0, 0, SIDEBAR_WIDTH, WINDOW_HEIGHT - STATUS_BAR_HEIGHT, &frame_arena);
            /* Row 0 is always the real synthetic ".." entry (real
             * directory navigation, 2026-08-27) -- a slightly dimmer
             * color than real file/directory names so it reads as
             * chrome, not a real listed entry. Real entries below it
             * start at fi=1, matching the MouseDown handler's own
             * identical row_idx convention. */
            Result upr = render_text(&ren, &font, "..", 8, 4, 140, 140, 155, &frame_arena);
            (void)upr;
            int fn = vec_len(&file_tree_entries);
            /* Real, deliberate v0 tradeoff: is_dir_() runs fresh per
             * visible row per FRAME (an opendir/closedir pair each),
             * not cached alongside file_tree_entries -- same "simple
             * over optimized" judgment this file's own render-text
             * (fresh SDL surface per call) and pos_from_mouse (O(n)
             * per-line walk) already make. A few dozen syscalls at 60Hz
             * for a real, honest directory-color hint is not a real
             * bottleneck; caching it would need a second array kept in
             * lockstep with file_tree_entries across both navigation
             * sites below for a purely cosmetic win. */
            /* Real, minimal scroll application (2026-08-27): starts the
             * visible range at file_tree_scroll_offset instead of 0, and
             * fy is computed relative to that offset so row 1 (right
             * below "..") always shows entry index file_tree_scroll_offset,
             * not entry index 0 -- same real "offset shifts what's
             * visible, doesn't change row HEIGHT/layout" shape
             * scroll_offset already establishes for the code buffer. */
            for (int fi = file_tree_scroll_offset; fi < fn; fi++) {
                int fy = 4 + (fi - file_tree_scroll_offset + 1) * FILE_TREE_ROW_HEIGHT;
                if (fy > WINDOW_HEIGHT - STATUS_BAR_HEIGHT - FILE_TREE_ROW_HEIGHT) break;
                char *entry_name = (char *)vec_get(&file_tree_entries, fi);
                int is_dir_entry;
                {
                    char full_path[4096];
                    snprintf(full_path, sizeof full_path, "%s/%s", file_tree_dir, entry_name);
                    is_dir_entry = is_dir_(full_path);
                }
                Result fr = is_dir_entry
                    ? render_text(&ren, &font, entry_name, 8, fy, 120, 170, 220, &frame_arena)
                    : render_text(&ren, &font, entry_name, 8, fy, 190, 190, 205, &frame_arena);
                (void)fr;
            }
        }

        if (last_mouse_y >= WINDOW_HEIGHT - HOVER_REVEAL_ZONE) {
            Result togglr = render_toggle(&ren, &font, &auto_indent_toggle, 45, 45, 52, 200, 200, 200, &frame_arena);
            (void)togglr;
            Result togglr2 = render_toggle(&ren, &font, &file_tree_toggle, 45, 45, 52, 200, 200, 200, &frame_arena);
            (void)togglr2;
            Result togglr3 = render_toggle(&ren, &font, &settings_toggle, 45, 45, 52, 200, 200, 200, &frame_arena);
            (void)togglr3;
            Result togglr4 = render_toggle(&ren, &font, &terminal_toggle, 45, 45, 52, 200, 200, 200, &frame_arena);
            (void)togglr4;
        }

        /* Real Linnen Settings panel v0 (2026-08-27) -- Zoom is the
         * only real setting so far (docs/NORTHSTAR_LINNEN.md's own
         * real, scoped v0: "figure out the basic settings? zoom i
         * dunno something relevant"). Same real "modal box drawn on
         * top of everything else" shape the Spotlight overlay already
         * establishes just below -- deliberately reusing that pattern,
         * not inventing a second one, per the NORTHSTAR doc's own
         * "real, hand-rolled click regions... not yet a reusable
         * Linnen widget type" scope note. */
        if (toggle_on_(&settings_toggle)) {
            Result pbg = set_draw_color(&ren, 26, 26, 32, 245, &frame_arena);
            (void)pbg;
            render_fill_rect(&ren, SETTINGS_BOX_X, 70, SETTINGS_BOX_W, SETTINGS_ZOOM_ROW_H + 20, &frame_arena);
            Result pborder = set_draw_color(&ren, 90, 130, 200, 255, &frame_arena);
            (void)pborder;
            render_fill_rect(&ren, SETTINGS_BOX_X, 70, SETTINGS_BOX_W, 2, &frame_arena);

            char zoom_line[64];
            snprintf(zoom_line, sizeof zoom_line, "Zoom: %d%%   [ - ]   [ + ]", zoom_percent);
            Result zt = render_text(&ren, &font, zoom_line, SETTINGS_BOX_X + 14, 82, 235, 235, 245, &frame_arena);
            (void)zt;
        }

        /* Real Spotlight overlay render (2026-08-27) -- drawn LAST, on
         * top of everything else (including the status bar toggles
         * just above), the same real "a modal draws over its own
         * host" convention every real Spotlight-style launcher uses.
         * Fixed real screen coordinates, same "UI chrome, not zoomed
         * content" reasoning the file-tree sidebar/status bar already
         * establish above. */
        if (spotlight_visible) {
#define SPOTLIGHT_BOX_X 90
#define SPOTLIGHT_BOX_W 520
            /* Real scroll application (2026-08-27): visible_rows is how
             * many rows actually fit starting at spotlight_scroll_offset
             * (the real remaining result count from there, capped at
             * SPOTLIGHT_MAX_VISIBLE_ROWS) -- spotlight_keep_selection_
             * visible (called at every real spotlight_selected mutation
             * site above) is what keeps the real selection inside this
             * same window, so by the time this renders, spotlight_selected
             * is always already within [offset, offset+visible_rows). */
            int total_results = vec_len(&spotlight_results);
            int visible_rows = total_results - spotlight_scroll_offset;
            if (visible_rows > SPOTLIGHT_MAX_VISIBLE_ROWS) visible_rows = SPOTLIGHT_MAX_VISIBLE_ROWS;
            if (visible_rows < 0) visible_rows = 0;
            int box_h = 44 + visible_rows * SPOTLIGHT_ROW_HEIGHT + 10;
            Result sbg = set_draw_color(&ren, 26, 26, 32, 245, &frame_arena);
            (void)sbg;
            render_fill_rect(&ren, SPOTLIGHT_BOX_X, 70, SPOTLIGHT_BOX_W, box_h, &frame_arena);
            Result sborder = set_draw_color(&ren, 90, 130, 200, 255, &frame_arena);
            (void)sborder;
            render_fill_rect(&ren, SPOTLIGHT_BOX_X, 70, SPOTLIGHT_BOX_W, 2, &frame_arena);

            char query_line[300];
            snprintf(query_line, sizeof query_line, "> %s", spotlight_query[0] ? spotlight_query : "Search files, or type a math expression\xE2\x80\xA6");
            Result sq = render_text(&ren, &font, query_line, SPOTLIGHT_BOX_X + 14, 82, 235, 235, 245, &frame_arena);
            (void)sq;

            for (int si = spotlight_scroll_offset; si < spotlight_scroll_offset + visible_rows; si++) {
                SpotlightResult *res = (SpotlightResult *)vec_get(&spotlight_results, si);
                int ry = 118 + (si - spotlight_scroll_offset) * SPOTLIGHT_ROW_HEIGHT;
                if (si == spotlight_selected) {
                    Result shl = set_draw_color(&ren, 55, 75, 110, 255, &frame_arena);
                    (void)shl;
                    render_fill_rect(&ren, SPOTLIGHT_BOX_X + 4, ry - 2, SPOTLIGHT_BOX_W - 8, SPOTLIGHT_ROW_HEIGHT, &frame_arena);
                }
                Result srow = (res->kind.tag == SpotlightKind_TAG_SKFile)
                    ? render_text(&ren, &font, res->label, SPOTLIGHT_BOX_X + 14, ry, 190, 210, 235, &frame_arena)
                    : render_text(&ren, &font, res->label, SPOTLIGHT_BOX_X + 14, ry, 190, 235, 195, &frame_arena);
                (void)srow;
            }
        }

        render_present(&ren);
        delay(16);
    }

    ttf_quit();
    destroy_renderer(ren);
    destroy_window(win);
    quit();
    arena_free_all(&frame_arena);
    arena_free_all(&a);
    return 0;
}
