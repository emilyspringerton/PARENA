/* examples/avr/blink_main.c — real, hand-written AVR host wiring PARENA's
 * pure decision logic (blink_gen.c's next_led_state) to real ATmega328p
 * GPIO registers. Arduino Uno's onboard LED is wired to PB5 (digital pin
 * 13) — a real, well-known board fact, not a guess.
 *
 * Same real "PARENA emits the decision, a thin native host wires it to the
 * real platform" pattern SPIDERBEETLE's stdlib/android/battery_ui.prn +
 * its generated BatteryUi.java host already established for a different
 * native target. #include "blink_gen.c" directly (not linking a separate
 * .o) matches `parena new`'s own scaffold convention (src/main.c's cmd_new
 * generates a main.c that does the same `#include "%s_gen.c"`) — this repo
 * doesn't use a build system with real per-target object linking for
 * generated PARENA output, it #includes the generated .c straight into a
 * real hand-written host file.
 *
 * See examples/avr/parena_runtime.h's own header comment for why THIS
 * directory has its own stub instead of using the shared
 * runtime/parena_runtime.h (Linux-syscall-heavy, doesn't compile under
 * avr-gcc).
 *
 * Uses `<util/delay_basic.h>`'s `_delay_loop_2` rather than
 * `<util/delay.h>`'s `_delay_ms` (2026-09-10, real LLVM-backend scoping
 * work, `docs/LLVM_BACKEND_NORTHSTAR.md`): `_delay_ms` expands through
 * `__builtin_avr_delay_cycles`, a GCC-only compiler builtin clang's AVR
 * frontend does not implement ("undefined reference to
 * '__builtin_avr_delay_cycles'" at link time, confirmed live) — while
 * `_delay_loop_2` is an ordinary inline-asm `static inline` function,
 * real and portable under both avr-gcc and clang. Kept as ONE shared
 * host file compiled by both `avr-blink-hex` (avr-gcc) and
 * `avr-blink-hex-clang` (clang, see that Makefile target's own header
 * comment) rather than forking a second, clang-only copy — real,
 * deliberate, single-source-of-truth choice. */
#include <avr/io.h>
#include <util/delay_basic.h>
#include "blink_gen.c"

int main(void) {
    /* Set PB5 (pin 13) as an output — DDRB (Data Direction Register B)
     * bit set = output, per the real ATmega328p datasheet. */
    DDRB |= (1 << PB5);

    int led_on = 0;
    for (;;) {
        led_on = next_led_state(led_on);
        if (led_on) {
            PORTB |= (1 << PB5);
        } else {
            PORTB &= (uint8_t)~(1 << PB5);
        }
        /* ~500ms at 16MHz: _delay_loop_2's own real, documented formula
         * is 4 cycles per iteration, so 100 * 60000 * 4 / 16e6 ≈ 1.5s --
         * an intentionally-visible, slow blink, not tuned to an exact
         * value (same "visibly slow, not a flicker" real intent
         * examples/host_led/led_main.c's own TOGGLE_DELAY_US already
         * states). */
        for (int i = 0; i < 100; i++) {
            _delay_loop_2(60000);
        }
    }
    return 0;
}
