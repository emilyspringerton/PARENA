/* examples/avr/parena_runtime.h — real, minimal AVR-target stand-in for
 * runtime/parena_runtime.h. NOT the shared runtime: that file pulls in
 * <sys/socket.h>/<pty.h>/<sys/mman.h>/<linux/spi/spidev.h>/<linux/i2c-dev.h>
 * etc. for its real Linux-host primitives (tcp/pty/spi/i2c/serial) and
 * genuinely does not compile under avr-gcc's freestanding AVR environment —
 * confirmed directly by trying, not assumed.
 *
 * Every `parena build`-generated .c file unconditionally
 * `#include "parena_runtime.h"` (a quoted include — resolved against the
 * generated file's OWN directory first, per C's normal include-search
 * rules, before any -I path). Placing this stub in the same directory as
 * examples/avr/blink_gen.c means avr-gcc picks THIS file up instead of the
 * real runtime/parena_runtime.h, with zero compiler/emit_c.c changes and
 * zero -I trickery needed — the same "generated output lives next to its
 * own real host glue" shape `parena new`'s own scaffold (src/main.c's
 * cmd_new) already establishes for ordinary desktop targets.
 *
 * Real, deliberate, currently-minimal scope: AVR PARENA programs compiled
 * this way are pure scalar decision logic (Bool/I32/etc in, same out) —
 * see examples/avr/blink.prn's own header comment. No Region/Arena/Result/
 * String/Vec machinery is provided here because nothing on the AVR side
 * needs it yet. When a real AVR PARENA program needs one of those (a
 * genuine, later ask), it gets added here for real, not stubbed silently —
 * exactly like this repo's own runtime/parena_runtime.h grew one real
 * primitive at a time (see that file's own header comment history).
 */
#ifndef PARENA_RUNTIME_H
#define PARENA_RUNTIME_H

#include <stdint.h>
#include <stddef.h>

#endif
