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
 */
#include <avr/io.h>
#include <util/delay.h>
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
        _delay_ms(500);
    }
    return 0;
}
