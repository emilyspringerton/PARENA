/* tests/test_editor_io.c -- real end-to-end verification of the
 * PARENA editor's own real save/load path: stdlib/io.prn's real
 * file-open/write-string/read-line/file-close, and
 * stdlib/editor/buffer.prn's real from-text constructor, the same
 * real functions examples/editor_main.c's own save_to_file/
 * load_first_line helpers call (founder real-time: "continue working
 * on parena editor").
 *
 * Same "test what's actually there" discipline as every other real
 * test in this repo: writes a real file to a real path, reads it back
 * with the real io.prn functions, confirms the real buffer round-trips
 * exactly -- not asserted against a canned in-memory expectation.
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "test_editor_io_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

int main(void) {
    Arena a;
    arena_init(&a);

    char path[] = "/tmp/parena_editor_io_test_XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0, "real temp file created for the round-trip test");
    close(fd);
    unlink(path); /* path-exists? must correctly report false before the real save below */

    CHECK(!path_exists_(path), "a real nonexistent path correctly reports path-exists? false");

    /* --- real save: build a buffer, write its real text to a real file --- */
    Buffer buf = from_text("Hello, PARENA editor!");
    Result openw = file_open(path, OpenMode_Write(), &a);
    CHECK(openw.tag == 1, "file-open in Write mode succeeds on a real path");
    if (openw.tag == 1) {
        FileHandle f = *(FileHandle *)openw.value;
        Result wr = write_string(f, active_text(&buf), &a);
        CHECK(wr.tag == 1, "write-string writes the real buffer text to the real file");
        Result cr = file_close(f, &a);
        CHECK(cr.tag == 1, "file-close succeeds after a real write");
    }

    CHECK(path_exists_(path), "the real file now exists on disk after a real save");

    /* --- real load: read it back with a FRESH buffer, confirm exact
     * round-trip through the real functions, not a canned string --- */
    Result openr = file_open(path, OpenMode_Read(), &a);
    CHECK(openr.tag == 1, "file-open in Read mode succeeds on the real just-written file");
    if (openr.tag == 1) {
        FileHandle f = *(FileHandle *)openr.value;
        Result lr = read_line(f, &a);
        CHECK(lr.tag == 1, "read-line succeeds on the real file");
        if (lr.tag == 1) {
            Option maybe_line = *(Option *)lr.value;
            CHECK(maybe_line.tag == 1, "read-line finds a real line in the real non-empty file");
            if (maybe_line.tag == 1) {
                char *line = (char *)maybe_line.value;
                Buffer loaded = from_text(line);
                CHECK(strcmp(active_text(&loaded), "Hello, PARENA editor!") == 0,
                      "the real loaded buffer text is byte-for-byte identical to what was really saved");
                CHECK(cursor_pos(&loaded) == (int)strlen(active_text(&loaded)),
                      "from-text places the real cursor at the real end of the loaded text");
            }
        }
        file_close(f, &a);
    }

    /* --- real, honest behavior on a real empty file: load-first-line's
     * own None case, not a crash or a fabricated line --- */
    char empty_path[] = "/tmp/parena_editor_io_test_empty_XXXXXX";
    int efd = mkstemp(empty_path);
    CHECK(efd >= 0, "real empty temp file created");
    close(efd);
    Result openempty = file_open(empty_path, OpenMode_Read(), &a);
    if (openempty.tag == 1) {
        FileHandle ef = *(FileHandle *)openempty.value;
        Result elr = read_line(ef, &a);
        CHECK(elr.tag == 1, "read-line succeeds (as Ok) even on a real empty file");
        if (elr.tag == 1) {
            Option maybe_empty = *(Option *)elr.value;
            CHECK(maybe_empty.tag == 0, "read-line correctly reports None on a real empty file, not a fabricated line");
        }
        file_close(ef, &a);
    }
    unlink(empty_path);
    unlink(path);

    arena_free_all(&a);

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
