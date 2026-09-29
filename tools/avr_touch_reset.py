#!/usr/bin/env python3
"""Real 1200-baud "touch" reset for Caterina-bootloader AVR boards (e.g. the Adafruit
Feather 32u4). Opening the port at 1200 baud and then immediately closing it again is the
actual mechanism the Arduino IDE uses to drop a 32u4-class board into its bootloader --
there's no avrdude flag for this, it has to happen as its own step before the real flash,
then the caller needs to wait out the bootloader's short (~8s) window before avrdude connects.

Plain termios, no pyserial dependency -- matches parena_runtime.h's own serial primitives,
which use the identical open/tcsetattr/close sequence, just from C (stdlib/hw/serial.prn).

Real, named limitation, same as every other AVR target in this repo (see
docs/AVR_ARDUINO_NORTHSTAR.md): no physical board is attached to this sandbox, so this is
verified to correctly open/configure/close a real termios-backed device (a BSD pty, the same
stand-in test_serial.c already uses), not against a real 32u4's own USB-CDC enumeration.

Usage: avr_touch_reset.py <port> [baud]
"""
import os
import sys
import termios


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: avr_touch_reset.py <port> [baud]", file=sys.stderr)
        return 1
    port = sys.argv[1]
    baud_name = "B" + (sys.argv[2] if len(sys.argv) > 2 else "1200")
    baud_value = getattr(termios, baud_name, None)
    if baud_value is None:
        print(f"avr_touch_reset: unsupported baud rate '{sys.argv[2]}'", file=sys.stderr)
        return 1

    try:
        fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
    except OSError as e:
        print(f"avr_touch_reset: could not open {port}: {e}", file=sys.stderr)
        return 1
    try:
        attrs = termios.tcgetattr(fd)
        attrs[4] = baud_value  # ispeed
        attrs[5] = baud_value  # ospeed
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
    finally:
        os.close(fd)
    print(f"avr_touch_reset: touched {port} at {sys.argv[2] if len(sys.argv) > 2 else '1200'} baud")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
