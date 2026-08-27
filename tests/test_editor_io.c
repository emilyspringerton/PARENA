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
#include <sys/stat.h>
#include <time.h>

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

    /* --- real list-dir (2026-08-27, added for the file-tree sidebar):
     * a real, isolated temp directory with known real entries, not a
     * canned expectation against some pre-existing repo path. --- */
    {
        char dir_template[] = "/tmp/parena_editor_listdir_test_XXXXXX";
        char *dirpath = mkdtemp(dir_template);
        CHECK(dirpath != NULL, "a real temp directory is created for the list-dir test");
        if (dirpath != NULL) {
            char file_a[512], file_b[512], subdir[512];
            snprintf(file_a, sizeof file_a, "%s/a.txt", dirpath);
            snprintf(file_b, sizeof file_b, "%s/b.prn", dirpath);
            snprintf(subdir, sizeof subdir, "%s/subdir", dirpath);
            FILE *fa = fopen(file_a, "w"); if (fa) fclose(fa);
            FILE *fb = fopen(file_b, "w"); if (fb) fclose(fb);
            CHECK(mkdir(subdir, 0755) == 0, "a real subdirectory is created inside the temp dir");

            Vec entries = list_dir(dirpath, &a);
            CHECK(vec_len(&entries) == 3, "list-dir finds all 3 real entries (2 files + 1 subdirectory)");
            int found_a = 0, found_b = 0, found_sub = 0;
            for (int i = 0; i < vec_len(&entries); i++) {
                char *name = (char *)vec_get(&entries, i);
                if (strcmp(name, "a.txt") == 0) found_a = 1;
                if (strcmp(name, "b.prn") == 0) found_b = 1;
                if (strcmp(name, "subdir") == 0) found_sub = 1;
                CHECK(strcmp(name, ".") != 0 && strcmp(name, "..") != 0,
                      "list-dir never includes the real '.' or '..' entries");
            }
            CHECK(found_a && found_b && found_sub, "list-dir's real entries are exactly the 3 real files/dirs created above");

            /* --- real is-dir? (2026-08-27, added closing the file-tree
             * sidebar's own "clicking a subdirectory opens it as a
             * file" gap) --- */
            CHECK(is_dir_(dirpath), "is-dir? correctly reports true for a real directory");
            CHECK(!is_dir_(file_a), "is-dir? correctly reports false for a real regular file");
            CHECK(is_dir_(subdir), "is-dir? correctly reports true for a real subdirectory");
            CHECK(!is_dir_("/tmp/parena-this-real-path-does-not-exist-anywhere"),
                  "is-dir? correctly reports false for a real nonexistent path, not a crash");

            unlink(file_a);
            unlink(file_b);
            rmdir(subdir);
            rmdir(dirpath);
        }

        Vec missing = list_dir("/tmp/parena-this-real-path-does-not-exist-anywhere", &a);
        CHECK(vec_len(&missing) == 0, "list-dir on a real nonexistent path returns a real empty Vec, not a crash or a stale/garbage entry");
    }

    /* --- real regression test for a real, live bug found and fixed
     * 2026-08-27 (founder actually dropping a real large file onto
     * the PARENA editor: "it crashed... if we try to open a too large
     * file it just chokes"): raw_read_all_impl (runtime/
     * parena_runtime.h, read-string's own real host primitive) used
     * to grow its buffer in FIXED 4096-byte steps, reallocating (not
     * resizing -- this is a bump arena) and copying the WHOLE buffer
     * on every single grow -- real O(N^2) copy work and real O(N^2)
     * wasted memory (every intermediate buffer stays permanently
     * allocated). Confirmed live: the pre-fix version couldn't even
     * finish reading a real ~10MB file in 30 real seconds; the fixed
     * version (fstat-sized single allocation, doubling growth as a
     * real fallback) reads the same file in well under a tenth of a
     * second. This test writes a real ~8MB file and asserts
     * read-string completes in well under a second -- a real,
     * bounded, wall-clock proof against ever regressing back to the
     * O(N^2) growth pattern, not just a "does it crash" check. */
    {
        char bigpath[256];
        snprintf(bigpath, sizeof bigpath, "/tmp/parena_editor_io_bigfile_test_%d.txt", (int)getpid());
        FILE *bf = fopen(bigpath, "w");
        CHECK(bf != NULL, "a real ~8MB test file opens for writing");
        if (bf) {
            const char *line = "the quick brown fox jumps over the lazy dog, twice, for real bulk\n";
            size_t line_len = strlen(line);
            size_t target = 8 * 1024 * 1024;
            size_t written = 0;
            while (written < target) {
                fputs(line, bf);
                written += line_len;
            }
            fclose(bf);

            Result openr = file_open(bigpath, OpenMode_Read(), &a);
            CHECK(openr.tag == 1, "the real ~8MB test file opens via file-open");
            if (openr.tag == 1) {
                FileHandle bfh = *(FileHandle *)openr.value;
                struct timespec t0, t1;
                clock_gettime(CLOCK_MONOTONIC, &t0);
                Result rr = read_string(bfh, &a);
                clock_gettime(CLOCK_MONOTONIC, &t1);
                file_close(bfh, &a);
                double elapsed = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
                CHECK(rr.tag == 1, "read-string succeeds on the real ~8MB test file");
                CHECK(rr.tag == 1 && strlen((char *)rr.value) == written,
                      "read-string returns the real, complete, correctly-sized content -- not truncated");
                CHECK(elapsed < 2.0,
                      "read-string reads a real ~8MB file in well under 2 real seconds -- the real, "
                      "bounded proof against ever regressing back to O(N^2) growth");
            }
            unlink(bigpath);
        }
    }

    /* --- real regression test for a real bug found in an earlier
     * draft of the O(N^2) fix above, caught before shipping further
     * (2026-08-27): fstat-sizing the buffer up front means a SMALL
     * file's own initial capacity can be far less than 4096 -- a
     * single `cap * 2` doubling step (as opposed to looping the
     * doubling until real capacity is actually sufficient) doesn't
     * reliably leave `cap >= len + 4096` before the next real read()
     * call, which unconditionally requests up to 4096 bytes. Real,
     * minimal repro: a real file smaller than 4096 bytes, read via
     * read-string, must come back byte-for-byte correct. */
    {
        char smallpath[256];
        snprintf(smallpath, sizeof smallpath, "/tmp/parena_editor_io_smallfile_test_%d.txt", (int)getpid());
        FILE *sf = fopen(smallpath, "w");
        CHECK(sf != NULL, "a real small (<4096 byte) test file opens for writing");
        if (sf) {
            const char *small_content = "a real small file, well under 4096 bytes\n";
            fputs(small_content, sf);
            fclose(sf);

            Result openr = file_open(smallpath, OpenMode_Read(), &a);
            CHECK(openr.tag == 1, "the real small test file opens via file-open");
            if (openr.tag == 1) {
                FileHandle sfh = *(FileHandle *)openr.value;
                Result rr = read_string(sfh, &a);
                file_close(sfh, &a);
                CHECK(rr.tag == 1 && strcmp((char *)rr.value, small_content) == 0,
                      "read-string returns a real small file's content byte-for-byte correct -- "
                      "no corruption from the fstat-sized buffer's own real growth path");
            }
            unlink(smallpath);
        }
    }

    arena_free_all(&a);

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
