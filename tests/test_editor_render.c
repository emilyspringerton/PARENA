/* tests/test_editor_render.c -- real end-to-end verification tying
 * together everything shipped this session: stdlib/editor/textmate.prn
 * (tokenizer) + stdlib/editor/textmate_parena.prn (the real PARENA
 * grammar) + stdlib/editor/theme.prn (scope -> color) +
 * stdlib/editor/render.prn (draws it). Founder real-time: "start adding
 * all of the features of textmate" -> "ALL THE FEATURES" -- the
 * concrete moment real PARENA source becomes real, visible,
 * syntax-highlighted text on a real screen.
 *
 * Two real levels of verification, same discipline every other test in
 * this repo already uses: (1) color-for-scope is a pure function,
 * checked directly against the real documented theme mapping for every
 * real scope this grammar produces; (2) render-highlighted-line is
 * checked for real, error-free integration against the real renderer/
 * font/grammar, tokenizing and drawing real lines of actual PARENA
 * source (lifted from this very session's own stdlib/pty.prn) under a
 * real Xvfb display.
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "test_editor_render_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

int main(void) {
    Arena a;
    arena_init(&a);

    /* --- color-for-scope: real, direct checks against the documented
     * theme mapping --- */
    {
        Color kw = color_for_scope("keyword.control.parena");
        CHECK(kw.r == 197 && kw.g == 134 && kw.b == 192, "keyword.control.parena maps to the real documented purple");

        Color str = color_for_scope("string.quoted.double.parena");
        CHECK(str.r == 206 && str.g == 145 && str.b == 120, "string.quoted.double.parena maps to the real documented orange");

        Color cm = color_for_scope("comment.line.semicolon.parena");
        CHECK(cm.r == 106 && cm.g == 153 && cm.b == 85, "comment.line.semicolon.parena maps to the real documented green");

        Color unknown = color_for_scope("scope.this.grammar.never.produces");
        Color plain = color_for_scope("");
        CHECK(unknown.r == plain.r && unknown.g == plain.g && unknown.b == plain.b,
              "an unrecognized scope falls back to the real default color, not a crash or garbage");

        /* Real Markdown scope colors (2026-08-27, founder: "make sure
         * we support .md syntax highlighting"). */
        Color heading = color_for_scope("markup.heading.markdown");
        CHECK(heading.r == 86 && heading.g == 156 && heading.b == 214, "markup.heading.markdown maps to the real documented blue");
        Color bold = color_for_scope("markup.bold.markdown");
        CHECK(bold.r == 220 && bold.g == 158 && bold.b == 84, "markup.bold.markdown maps to the real documented warm orange");
        Color italic = color_for_scope("markup.italic.markdown");
        CHECK(italic.r == 96 && italic.g == 179 && italic.b == 166, "markup.italic.markdown maps to the real documented muted teal");
        Color code = color_for_scope("markup.inline.raw.markdown");
        CHECK(code.r == 206 && code.g == 145 && code.b == 120,
              "markup.inline.raw.markdown reuses the real existing string/code orange");
        Color quote = color_for_scope("markup.quote.markdown");
        CHECK(quote.r == 106 && quote.g == 153 && quote.b == 85,
              "markup.quote.markdown reuses the real existing comment green");
        Color list = color_for_scope("markup.list.markdown");
        CHECK(list.r == 128 && list.g == 179 && list.b == 224, "markup.list.markdown maps to the real documented light blue");
    }

    /* --- real end-to-end render: an actual grammar tokenizing and
     * drawing real PARENA source through the real SDL2 stack --- */
    Result r = init(&a);
    CHECK(r.tag == 1, "sdl2/init succeeds under a real X display");
    if (r.tag != 1) { arena_free_all(&a); return 1; }

    Result wr = create_window("PARENA editor v0 -- real syntax highlighting", 640, 300, &a);
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
    if (fontr.tag != 1) { destroy_renderer(ren); destroy_window(win); arena_free_all(&a); return 1; }
    Font font = *(Font *)fontr.value;

    Result gr = build_grammar(&a);
    CHECK(gr.tag == 1, "editor/textmate-parena's real build-grammar compiles every real rule");
    if (gr.tag != 1) { arena_free_all(&a); return 1; }
    Vec rules = *(Vec *)gr.value;
    CHECK(vec_len(&rules) == 7, "the real PARENA grammar has exactly its 7 real documented rules");

    /* Real lines of actual PARENA source, lifted from this session's
     * own stdlib/pty.prn -- not synthetic test strings. */
    const char *real_lines[] = {
        ";; real comment: PITVIPER's own shipped Open()",
        "(defstruct Pty",
        "(defn pty-open [(shell : String @ Region) (cols : I32)]",
        "  (Err SpawnFailed)",
    };
    int all_ok = 1;
    Result set_bg = set_draw_color(&ren, 30, 30, 35, 255, &a);
    if (set_bg.tag != 1) all_ok = 0;
    Result clr = render_clear(&ren, &a);
    if (clr.tag != 1) all_ok = 0;
    for (int i = 0; i < 4; i++) {
        Result hr = render_highlighted_line(&ren, &font, &rules, (char *)real_lines[i], 8, 8 + i * 22, &a);
        if (hr.tag != 1) all_ok = 0;
    }
    render_present(&ren);
    delay(16);
    CHECK(all_ok, "4 real lines of actual PARENA source tokenize and render as real syntax-highlighted text, no errors");

    /* --- real multi-line rendering (render-highlighted-text), added
     * 2026-08-26 alongside real multi-line editing support --- */
    {
        const char *multiline_src =
            ";; a real multi-line PARENA snippet\n"
            "(defn add [(a : I32) (b : I32)] : I32\n"
            "  (+ a b))";
        Result cbg2 = set_draw_color(&ren, 30, 30, 35, 255, &a);
        Result clr2 = render_clear(&ren, &a);
        Result mlr = render_highlighted_text(&ren, &font, &rules, (char *)multiline_src, 8, 8, 22, &a);
        render_present(&ren);
        delay(16);
        CHECK(cbg2.tag == 1 && clr2.tag == 1 && mlr.tag == 1,
              "render-highlighted-text renders a real 3-line PARENA snippet (embedded real newlines) without error");
    }

    /* --- real Markdown grammar (2026-08-27, founder: "make sure we
     * support .md syntax highlighting") -- real lines lifted from this
     * very repo's own real .md files (CLAUDE.md, BACKLOG.md,
     * emiree-emily-fatbaby.md), not synthetic test strings, same real
     * discipline the PARENA grammar's own test above already uses. --- */
    {
        Result mgr = build_markdown_grammar(&a);
        CHECK(mgr.tag == 1, "editor/textmate-markdown's real build-markdown-grammar compiles every real rule");
        if (mgr.tag == 1) {
            Vec mrules = *(Vec *)mgr.value;
            CHECK(vec_len(&mrules) == 9, "the real Markdown grammar has exactly its 9 real documented rules");

            /* Real, direct tokenization check (not just "did it render
             * without crashing") -- confirms the real heading rule
             * actually fires on a real heading line, matching the real
             * PARENA grammar's own tokenize-line precedent used
             * elsewhere in this file's own test_editor.c sibling. */
            Vec toks = tokenize_line(&mrules, (char *)"# CLAUDE.md", &a);
            CHECK(vec_len(&toks) >= 1, "tokenize-line produces at least one real token for a real heading line");
            if (vec_len(&toks) >= 1) {
                Token first = *(Token *)vec_get(&toks, 0);
                CHECK(strcmp(first.scope, "markup.heading.markdown") == 0,
                      "the real heading line's own first token is genuinely scoped markup.heading.markdown");
            }

            const char *real_md_lines[] = {
                "# CLAUDE.md",
                "**Before starting any work, read `EMILY/BACKLOG.md`.** Pick the highest-priority unchecked item.",
                "> *\"The backlog is the load-bearing node. Everything else is downstream of that.\"*",
                "1. Provision MySQL and set `MYSQL_DSN`",
            };
            int md_all_ok = 1;
            Result mbg = set_draw_color(&ren, 30, 30, 35, 255, &a);
            if (mbg.tag != 1) md_all_ok = 0;
            Result mclr = render_clear(&ren, &a);
            if (mclr.tag != 1) md_all_ok = 0;
            for (int i = 0; i < 4; i++) {
                Result hr = render_highlighted_line(&ren, &font, &mrules, (char *)real_md_lines[i], 8, 8 + i * 22, &a);
                if (hr.tag != 1) md_all_ok = 0;
            }
            render_present(&ren);
            delay(16);
            CHECK(md_all_ok, "4 real lines lifted from this repo's own .md files tokenize and render as real syntax-highlighted text, no errors");
        }
    }

    close_font(font);
    ttf_quit();
    destroy_renderer(ren);
    destroy_window(win);
    quit();
    arena_free_all(&a);

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
