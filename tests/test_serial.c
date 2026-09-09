/* tests/test_serial.c -- real end-to-end verification for
 * stdlib/hw/serial.prn (docs/UART_SERIAL_NORTHSTAR.md, kanban cards
 * HW-001/HW-003).
 *
 * Same "test what's actually there" discipline test_shell.c already
 * establishes for pty.prn: this sandbox has no physical USB-serial
 * device (UART_SERIAL_NORTHSTAR.md's own Phase 2 names this real,
 * honest limitation up front), so a real Arduino echo-sketch round-trip
 * genuinely cannot be run here. What CAN be run here, and is, against a
 * real (not mocked) OS device: a real BSD pty pair. A pty slave is a
 * genuine termios-configurable tty device -- the exact same tcgetattr/
 * cfmakeraw/cfsetispeed/cfsetospeed/tcsetattr sequence serial_configure_
 * impl runs against a real /dev/ttyUSB0 runs unmodified against it, so
 * opening the slave's device path BY NAME through serial-open (not by
 * reusing the already-open fd openpty handed back) exercises the real
 * open()+configure() sequence a real caller would go through, not a
 * shortcut around it. The master side stands in for "the real
 * microcontroller on the other end of the wire" -- every read/write
 * against it is a real syscall on a real fd, not a stub.
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "test_serial_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

/* open_test_pty -- real openpty(3) pair; returns the slave's real
 * device path (e.g. /dev/pts/7) by name, closing our own copy of the
 * slave fd immediately so serial-open's own real_serial_raw_open_impl
 * is the only thing that (re)opens it -- proving the by-path open
 * itself works, not just read/write against an fd we already held. */
static int open_test_pty(int *master_out, char *path_out, size_t path_cap) {
    int master_fd, slave_fd;
    if (openpty(&master_fd, &slave_fd, NULL, NULL, NULL) != 0) return -1;
    if (ttyname_r(slave_fd, path_out, path_cap) != 0) {
        close(master_fd);
        close(slave_fd);
        return -1;
    }
    close(slave_fd);
    *master_out = master_fd;
    return 0;
}

int main(void) {
    Arena a;
    arena_init(&a);

    /* --- serial-open: real open()+termios-configure against a real
     * pty slave device, by path. */
    {
        int master_fd;
        char path[64];
        CHECK(open_test_pty(&master_fd, path, sizeof path) == 0,
              "real pty pair created for serial-open testing");

        Result r = serial_open(path, 9600, &a);
        CHECK(r.tag == 1, "serial-open succeeds against a real tty device at 9600 baud");
        if (r.tag == 1) {
            SerialPort p = *(SerialPort *)r.value;
            CHECK(p.fd >= 0, "serial-open's own opened port has a real, valid fd");

            /* --- real round trip, master -> serial-read. Written in
             * raw framing (no line discipline in the way, unlike the
             * device's original cooked-mode default), a real
             * microcontroller-shaped byte stream with no trailing
             * newline required. Bounded poll loop, same real per-frame
             * shape test_shell.c's own pty-poll-read check uses --
             * proves this is really non-blocking, not a lucky single
             * call. */
            CHECK(write(master_fd, "real-serial-rx-test", 20) == 20,
                  "writing raw bytes from the 'device' side (pty master) succeeds");

            char *found = NULL;
            for (int attempt = 0; attempt < 40 && !found; attempt++) {
                usleep(50000);
                Result rr = serial_read(&p, &a);
                if (rr.tag == 1) {
                    char *chunk = (char *)rr.value;
                    if (chunk && strstr(chunk, "real-serial-rx-test")) found = chunk;
                }
            }
            CHECK(found != NULL,
                  "real bytes written from the device side round-trip back through "
                  "repeated serial-read polling calls");

            /* --- real round trip, serial-write -> master read. */
            Result wr = serial_write(&p, "real-serial-tx-test", &a);
            CHECK(wr.tag == 1, "serial-write to the real tty device succeeds");
            char rbuf[64];
            memset(rbuf, 0, sizeof rbuf);
            ssize_t n = -1;
            for (int attempt = 0; attempt < 40 && n <= 0; attempt++) {
                usleep(50000);
                n = read(master_fd, rbuf, sizeof rbuf - 1);
            }
            CHECK(n > 0 && strstr(rbuf, "real-serial-tx-test") != NULL,
                  "real bytes sent via serial-write round-trip back to the device side (pty master)");

            /* --- serial-read against a genuinely idle device returns
             * immediately (poll-gated, non-blocking) -- same real wall-
             * clock proof test_shell.c's own pty-poll-read check makes;
             * if this regressed to a blocking read, this call would
             * hang for the test's own timeout instead of just running
             * slow. */
            time_t t0 = time(NULL);
            Result idle = serial_read(&p, &a);
            time_t t1 = time(NULL);
            CHECK(idle.tag == 1, "serial-read returns Ok against a real, genuinely idle device");
            CHECK((t1 - t0) < 2,
                  "serial-read returns immediately against an idle device, not blocked "
                  "waiting for bytes that aren't coming");

            Result cr = serial_close(&p, &a);
            CHECK(cr.tag == 1, "serial-close on the real opened port succeeds");
        }
        close(master_fd);
    }

    /* --- serial-open: a real, honest failure on an unsupported v0
     * baud rate (this file's own SerialError comment names this as a
     * deliberate boundary, not an oversight) -- open() itself succeeds
     * against the real device, but configure fails, and the fd it
     * opened is not leaked back to the caller as a fake success. */
    {
        int master_fd;
        char path[64];
        CHECK(open_test_pty(&master_fd, path, sizeof path) == 0,
              "real pty pair created for the unsupported-baud test");

        Result r = serial_open(path, 4800, &a);
        CHECK(r.tag == 0, "serial-open honestly fails on a real, unsupported v0 baud rate (4800)");
        close(master_fd);
    }

    /* --- serial-open: a real, honest failure on a path that names no
     * real device at all. */
    {
        Result r = serial_open("/dev/this-path-does-not-exist-parena-test", 9600, &a);
        CHECK(r.tag == 0, "serial-open honestly fails opening a nonexistent device path");
    }

    arena_free_all(&a);

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
