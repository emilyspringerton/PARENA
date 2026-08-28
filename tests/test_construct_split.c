/* tests/test_construct_split.c -- real end-to-end verification of
 * stdlib/editor/construct_split.prn (2026-08-28, founder real-time:
 * "i want this as a parena mod - have it hook into the ctrl t quick
 * open pane when i have that open if i type /construct-split 10 if it
 * is a construct file it should use file start and file end to open
 * up new panes with the chunks of the file broken into roughly equal
 * 10 sizes its not gonna be totally equal").
 *
 * Pure logic, no SDL2/Xvfb needed -- the real splitting algorithm
 * itself never touches disk or a window; the real per-chunk temp-file-
 * write-plus-new-window-spawn only happens in the C driver at
 * Spotlight activation time (examples/editor_main.c), out of scope
 * for this test the same way SKFile's own "open this path" is never
 * exercised by tests/test_editor_spotlight.c either.
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>

#include "test_construct_split_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

/* build_construct -- real, minimal construct-bundle text with
 * `n_files` real "--- FILE START: ---"/"--- FILE END: ---" blocks,
 * matching PARENA's own real ci.yml "Generate Construct Bundle" step
 * output format exactly (the same real format construct-split.prn's
 * own header comment names). */
static char *build_construct(Arena *a, int n_files) {
    size_t cap = 65536 + (size_t)n_files * 256;
    char *buf = (char *)arena_alloc(a, cap);
    buf[0] = '\0';
    strcat(buf, "PARENA CONSTRUCT 1\ncommit: abc123\ngenerated_utc: 2026-08-28T00:00:00Z\n\n");
    for (int i = 0; i < n_files; i++) {
        char piece[300];
        snprintf(piece, sizeof piece,
                 "--- FILE START: file%d.c ---\nint x%d = %d;\nsome padding content here\n\n"
                 "--- FILE END: file%d.c ---\n\n",
                 i, i, i, i);
        strcat(buf, piece);
    }
    return buf;
}

int main(void) {
    Arena a;
    arena_init(&a);

    /* --- parse-construct-split-count --- */
    {
        Option r = parse_construct_split_count((char *)"/construct-split 10", &a);
        CHECK(r.tag == 1, "parse-construct-split-count recognizes a real '/construct-split 10' command");
        CHECK(r.tag == 1 && *(int *)r.value == 10,
              "parse-construct-split-count extracts the real, correct count (10)");
    }
    {
        Option r = parse_construct_split_count((char *)"hello.prn", &a);
        CHECK(r.tag == 0, "parse-construct-split-count returns None for a real ordinary file-search query");
    }
    {
        Option r = parse_construct_split_count((char *)"/construct-split abc", &a);
        CHECK(r.tag == 0, "parse-construct-split-count returns None for a real non-numeric count");
    }
    {
        Option r = parse_construct_split_count((char *)"/construct-split 0", &a);
        CHECK(r.tag == 0, "parse-construct-split-count returns None for a real non-positive count");
    }

    /* --- construct-file? --- */
    {
        char *real_construct = build_construct(&a, 5);
        CHECK(construct_file_(real_construct, &a) == 1,
              "construct-file? correctly identifies a real construct bundle");
        CHECK(construct_file_((char *)"just some ordinary code here\n", &a) == 0,
              "construct-file? correctly rejects real ordinary (non-construct) text");
    }

    /* --- split-construct: real correctness across all real chunks --- */
    {
        int n_files = 400;
        char *text = build_construct(&a, n_files);
        Vec chunks = split_construct(text, 10, &a);
        int n_chunks = vec_len(&chunks);
        CHECK(n_chunks > 0 && n_chunks <= 10,
              "split-construct produces at most the requested chunk count, and at least one");

        /* every real chunk starts with the shared header */
        int all_have_header = 1;
        for (int i = 0; i < n_chunks; i++) {
            char *c = (char *)vec_get(&chunks, i);
            if (strncmp(c, "PARENA CONSTRUCT", 16) != 0) all_have_header = 0;
        }
        CHECK(all_have_header, "every real chunk carries the shared construct header, not just the first");

        /* every real file appears in EXACTLY one chunk -- the real
         * correctness property that actually matters: no file
         * silently duplicated or dropped across the real split. */
        int all_found_once = 1;
        for (int f = 0; f < n_files; f++) {
            char needle[64];
            snprintf(needle, sizeof needle, "int x%d = %d;", f, f);
            int hits = 0;
            for (int i = 0; i < n_chunks; i++) {
                char *c = (char *)vec_get(&chunks, i);
                char *p = c;
                while ((p = strstr(p, needle)) != NULL) { hits++; p++; }
            }
            if (hits != 1) { all_found_once = 0; break; }
        }
        CHECK(all_found_once,
              "every one of 400 real files across the real split appears in EXACTLY one chunk -- "
              "none silently duplicated or dropped");

        /* real, roughly-equal sizing: no chunk should be wildly larger
         * than the real average -- a genuine regression (e.g. all
         * content landing in one chunk) would blow this bound, while
         * still allowing the founder's own explicit "not gonna be
         * totally equal" real unevenness. */
        int total = 0;
        for (int i = 0; i < n_chunks; i++) total += (int)strlen((char *)vec_get(&chunks, i));
        int avg = total / n_chunks;
        int roughly_balanced = 1;
        for (int i = 0; i < n_chunks; i++) {
            int len = (int)strlen((char *)vec_get(&chunks, i));
            if (len > avg * 3) roughly_balanced = 0;
        }
        CHECK(roughly_balanced,
              "real chunk sizes stay roughly balanced (no single chunk more than 3x the real average) -- "
              "a real greedy bin-pack, not all content landing in one chunk");
    }

    /* --- split-construct: real, honest fallback for non-construct text --- */
    {
        Vec chunks = split_construct((char *)"just plain text, not a construct file\n", 5, &a);
        CHECK(vec_len(&chunks) == 1,
              "split-construct on real non-construct text returns the whole text as ONE chunk, "
              "not a crash or an empty result");
    }

    /* --- split-construct: fewer real files than requested chunks --- */
    {
        char *text = build_construct(&a, 3);
        Vec chunks = split_construct(text, 10, &a);
        CHECK(vec_len(&chunks) <= 3,
              "split-construct never produces more real chunks than there are real files, even "
              "when more chunks were requested than files exist");
    }

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
