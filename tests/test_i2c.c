/* tests/test_i2c.c -- real end-to-end verification for stdlib/hw/i2c.prn.
 *
 * Same "test what's actually there" discipline test_shell.c/test_serial.c/
 * test_spi.c already establish, but I2C's own real shape lets this go
 * further than SPI's own test could: i2c_read_impl/i2c_write_impl are
 * PLAIN POSIX read()/write() syscalls once a device is open (see this
 * file's own header comment on i2c-dev's real "ioctl once to fix the
 * slave address, then ordinary read/write" convention) -- the actual
 * byte-transfer mechanics need no real I2C hardware or ioctl to exercise
 * honestly, unlike SPI's own necessarily-ioctl-gated transfer. So this
 * file proves a REAL, byte-perfect round trip (not just failure paths)
 * against a real regular temp file standing in for an open I2C device
 * fd -- a real, honest, meaningful stand-in for this specific piece,
 * since the temp file exercises the exact same read()/write() code path
 * i2c_read_impl/i2c_write_impl actually run against a real character
 * device.
 *
 * What genuinely CANNOT be tested here, named honestly rather than
 * worked around: this actual box has a real, live root-owned
 * `/dev/i2c-0` (an Intel SMBus controller) -- deliberately NOT opened
 * or probed anywhere in this file (see runtime/parena_runtime.h's own
 * header comment on this section for the full reasoning: an SMBus
 * commonly carries real battery/thermal/RAM-SPD system traffic, and
 * this stdlib work doesn't need to risk an unreviewed real transaction
 * against real system hardware to be correct). So the real
 * `I2C_SLAVE` ioctl's SUCCESS path (i2c-open actually succeeding
 * against a genuine I2C device) is not exercised — only its honest
 * failure path is, against real non-I2C files where the real kernel
 * itself refuses the ioctl.
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include "test_i2c_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

int main(void) {
    Arena a;
    arena_init(&a);

    /* --- i2c-open: a real, honest failure on a path that names no real
     * device at all. */
    {
        Result r = i2c_open("/dev/this-path-does-not-exist-parena-i2c-test", 0x42, &a);
        CHECK(r.tag == 0, "i2c-open honestly fails opening a nonexistent device path");
    }

    /* --- i2c-open: a real, honest failure against a real file that
     * exists but isn't a real I2C device -- open() itself succeeds
     * against a regular temp file, but the real kernel's own
     * I2C_SLAVE ioctl genuinely refuses it (ENOTTY, not a real i2c
     * adapter), so i2c_open_impl's own rollback (close the fd it just
     * opened, return -1) has to actually run for this to pass. */
    {
        char path[] = "/tmp/parena_i2c_test_XXXXXX";
        int fd = mkstemp(path);
        CHECK(fd >= 0, "real temp file created for the not-a-real-i2c-device test");
        if (fd >= 0) close(fd);
        Result r = i2c_open(path, 0x42, &a);
        CHECK(r.tag == 0, "i2c-open honestly fails configuring a real, non-I2C file via I2C_SLAVE");
        unlink(path);
    }

    /* --- real, byte-perfect round trip against a real temp file fd,
     * calling the runtime's own i2c_write_impl/i2c_read_impl directly
     * (bypassing i2c-open's ioctl gate, which cannot succeed in this
     * sandbox -- see this file's own header comment). Proves the real
     * read()/write() mechanics -- including i2c-read's own explicit
     * length contract -- work correctly against a real fd, not just
     * that they compile. */
    {
        char path[] = "/tmp/parena_i2c_test_XXXXXX";
        int fd = mkstemp(path);
        CHECK(fd >= 0, "real temp file opened for the round-trip test");
        if (fd >= 0) {
            int wr = i2c_write_impl(fd, "real-i2c-rw-test");
            CHECK(wr == 0, "i2c_write_impl writes real bytes to a real fd successfully");

            CHECK(lseek(fd, 0, SEEK_SET) == 0, "real fd seeked back to the start for readback");

            char *rx = i2c_read_impl(fd, 17, &a); /* strlen("real-i2c-rw-test") == 17 */
            CHECK(rx != NULL && strcmp(rx, "real-i2c-rw-test") == 0,
                  "i2c_read_impl reads back the exact real bytes just written, byte-perfect");

            close(fd);
        }
        unlink(path);
    }

    /* --- i2c_write_bytes_impl/i2c_read_bytes_impl, the real,
     * byte-perfect siblings (docs/BYTES_NORTHSTAR.md's own real Phase 2
     * retrofit), called directly against a real temp file fd -- same
     * real reason as the String-based round trip above: the actual
     * data-transfer mechanics are plain POSIX read()/write(), needing
     * no real I2C hardware to exercise honestly. The one thing worth
     * proving above everything else: a genuine embedded 0x00 byte
     * survives a real write-then-read round trip intact, with the
     * reported length unaffected -- exactly what the String-based pair
     * above cannot do. */
    {
        char path[] = "/tmp/parena_i2c_test_XXXXXX";
        int fd = mkstemp(path);
        CHECK(fd >= 0, "real temp file opened for the bytes-sibling round-trip test");
        if (fd >= 0) {
            unsigned char payload[9] = {'r', 'e', 'a', 'l', 0, 'i', '2', 'c', '!'};
            Bytes tx = bytes_alloc(9, &a);
            for (int i = 0; i < 9; i++) bytes_set_(tx, i, payload[i]);

            int wr = i2c_write_bytes_impl(fd, tx);
            CHECK(wr == 0, "i2c_write_bytes_impl writes a real payload with an embedded 0x00 successfully");

            CHECK(lseek(fd, 0, SEEK_SET) == 0, "real fd seeked back to the start for the bytes-sibling readback");

            Bytes rx = i2c_read_bytes_impl(fd, 9, &a);
            CHECK(bytes_len(rx) == 9,
                  "i2c_read_bytes_impl reports the real, full 9-byte length -- unaffected by the "
                  "embedded 0x00, unlike i2c_read_impl's own strlen-based String");
            CHECK(bytes_get(rx, 4) == 0, "the real embedded 0x00 byte itself reads back correctly");
            CHECK(bytes_get(rx, 5) == 'i' && bytes_get(rx, 8) == '!',
                  "the bytes after the embedded 0x00 are not lost -- 'i2c!' survives intact");

            close(fd);
        }
        unlink(path);
    }

    /* --- i2c_read_bytes_impl's own real, honest short-read handling,
     * matching i2c_read_impl's own established convention: the
     * REPORTED length reflects what was actually read, not what was
     * requested. */
    {
        char path[] = "/tmp/parena_i2c_test_XXXXXX";
        int fd = mkstemp(path);
        if (fd >= 0) {
            unsigned char short_payload[3] = {1, 0, 2}; /* a real embedded 0x00, still just 3 real bytes */
            CHECK(write(fd, short_payload, 3) == 3, "real 3-byte file (with an embedded 0x00) written for the short-read test");
            CHECK(lseek(fd, 0, SEEK_SET) == 0, "real fd seeked back to the start");

            Bytes rx = i2c_read_bytes_impl(fd, 100, &a); /* asks for far more than the file has */
            CHECK(bytes_len(rx) == 3,
                  "i2c_read_bytes_impl honestly reports the real, short length actually read, not the "
                  "100 bytes requested -- and not fooled by the embedded 0x00 into reporting even less");
            CHECK(bytes_get(rx, 1) == 0 && bytes_get(rx, 2) == 2,
                  "the short read's own real embedded 0x00 byte and the byte after it both survive intact");

            close(fd);
        }
        unlink(path);
    }

    /* --- i2c_read_impl's own real, honest short-read handling: asking
     * for more bytes than the file actually has must return exactly
     * what's available, NUL-terminated there, not hang or error. */
    {
        char path[] = "/tmp/parena_i2c_test_XXXXXX";
        int fd = mkstemp(path);
        if (fd >= 0) {
            CHECK(write(fd, "short", 5) == 5, "real 5-byte file written for the short-read test");
            CHECK(lseek(fd, 0, SEEK_SET) == 0, "real fd seeked back to the start");

            char *rx = i2c_read_impl(fd, 100, &a); /* asks for far more than the file has */
            CHECK(rx != NULL && strcmp(rx, "short") == 0,
                  "i2c_read_impl honestly returns exactly the bytes actually available on a short read, "
                  "not a hang or an error");

            close(fd);
        }
        unlink(path);
    }

    /* --- i2c_read_impl with a zero-length request: real, honest empty
     * result, no real read() syscall even attempted. */
    {
        char path[] = "/tmp/parena_i2c_test_XXXXXX";
        int fd = mkstemp(path);
        if (fd >= 0) {
            char *rx = i2c_read_impl(fd, 0, &a);
            CHECK(rx != NULL && rx[0] == '\0',
                  "i2c_read_impl with a zero-length request returns a real, empty buffer");
            close(fd);
        }
        unlink(path);
    }

    /* --- i2c-close: real success against a real, independently-opened
     * fd (I2cDevice constructed directly -- there is no real i2c-open
     * success path to obtain one from in this sandbox). */
    {
        char path[] = "/tmp/parena_i2c_test_XXXXXX";
        int fd = mkstemp(path);
        CHECK(fd >= 0, "real temp file opened for i2c-close testing");
        if (fd >= 0) {
            I2cDevice dev = I2cDevice_new(fd);
            Result cr = i2c_close(&dev, &a);
            CHECK(cr.tag == 1, "i2c-close on a real, valid fd succeeds");
        }
        unlink(path);
    }

    /* --- i2c-close: real, honest failure against an already-invalid
     * fd. */
    {
        I2cDevice dev = I2cDevice_new(-1);
        Result cr = i2c_close(&dev, &a);
        CHECK(cr.tag == 0, "i2c-close honestly fails on an already-invalid fd");
    }

    arena_free_all(&a);

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
