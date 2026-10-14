/* tests/test_hexview.c -- verifies stdlib/editor/hexview.prn (hex rows, embedded NULs,
 * short-row padding, offset<->cell geometry). Built with PARENA_NO_GRAPHICS. */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include "test_hexview_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } } while (0)

int main(void) {
    Arena a;
    arena_init(&a);
    char data[] = {0x48, 0x65, 0x00, (char)0xff, 0x7f, 0x20, 0x41, 0x0a, 0x42, 0x10};
    int len = 10;
    CHECK(strcmp(hex_byte(data, 3, &a), "ff") == 0, "hex-byte sign-safe 0xff");
    CHECK(strcmp(hex_offset(0x1a2b, &a), "00001a2b") == 0, "hex-offset 8 digits");
    CHECK(printable_code(0x41) == 'A' && printable_code(0) == '.' && printable_code(0x7f) == '.',
          "printable-code");
    const char *r0 = hex_row(data, len, 0, 8, &a);
    CHECK(strcmp(r0, "00000000  48 65 00 ff 7f 20 41 0a  |He... A.|") == 0, "full row incl. NUL");
    const char *r1 = hex_row(data, len, 8, 8, &a);
    CHECK(strcmp(r1, "00000008  42 10" "                    " "|B."  "|") == 0, "short row padded");
    const char *d = hex_dump(data, len, 8, &a);
    CHECK(strstr(d, "\n00000008  42 10") != NULL, "dump joins rows");
    CHECK(row_of(19, 16) == 1 && col_of(19, 16) == 3 && rows_for(33, 16) == 3, "row/col/rows");
    CHECK(cell_x(0) == 10 && cell_x(2) == 16 && ascii_x(0, 8) == 36, "cell geometry");
    printf(failures ? "FAILED\n" : "ALL PASS\n");
    return failures != 0;
}
