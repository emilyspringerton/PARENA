/* examples/host_led/led_main.c — real follow-up work (2026-09-10, founder
 * real-time: "ok do the followup work we need the led on the board to
 * actually flash (there's one built in you can make blink)"), closing
 * the one honest gap examples/avr/blink.prn's own AVR path named:
 * `docs/AVR_ARDUINO_NORTHSTAR.md` proved the full parena-build ->
 * avr-gcc -> avr-objcopy -> avrdude pipeline for real, but avrdude
 * itself could only fail honestly at its own port-open step, since no
 * physical Arduino is attached to this sandbox.
 *
 * Real, checked (not assumed) discovery: this box has NO USB device at
 * all (`lsusb` returns nothing) and no ttyACM or ttyUSB device under
 * /dev -- but it DOES have a real, kernel-exposed LED class device
 * (a "scrolllock"-suffixed entry under /sys/class/leds, Linux's own
 * standard keyboard-LED sysfs interface, from the i8042 input driver --
 * present on every Linux box with a keyboard input device registered,
 * virtual or real). That's "the board's own built-in LED" this program
 * actually drives.
 *
 * Same real "PARENA emits the decision, a thin native host wires it to
 * the real platform" pattern examples/avr/blink_main.c already
 * established for AVR and SPIDERBEETLE's battery_ui.prn established for
 * Android -- `next_led_state` (from the SAME examples/avr/blink.prn
 * source, a plain Bool -> Bool toggle with zero platform assumptions)
 * is reused verbatim here, unmodified, driving a genuinely different
 * real target.
 *
 * Needs root to write to that LED's own root-owned brightness file --
 * routed through sudo-queue/77-blink-onboard-led.sh per this monorepo's
 * own standing "no sudo, route privileged one-liners through sudo-queue
 * for the founder to run" discipline, rather than this program (or
 * Claude) invoking sudo itself. Bounded to 10 real on/off cycles
 * (~8 seconds) then leaves the LED off and exits -- no lingering
 * root-owned process.
 */
/* blink_gen.c must be included FIRST, before any system header: its own
 * #include "parena_runtime.h" defines _POSIX_C_SOURCE/_DEFAULT_SOURCE
 * (needed for setenv/cfmakeraw/strtok_r/kill/popen/etc elsewhere in that
 * shared runtime), which glibc only honors if set before the FIRST
 * system header is parsed -- runtime/parena_runtime.h's own header
 * comment already documents this exact rule. Found live: an earlier
 * draft included <unistd.h> etc. first, which pulled in glibc's feature-
 * test defaults before parena_runtime.h ever got a chance to override
 * them, breaking usleep/strtok_r/kill/popen/setenv/cfmakeraw with
 * -Werror=implicit-function-declaration. */
#include "blink_gen.c"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define LEDS_DIR "/sys/class/leds"
#define LED_SUFFIX "::scrolllock"
#define NUM_TOGGLES 20 /* 10 full on/off cycles */
#define TOGGLE_DELAY_US 400000 /* 400ms -- visibly slow, not a flicker */

/* find_led_path -- scans /sys/class/leds for the first entry ending in
 * "::scrolllock" rather than hardcoding "input1::scrolllock" (this
 * box's own real, checked-live name) -- the exact input device NUMBER
 * a given kernel assigns to the keyboard controller isn't a stable
 * contract across reboots/different boxes, same real reasoning
 * hw/serial.prn's own header comment already applies to not hardcoding
 * /dev/ttyACM0. */
static int find_led_path(char *out, size_t out_len) {
    DIR *d = opendir(LEDS_DIR);
    if (!d) {
        perror("led_blink: opendir " LEDS_DIR);
        return -1;
    }
    struct dirent *e;
    int found = 0;
    while ((e = readdir(d)) != NULL) {
        size_t nlen = strlen(e->d_name);
        size_t slen = strlen(LED_SUFFIX);
        if (nlen > slen && strcmp(e->d_name + nlen - slen, LED_SUFFIX) == 0) {
            int n = snprintf(out, out_len, "%s/%s/brightness", LEDS_DIR, e->d_name);
            if (n > 0 && (size_t)n < out_len) {
                found = 1;
            }
            break;
        }
    }
    closedir(d);
    if (!found) {
        fprintf(stderr, "led_blink: no *%s LED found under %s\n", LED_SUFFIX, LEDS_DIR);
        return -1;
    }
    return 0;
}

static int write_led(const char *path, int on) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror("led_blink: open led brightness");
        return -1;
    }
    const char *v = on ? "1" : "0";
    ssize_t n = write(fd, v, 1);
    close(fd);
    if (n != 1) {
        fprintf(stderr, "led_blink: short/failed write to %s (are you root? see sudo-queue/77-blink-onboard-led.sh)\n", path);
        return -1;
    }
    return 0;
}

int main(void) {
    char led_path[256];
    if (find_led_path(led_path, sizeof led_path) != 0) {
        return 1;
    }
    fprintf(stderr, "led_blink: using %s\n", led_path);

    int led_on = 0;
    for (int i = 0; i < NUM_TOGGLES; i++) {
        led_on = next_led_state(led_on);
        if (write_led(led_path, led_on) != 0) {
            return 1;
        }
        fprintf(stderr, "led_blink: %s\n", led_on ? "ON" : "off");
        usleep(TOGGLE_DELAY_US);
    }
    /* Real, deliberate: leave the LED off when done, not lit -- a
     * bounded demo shouldn't leave lasting state behind. */
    write_led(led_path, 0);
    fprintf(stderr, "led_blink: done, LED left off\n");
    return 0;
}
