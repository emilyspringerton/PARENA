/* tests/test_img_store.c -- runtime image store (image/store.prn) and the stream module's second receive
 * bank (net/stream.prn stream-bank / stream-copy-bytes), both added 2026-10-15 for PELLEGRINO's <img>; plus the
 * static stylesheet text store (css-append / css-buf / css-len / css-reset), added the same day for its
 * external <link rel=stylesheet> / @import support. */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
static int failures = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); failures++; } else printf("PASS: %s\n", m); } while (0)

int main(void) {
    Arena a;
    arena_init(&a);

    /* store: 2x2 RGBA, one transparent pixel */
    Bytes px = bytes_alloc_impl(&a, 16);
    const unsigned char v[16] = {255, 0, 0, 255,  0, 255, 0, 255,  0, 0, 255, 10,  1, 2, 3, 128};
    for (int i = 0; i < 16; i++) bytes_set_impl(px, i, v[i]);
    int id = img_store_impl(px, 2, 2);
    CHECK(id == 0, "first image gets id 0");
    CHECK(img_w_impl(id) == 2 && img_h_impl(id) == 2, "dimensions kept");
    CHECK(img_px_impl(id, 0, 0) == 0xFF0000, "red");
    CHECK(img_px_impl(id, 1, 0) == 0x00FF00, "green");
    CHECK(img_px_impl(id, 0, 1) == -1, "alpha 10 is transparent");
    CHECK(img_px_impl(id, 1, 1) == 0x010203, "alpha 128 is opaque enough");
    CHECK(img_px_impl(id, 2, 0) == -1 && img_px_impl(id, 0, -1) == -1, "out of range is -1");
    CHECK(img_store_impl(px, 3, 3) == -1, "short buffer refused");
    CHECK(img_store_impl(px, 0, 5) == -1, "zero width refused");
    CHECK(img_reset_impl() == 0 && img_w_impl(0) == 0, "reset forgets images");
    int n = 0;
    while (img_store_impl(px, 2, 2) >= 0) n++;
    CHECK(n == PARENA_IMG_MAX_IDS, "id table is bounded");
    img_reset_impl();

    /* banks: bytes written to bank 1 never touch bank 0 */
    CHECK(stream_bank_impl(0) == 0, "starts on bank 0");
    stream_reset_impl();
    memcpy(stream_cur_buf(), "page", 4);
    stream_cur_set_len(4);
    CHECK(stream_bank_impl(1) == 0, "switch returns old bank");
    stream_reset_impl();
    CHECK(stream_len_impl() == 0, "bank 1 starts empty");
    const char bin[6] = {'\x89', 'P', 0, 'N', 'G', 7};
    memcpy(stream_cur_buf(), bin, 6);
    stream_cur_set_len(6);
    Bytes cp = stream_copy_bytes_impl(&a, 2);
    CHECK(cp.len == 4 && cp.data[0] == 0 && cp.data[1] == 'N' && cp.data[3] == 7, "binary-safe copy past a NUL");
    CHECK(stream_copy_bytes_impl(&a, 9).len == 0, "copy out of range is empty");
    CHECK(stream_bank_impl(0) == 1, "switch back returns bank 1");
    CHECK(stream_len_impl() == 4 && memcmp(stream_buf_impl(), "page", 4) == 0, "page bytes untouched");

    /* css store: appended text outlives the bank it came from; offsets stay valid as the store grows */
    stream_bank_impl(1);
    stream_reset_impl();
    memcpy(stream_cur_buf(), "HDR\r\n\r\na{color:red}", 19);
    stream_cur_set_len(19);
    css_reset_impl();
    CHECK(css_len_impl() == 0 && css_buf_impl()[0] == 0, "css store starts empty");
    int s1 = css_append_impl(7, -1);
    CHECK(s1 == 0 && css_len_impl() == 13, "first append starts at 0 and adds a newline");
    CHECK(memcmp(css_buf_impl(), "a{color:red}\n", 13) == 0 && css_buf_impl()[13] == 0, "text is copied, NUL-terminated");
    stream_reset_impl();
    memcpy(stream_cur_buf(), "XXb{x:y}", 8);
    stream_cur_set_len(8);
    int s2 = css_append_impl(2, 4);
    CHECK(s2 == 13 && css_len_impl() == 13 + 4 + 1, "second append lands after the first, capped by maxlen");
    CHECK(memcmp(css_buf_impl() + s2, "b{x:\n", 5) == 0, "capped copy is the first maxlen bytes");
    CHECK(css_append_impl(9, -1) == -1 && css_append_impl(-1, -1) == -1, "bad offsets are refused");
    CHECK(memcmp(css_buf_impl(), "a{color:red}\n", 13) == 0, "earlier text untouched");
    stream_reset_impl();
    css_reset_impl();
    CHECK(css_len_impl() == 0, "reset empties the store");
    /* the cap: a store that cannot take more says -1, never overruns */
    {
        static char big[1024];
        memset(big, 'x', sizeof big);
        stream_bank_impl(1);
        int guard = 0;
        int last = 0;
        while (guard++ < 100000) {
            memcpy(stream_cur_buf(), big, sizeof big);
            stream_cur_set_len(sizeof big);
            last = css_append_impl(0, -1);
            if (last < 0) break;
        }
        CHECK(last == -1 && css_len_impl() <= (int)PARENA_CSS_MAX && css_buf_impl()[css_len_impl()] == 0,
              "store is bounded and stays NUL-terminated");
        css_reset_impl();
        stream_reset_impl();
    }
    stream_bank_impl(0);

    arena_free_all(&a);
    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
