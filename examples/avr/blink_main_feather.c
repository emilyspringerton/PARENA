/* examples/avr/blink_main_feather.c — real, hand-written AVR host for the Adafruit Feather 32u4
 * (ATmega32u4, EDGE.GAME S584), wiring the SAME PARENA decision logic (blink_gen.c's
 * next_led_state) as blink_main.c, but to the real, DIFFERENT hardware facts a 32u4-class board
 * has versus the Uno/Nano's ATmega328p that blink_main.c targets:
 *
 * - LED pin: an ATmega32u4-based board (Leonardo/Micro/Feather 32u4 all share this core pin
 *   mapping) wires its onboard/digital-pin-13 LED to PC7, NOT the ATmega328p's PB5 -- this is
 *   standard, widely-documented Arduino AVR-core board convention (the same "well-known board
 *   fact" standing blink_main.c's own PB5 comment already claims for the Uno), not something
 *   independently re-derived from a schematic in this sandbox. Reusing blink_main.c unmodified
 *   against this chip would silently toggle the WRONG pin -- confirmed as a real, distinct bug
 *   caught before this file existed, not a hypothetical.
 * - Clock speed: Adafruit's own published Feather 32u4 spec runs it at 8MHz (3.3V logic), half
 *   the Uno/Nano's 16MHz/5V -- `_delay_loop_2`'s cycle-count math is clock-speed-dependent, so
 *   blink_main.c's own outer-loop constant (100, tuned for 16MHz) would blink at DOUBLE the
 *   intended real-world duration if reused as-is here.
 *
 * Real, honest v0 constraint, same as blink_main.c's own: no physical Feather exists in this
 * sandbox, so this is verified to compile clean for atmega32u4 (`make avr-feather-blink-hex`) and
 * to correctly ATTEMPT the touch-reset + flash (`make avr-feather-blink-upload`), never against
 * the board's own real USB-CDC enumeration or LED. */
#include <avr/io.h>
#include <util/delay_basic.h>
#include "blink_gen.c"

int main(void) {
    /* Set PC7 (digital pin 13 on a 32u4-class board) as an output. */
    DDRC |= (1 << PC7);

    int led_on = 0;
    for (;;) {
        led_on = next_led_state(led_on);
        if (led_on) {
            PORTC |= (1 << PC7);
        } else {
            PORTC &= (uint8_t)~(1 << PC7);
        }
        /* ~1.5s at 8MHz: _delay_loop_2 is 4 cycles/iteration, so matching blink_main.c's own real,
         * documented 1.5s blink at HALF the clock needs HALF the total iterations -- 50 * 60000 * 4
         * / 8e6 ≈ 1.5s (blink_main.c's own 16MHz math is 100 * 60000 * 4 / 16e6 ≈ 1.5s; same
         * per-iteration cycle cost, half the clock, half the outer count for the same real time). */
        for (int i = 0; i < 50; i++) {
            _delay_loop_2(60000);
        }
    }
    return 0;
}
