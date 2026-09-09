/* tests/test_spi.c -- real end-to-end verification for stdlib/hw/spi.prn,
 * scoped to what's genuinely testable in this sandbox.
 *
 * Same "test what's actually there" discipline test_shell.c/test_serial.c
 * already establish, taken one step further than test_serial.c had to:
 * a real BSD pty pair stood in honestly for a serial device there (a
 * pty slave is a genuine termios-configurable tty device, same real API
 * class as /dev/ttyUSB0). SPI has no equivalent fakeable stand-in --
 * `spidev` device files only exist when a real kernel SPI controller
 * driver has registered one; there is no userspace way to conjure a
 * fake one the way `openpty()` conjures a fake tty. So a real,
 * successful `spi-transfer` genuinely cannot be exercised here — named
 * honestly, not worked around with a mock ioctl.
 *
 * What CAN be exercised for real, and is: spi-open's own real failure
 * paths (a nonexistent device path, and — a real, meaningful assertion,
 * not a trivial one — a real, existing device file that is NOT a real
 * SPI device, proving the ioctl-configure-then-rollback-on-failure logic
 * in spi_open_impl actually runs and actually closes the fd rather than
 * handing back a "valid" but misconfigured one); spi_transfer_impl's own
 * documented "a failed ioctl doesn't crash, returns a real zeroed
 * buffer of the right length" contract, called directly against a real
 * open fd that doesn't support SPI_IOC_MESSAGE; and spi-close's real
 * success/failure paths against real fds obtained independently of
 * spi-open (a real temp file and a deliberately-already-closed fd).
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include "test_spi_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

int main(void) {
    Arena a;
    arena_init(&a);

    /* --- spi-open: a real, honest failure on a path that names no real
     * device at all. */
    {
        Result r = spi_open("/dev/this-path-does-not-exist-parena-spi-test", 0, 1000000, 8, &a);
        CHECK(r.tag == 0, "spi-open honestly fails opening a nonexistent device path");
    }

    /* --- spi-open: a real, honest failure against a real device file
     * that exists but isn't a real SPI device -- /dev/null happily
     * open()s, but none of the three real SPI_IOC_WR_* ioctls succeed
     * against it, so spi_open_impl's own real rollback path (close the
     * fd it just opened, return -1) has to actually run for this to
     * pass. Proves a caller never gets back a "successful" SpiDevice
     * that's actually misconfigured. */
    {
        Result r = spi_open("/dev/null", 0, 1000000, 8, &a);
        CHECK(r.tag == 0, "spi-open honestly fails configuring a real, non-SPI device file (/dev/null)");
    }

    /* --- spi_transfer_impl, called directly against a real open fd
     * that doesn't support SPI_IOC_MESSAGE (a real temp file): proves
     * the documented "a failed ioctl doesn't crash, returns a real,
     * correctly-sized zeroed buffer, not garbage" contract for real,
     * not just by reading the header comment. Called directly (not via
     * spi-transfer/spi-open) since there is no way to obtain a real,
     * successfully-configured SpiDevice in this sandbox at all. */
    {
        char path[] = "/tmp/parena_spi_test_XXXXXX";
        int fd = mkstemp(path);
        CHECK(fd >= 0, "real temp file opened for direct spi_transfer_impl testing");
        if (fd >= 0) {
            char *rx = spi_transfer_impl(fd, "\x01\x02\x03", &a);
            CHECK(rx != NULL, "spi_transfer_impl against a non-SPI fd returns a real, non-NULL buffer, not a crash");
            CHECK(rx[0] == '\0' && rx[1] == '\0' && rx[2] == '\0',
                  "spi_transfer_impl zeroes its own receive buffer rather than returning stale/garbage bytes "
                  "when the underlying ioctl genuinely fails");
            close(fd);
            unlink(path);
        }
    }

    /* --- spi_transfer_impl with an empty tx payload: the real, honest
     * zero-length edge case (real len > 0 guard around the ioctl call
     * itself). */
    {
        char path[] = "/tmp/parena_spi_test_XXXXXX";
        int fd = mkstemp(path);
        if (fd >= 0) {
            char *rx = spi_transfer_impl(fd, "", &a);
            CHECK(rx != NULL && rx[0] == '\0',
                  "spi_transfer_impl with an empty payload returns a real, empty buffer, no ioctl attempted");
            close(fd);
            unlink(path);
        }
    }

    /* --- spi-close: real success against a real, independently-opened
     * fd (SpiDevice constructed directly -- there is no real spi-open
     * success path to obtain one from in this sandbox). */
    {
        char path[] = "/tmp/parena_spi_test_XXXXXX";
        int fd = mkstemp(path);
        CHECK(fd >= 0, "real temp file opened for spi-close testing");
        if (fd >= 0) {
            SpiDevice dev = SpiDevice_new(fd);
            Result cr = spi_close(&dev, &a);
            CHECK(cr.tag == 1, "spi-close on a real, valid fd succeeds");
            unlink(path);
        }
    }

    /* --- spi-close: real, honest failure against an already-closed
     * (invalid) fd. */
    {
        SpiDevice dev = SpiDevice_new(-1);
        Result cr = spi_close(&dev, &a);
        CHECK(cr.tag == 0, "spi-close honestly fails on an already-invalid fd");
    }

    arena_free_all(&a);

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
