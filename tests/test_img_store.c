/* tests/test_img_store.c -- runtime image store (image/store.prn) and the stream module's second receive
 * bank (net/stream.prn stream-bank / stream-copy-bytes), both added 2026-10-15 for PELLEGRINO's <img>. */
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

    arena_free_all(&a);
    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
