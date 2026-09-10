# AVR / Arduino Northstar

## Origin

Founder real-time, 2026-09-10: "ok a pivot lets update parena so that it can run on an
arduino... we need to update the arduino editor to be able to upload to an arduino with a
button... clicking the button compiles the parena code and uploads it to the arduino... start
with making the onboard led blink." Clarified moments later, identifying which editor: "the
parena editor" — `examples/editor_main.c`, this repo's own real SDL2-windowed editor (see
`CLAUDE.md`'s "Status" section and that file's own header comments), not `SAND`/`DUNG` (planned,
not-yet-built PARENA-native editors) or `JEWEL` (the Jupyter-kernel product).

## Real, checked distinction from the existing hardware stdlib

`stdlib/hw/serial.prn`/`spi.prn`/`i2c.prn` (`docs/UART_SERIAL_NORTHSTAR.md`,
`docs/SPI_NORTHSTAR.md`, `docs/I2C_NORTHSTAR.md`) are for a **Linux host talking TO an
Arduino/peripheral** over USB-serial/SPI/I2C — this ask is the opposite direction: PARENA code
running natively ON the Arduino's own AVR chip (no Linux, no syscalls, no OS at all). These are
genuinely different problems; the existing stdlib does not solve this one.

## Why the shared runtime can't be reused

`runtime/parena_runtime.h` (what every `parena build`-generated `.c` file `#include`s) is
Linux-syscall-heavy: `<sys/socket.h>`, `<pty.h>`, `<sys/mman.h>`, `<linux/spi/spidev.h>`,
`<linux/i2c-dev.h>`, `<termios.h>`, etc. Confirmed directly (not assumed) that this does not
compile under `avr-gcc`'s freestanding AVR environment — none of those headers exist in
avr-libc.

## The real approach taken (v0)

Same shape SPIDERBEETLE's `stdlib/android/battery_ui.prn` + its generated `BatteryUi.java` host
already established for a different native target: **PARENA emits pure, scalar decision logic
with zero platform assumptions; a thin, hand-written native host wires that logic to the real
platform's own registers/APIs.**

- `examples/avr/blink.prn` — the real PARENA source. One function,
  `next-led-state : Bool -> Bool` (a plain toggle) — no I/O, no Region/Arena machinery, nothing
  AVR-specific in the PARENA code itself.
