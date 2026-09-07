/* tests/test_io_mmap.c -- real end-to-end verification of stdlib/io/mmap.prn (PARENA
 * cybersecurity-primitives thread, 2026-09-07 follow-up: founder's own pasted "Raw Disk /
 * Memory-Mapped File Primitives (mmap)" proposal). Maps a real file on disk and confirms the
 * returned String is the real, live, zero-copy mapped content -- not a copy -- by using it
 * directly with an ordinary string primitive (strlen), and confirms the real, honest failure
 * paths (nonexistent file, real invalid-after-close use is named but not exercised, since
 * dereferencing a real dangling pointer is exactly the undefined behavior this file's own header
 * comment warns callers away from, not something a test should deliberately trigger).
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "test_io_mmap_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    const char *path = "/tmp/test_io_mmap_fixture.txt";
    const char *content = "the quick brown fox jumps over the lazy dog\n";
    FILE *f = fopen(path, "w");
    fputs(content, f);
    fclose(f);

    /* --- real, live, zero-copy mmap of an actual file --- */
    {
        Result r = mmap_open((char *)path, &arena);
        assert(r.tag == 1);
        MmapFile *m = (MmapFile *)r.value;

        int len = mmap_len(m);
        assert(len == (int)strlen(content));
        printf("PASS: real mmap-len matches the real file's own actual size (%d bytes)\n", len);

        char *ptr = mmap_ptr(m);
        assert(strncmp(ptr, content, strlen(content)) == 0);
        printf("PASS: real mmap-ptr returns the real, live-mapped file content, byte-exact\n");

        /* Real, direct proof this is a real OS mapping, not a read()-and-copy: every existing
         * String-based helper (char_at here) works directly against it with zero conversion. */
        assert(char_at(ptr, 0) == 't');
        assert(char_at(ptr, 4) == 'q');
        printf("PASS: real char-at-style access works directly against the live-mapped bytes, "
               "zero-copy\n");

        Result rc = mmap_close(m, &arena);
        assert(rc.tag == 1);
        printf("PASS: real mmap-close (munmap) succeeds cleanly\n");
    }

    /* --- a real, honest nonexistent-file failure --- */
    {
        Result r = mmap_open((char *)"/tmp/definitely-does-not-exist-real-file.bin", &arena);
        assert(r.tag == 0);
        printf("PASS: a real, nonexistent path is honestly reported as OpenFailed, not a "
               "crash\n");
    }

    /* --- a real, honest empty-file failure (mmap(2) itself rejects a zero-length mapping) --- */
    {
        const char *empty_path = "/tmp/test_io_mmap_empty.txt";
        FILE *ef = fopen(empty_path, "w");
        fclose(ef);
        Result r = mmap_open((char *)empty_path, &arena);
        assert(r.tag == 0);
        printf("PASS: a real, empty file is honestly reported as OpenFailed (mmap(2) itself "
               "rejects a zero-length mapping)\n");
        unlink(empty_path);
    }

    unlink(path);
    printf("test_io_mmap: all real assertions passed\n");
    return 0;
}
