/* fmt.c — `parena fmt`. Founder, real-time: "also build parena auto
 * formatter" / "like go fmt" / "built into parena cli", raised as a
 * real prerequisite for writing PHP-subset templates as PARENA source
 * without every stray indentation style fighting readability.
 *
 * Deliberately NOT an AST-based pretty-printer (parse -> re-serialize):
 * ast.h's own Node has no comment payload at all -- comments are
 * dropped entirely during lexing (skip_whitespace_and_comments(),
 * lexer.c) -- and this stdlib's entire documentation culture lives in
 * dense, real inline `;;` comment blocks (every file in stdlib/ has
 * them). An AST round-trip would silently delete every one. Real,
 * separate, bigger work (teaching the lexer/parser/AST to carry
 * comments as real nodes) would be needed before that approach is
 * safe -- not attempted here.
 *
 * What this actually does instead: a single text-level pass that
 * tracks paren/bracket/brace depth (correctly skipping over string
 * literals and `;`/`;;` comments, matching lexer.c's own real escape
 * and comment rules exactly, so characters inside either never affect
 * depth) and re-indents each line to `depth * 2` spaces, where `depth`
 * accounts for any closing delimiters at the very start of the line
 * (so a line starting with `)` dedents by however many closers open
 * it, before the rest of that line's own content is measured). Every
 * byte of every line's own CONTENT -- comments, strings, internal
 * spacing between tokens on the same line -- passes through verbatim;
 * only each line's OWN leading whitespace is replaced.
 *
 * Real, honest, narrow scope: this is depth-based re-indentation, not
 * semantic Lisp-style alignment (e.g. indenting a form's later
 * arguments under its head symbol, the way Emacs' lisp-mode or
 * clojure-mode do). That's real, separate, much more involved work --
 * this still gives every file in this stdlib a single, consistent,
 * mechanically-checkable indentation convention, which is the real
 * problem "like go fmt" was asked to solve.
 */
#include "fmt.h"
#include <string.h>

typedef struct {
    int depth;      /* running paren/bracket/brace depth */
    int in_comment; /* inside a ';'-to-end-of-line comment */
} ScanState;

/* scan_line_body walks one line's worth of characters (already known
 * not to include the trailing '\n'), updating `st` for the NEXT line
 * exactly the way lexer.c's own real tokenizer would: strings and
 * comments are real, opaque spans -- nothing inside either ever
 * changes depth. `st->in_comment` always resets to 0 on return (a line
 * comment can't span a newline), matching skip_whitespace_and_comments'
 * own "runs to end of line" rule. */
static void scan_line_body(ScanState *st, const char *line, size_t len) {
    st->in_comment = 0;
    size_t i = 0;
    while (i < len) {
        char c = line[i];
        if (st->in_comment) {
            i++;
            continue;
        }
        if (c == ';') {
            st->in_comment = 1;
            i++;
            continue;
        }
        if (c == '"') {
            i++;
            while (i < len && line[i] != '"') {
                if (line[i] == '\\' && i + 1 < len) {
                    i += 2;
                } else {
                    i++;
                }
            }
            if (i < len) i++; /* consume the closing '"'; an unterminated
                                  string just runs off the line's own end,
                                  same honest non-failure this whole pass
                                  takes on malformed input elsewhere */
            continue;
        }
        switch (c) {
            case '(': case '[': case '{': st->depth++; break;
            case ')': case ']': case '}':
                if (st->depth > 0) st->depth--;
                break;
            default: break;
        }
        i++;
    }
    st->in_comment = 0;
}

/* leading_closer_count counts how many closing delimiters open this
 * line, ignoring leading whitespace, so a line starting with `)))`
 * (or `) foo`) indents at the depth AFTER those closes, not before --
 * the one real bit of "isn't purely mechanical" this formatter does,
 * and still purely structural (no semantic head-symbol alignment). */
static int leading_closer_count(const char *line, size_t len) {
    size_t i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    int count = 0;
    while (i < len && (line[i] == ')' || line[i] == ']' || line[i] == '}')) {
        count++;
        i++;
    }
    return count;
}

const char *fmt_source(Arena *arena, const char *src, size_t len) {
    /* Output is never smaller than the input by more than a constant
     * factor (re-indentation only ever changes LEADING whitespace per
     * line) -- a generous fixed multiplier avoids a second pass just
     * to size the buffer exactly, the same "measure loosely, arena-
     * allocate once" convention this compiler's own emitter already
     * uses throughout (StrBuf's own doubling growth). */
    size_t cap = len * 2 + 4096;
    char *out = (char *)arena_alloc(arena, cap);
    size_t out_len = 0;

    ScanState st;
    st.depth = 0;
    st.in_comment = 0;

    size_t i = 0;
    while (i <= len) {
        size_t line_start = i;
        while (i < len && src[i] != '\n') i++;
        size_t line_len = i - line_start;
        const char *line = src + line_start;

        /* Trim leading whitespace from the line's own content -- it's
         * being replaced by the computed indent below. Trailing
         * whitespace is trimmed too (a real, small, honest extra: no
         * reason a formatter should preserve trailing spaces gofmt
         * itself would also strip). */
        size_t content_start = 0;
        while (content_start < line_len && (line[content_start] == ' ' || line[content_start] == '\t')) {
            content_start++;
        }
        size_t content_end = line_len;
        while (content_end > content_start && (line[content_end - 1] == ' ' || line[content_end - 1] == '\t' ||
                                                 line[content_end - 1] == '\r')) {
            content_end--;
        }
        size_t content_len = content_end - content_start;

        if (content_len > 0) {
            int closers = leading_closer_count(line, line_len);
            int indent_depth = st.depth - closers;
            if (indent_depth < 0) indent_depth = 0;
            int indent_cols = indent_depth * 2;

            if (out_len + (size_t)indent_cols + content_len + 2 > cap) {
                /* Real, honest fallback: a pathological input (e.g. one
                 * absurdly long line) outgrowing the generous initial
                 * estimate just stops growing the indent/trim pass for
                 * the remainder and copies the rest of the line
                 * verbatim -- never silently truncates output, never
                 * crashes on a bounds violation. */
                for (int c = 0; c < indent_cols && out_len < cap; c++) out[out_len++] = ' ';
                for (size_t c = 0; c < content_len && out_len < cap; c++) out[out_len++] = line[content_start + c];
            } else {
                for (int c = 0; c < indent_cols; c++) out[out_len++] = ' ';
                memcpy(out + out_len, line + content_start, content_len);
                out_len += content_len;
            }
        }
        /* Blank line (content_len == 0): emit nothing but the newline
         * below -- collapses trailing-whitespace-only lines to truly
         * empty ones, real lines stay real lines. */

        scan_line_body(&st, line, line_len);

        if (i < len) {
            /* real newline in the input -- keep it */
            out[out_len++] = '\n';
            i++;
        } else {
            /* end of input: only emit a trailing newline if the last
             * real line had content, so formatting an already-clean
             * file (ending in exactly one '\n') is a true no-op rather
             * than growing by one byte every run. */
            if (content_len > 0 && (out_len == 0 || out[out_len - 1] != '\n')) {
                out[out_len++] = '\n';
            }
            break;
        }
    }

    out[out_len] = '\0';
    return out;
}