- `examples/avr/parena_runtime.h` — a real, minimal, AVR-safe stand-in for the shared runtime.
  Because `parena build`'s generated `.c` uses a **quoted** `#include "parena_runtime.h"`, C's
  own include-search rules resolve it against the generated file's own directory FIRST — placing
  this stub next to `examples/avr/blink_gen.c` makes `avr-gcc` pick it up instead of the real
  `runtime/parena_runtime.h`, with **zero compiler/`emit_c.c` changes and zero `-I` trickery**.
  Currently just `<stdint.h>`/`<stddef.h>` — nothing more is needed yet; grows one real primitive
  at a time if/when an AVR PARENA program needs one, same convention the real
  `runtime/parena_runtime.h` itself grew by (see that file's own header comment history).
- `examples/avr/blink_main.c` — the real, hand-written AVR host. `#include`s the generated
  `blink_gen.c` directly (same convention `parena new`'s own scaffold uses for ordinary desktop
  targets — see `src/main.c`'s `cmd_new`), sets `DDRB` for PB5 (Arduino Uno's onboard LED /
  digital pin 13 — a real, well-known board fact), and loops calling `next_led_state` to decide
  the LED bit, toggling `PORTB` and delaying via `avr-libc`'s `_delay_ms`.

## The real, no-sudo AVR toolchain

This sandbox has no root. `apt-get download <pkg>` fetches real `.deb` files to the current
directory **without needing sudo and without installing system-wide** — confirmed working,
distinct from `apt-get install` (needs sudo). Recipe used:

```bash
mkdir -p /tmp/avr-deb && cd /tmp/avr-deb
apt-get download gcc-avr avr-libc avrdude binutils-avr libftdi1 libhidapi-libusb0 libusb-0.1-4
# NOTE: apt-get download aborts the ENTIRE batch if any one package name is invalid --
# confirmed live (an earlier attempt including the nonexistent "cpp-avr" fetched zero files).
# The three trailing libs above are avrdude's own real shared-library deps that are NOT
# preinstalled on a stock box -- checked directly via `dpkg -l`, not assumed.

mkdir -p ~/.local/opt/avr-toolchain
for f in *.deb; do dpkg -x "$f" ~/.local/opt/avr-toolchain; done
```

Live-verified against the real, extracted toolchain (not just "it exists on disk"):

```bash
export PATH=~/.local/opt/avr-toolchain/usr/bin:$PATH
export LD_LIBRARY_PATH=~/.local/opt/avr-toolchain/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH
avr-gcc --version      # avr-gcc (GCC) 7.3.0
avrdude -C ~/.local/opt/avr-toolchain/etc/avrdude.conf -v   # Version 7.1, config loads
```

A real `blink.c` targeting `atmega328p` compiled, linked, and produced a real, correctly-shaped
Intel-HEX file (`avr-size` reports 192 bytes of real program memory, 0.6% of the chip) — before
any of the PARENA-specific pieces above were even written.

## The real Makefile targets

```
make avr-blink-hex      # parena build -> avr-gcc -> avr-objcopy -> examples/avr/blink.hex
make avr-blink-upload   # avr-blink-hex, then avrdude -U flash:w:...:i
```

`AVR_TOOLCHAIN_ROOT`/`AVR_MCU`/`AVR_F_CPU`/`AVR_PORT`/`AVR_BAUD`/`AVR_PROGRAMMER` are real,
overridable `make` variables (defaults match this sandbox + a stock Arduino Uno on
`/dev/ttyACM0`) — a different box just overrides them, e.g.
`make avr-blink-upload AVR_PORT=/dev/ttyUSB0`.

## The real Upload button

`examples/editor_main.c`'s right sidebar already had a real "Compile" button (a momentary
action, not a `Toggle`-typed widget, in a reserved bottom strip — see that file's own
`compile_and_relaunch`). A new "Upload" button was added directly above it, same shape, own
color (blue vs. Compile's green) so the two are visually distinct. Clicking it calls
`compile_and_upload_avr()`, a real, blocking `system("make avr-blink-upload")` call (same
no-threading tradeoff `compile_and_relaunch` already accepts and documents).

**Real, honest, named v0 scope limitation**: Upload always targets `examples/avr/blink.prn`
regardless of the file currently open in the editor — unlike Compile, which rebuilds whatever
the editor has open. Making Upload respect the currently-open `.prn` file is real, separate
follow-up work, not done here.

## What's real vs. not yet proven

**Real and verified**: the full pipeline — `parena build` → `avr-gcc` → `avr-objcopy` → a
correctly-shaped `.hex` — compiles and links cleanly with zero warnings under
`-Wall -Wextra -pedantic -Werror`\* (\*that flag set is `editor-demo`'s own; the AVR target uses
plain `-Wall -Os` since `-pedantic`/`-Werror` were not checked against `avr-gcc`'s own header
set). `avrdude` genuinely runs against the real toolchain, loads its real config, and correctly
attempts to open the configured serial port. `editor-demo` itself still builds clean
(`-Wall -Wextra -pedantic -Werror`, zero warnings) with the new button wired in.

**Not yet proven — real, honest, named gap, not hidden**: no physical Arduino exists in this
sandbox (the exact same constraint `docs/UART_SERIAL_NORTHSTAR.md`'s own Phase 2 already names
and accepts for the serial stdlib). A real run here fails only at avrdude's own port-open step —
`/dev/ttyACM0: No such file or directory` — never at compile, link, or avrdude invocation. On a
real box with a real board attached, the same `make avr-blink-upload` / Upload-button click
flashes it for real; that final hardware round-trip is the one thing this write-up cannot claim.

## Real, not-yet-done follow-up work

- Upload button targeting the currently-open `.prn` file instead of always `blink.prn`.
- A general AVR GPIO stdlib module (`stdlib/hw/avr_gpio.prn`-shaped) with real `#target`
  register-level primitives, instead of every AVR program needing its own hand-written host —
  the same real jump `stdlib/hw/serial.prn` already made once for its own Linux-host primitives.
- Board/port auto-detection (no hardcoded `/dev/ttyACM0` default).
- An actual hardware round-trip proof once a physical Arduino is available.
