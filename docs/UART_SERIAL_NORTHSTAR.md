# NORTHSTAR — UART/Serial stdlib for hardware platforms (Arduino-equivalent)

Real, direct answer to two related kanban priority-queue cards, treated as one unified ask:
`HW-001` ("UART STDLIBS and DEPS STDLIBS PLANNING FOR HARDWARE PLATFORMS ARDUINO EQUIVALENT
etc") and `HW-003` ("SERIAL STDLIBS"). UART is one specific kind of serial communication — the
two cards are the same real ask at two levels of specificity, not two separate features.
Research-and-planning only, no code written for this pass.

## Real motivation, grounded in this monorepo's own actual hardware

Almost every real Arduino-class microcontroller talks to a host over USB-serial (a virtual UART
exposed as `/dev/ttyUSB0`/`/dev/ttyACM0` on Linux), and many real sensor/GPS/radio modules use
genuine UART directly. Real, direct connection to this same session's own
`IDUNA/docs/NORTHSTAR_INVENTORY.md`: the founder's own named real hardware (2× Raspberry Pi,
2× Pi Zero, an Adafruit Feather with a radio module) is exactly the class of device this stdlib
work would let PARENA talk to directly.

## Real, existing PARENA foundation — checked directly, not assumed

- **`stdlib/io.prn`**: real, generic file I/O (`file-open`/`read-string`/`write-string`/
  `file-close`). On Linux, a serial port is just a special device file — this is the real,
  existing read/write path once the port is correctly configured (see the one real gap below).
- **`stdlib/pty.prn`**: the real, direct structural precedent for "a raw syscall FFI wrapper
  around a POSIX terminal-adjacent primitive, with a `#target {:c (inline-c ...)}` body" — the
  exact shape a new serial-port primitive should follow. Checked directly: `pty.prn`'s own
  `pty-raw-open` uses `forkpty`, which already hands back a usable pty with no separate raw-mode
  configuration step — so it's the right STRUCTURAL model, not a directly reusable function (a
  real serial device file needs its own, different open/configure sequence, below).

## The one real, genuinely new gap

Opening `/dev/ttyUSB0` with plain `open()` gets you a file descriptor in the **wrong default
mode** for a real microcontroller connection: canonical (line-buffered, echoing) mode at
whatever baud rate the device happened to be left at, not the specific baud rate (commonly 9600
or 115200 for Arduino-class boards) and raw (unbuffered, 8N1) framing a real serial link needs.
This needs a real `termios` configuration step — `tcgetattr`/`cfsetispeed`/`cfsetospeed`/
`cfmakeraw`/`tcsetattr` — applied to the fd right after opening it. **This is the one genuinely
new runtime primitive this plan needs**; everything else (the actual read/write once configured)
already exists via `io.prn`.

## Real, honest, deliberately-out-of-scope distinction

The Adafruit Feather's own "packet module" (`IDUNA_PRO`'s own inventory doc names this as
"uncertain, likely an RFM9x LoRa radio") very likely talks over **SPI, not UART** — a real,
genuinely different bus protocol (synchronous, separate clock/MOSI/MISO/CS lines, no baud-rate
concept). SPI stdlib work is real, separate, unscoped here — this doc answers the literal
UART/Serial ask the cards name, not every possible hardware bus.

## Real API surface, direct structural sibling of `net/tcp.prn`

```clojure
(defstruct SerialPort (fd : I32))
(defenum SerialError (OpenFailed) (ConfigureFailed) (ReadFailed) (WriteFailed))

(defn serial-open [(path : String @ :region/scratch) (baud : I32) (dest : Arena @ Region)]
  : (Result SerialPort SerialError) @ Region)
(defn serial-read [(!port : &mut SerialPort) (dest : Arena @ Region)] : (Result String SerialError) @ Region)
(defn serial-write [(!port : &mut SerialPort) (data : String @ Region)] : (Result Unit SerialError))
(defn serial-close [(!port : &mut SerialPort)])
```

Real, standard baud rates to support in v0: 9600 and 115200 — covers the large majority of real
Arduino-class boards; a real, small, named lookup table (`baud-to-speed-constant`) maps the real
I32 baud rate a caller passes to the real POSIX `speed_t` constant (`B9600`/`B115200`) the
`cfsetispeed`/`cfsetospeed` calls actually need — not every real POSIX baud rate, a deliberate v0
boundary, extended later only if a real device needs one not yet listed.

## Real, phased plan (none started)

**Phase 1 — the termios primitive + basic stdlib wrapper.** New `runtime/parena_runtime.h`
function `serial_configure_impl(fd, baud)` (the one genuinely new primitive), plus
`stdlib/hw/serial.prn` wrapping it with `serial-open`/`-read`/`-write`/`-close` per the API above,
reusing `io.prn`'s own already-real file-open/read/write underneath once the fd is configured.

**Phase 2 — real hardware round-trip proof.** Real, honest, named limitation up front, matching
this same session's own `pentest/pcap.prn` precedent (no `CAP_NET_RAW` in this sandbox): this
dev environment very likely has no physical USB-serial device attached, so a genuine live proof
needs a real box with actual hardware plugged in — not attempted or faked here. Real, concrete
test plan once hardware exists: an Arduino running a trivial "echo what you receive" sketch,
verified end to end through `serial-write`/`serial-read`.

## Related

- `IDUNA/docs/NORTHSTAR_INVENTORY.md` — the real, named hardware (Raspberry Pi, Adafruit Feather)
  this stdlib work would let PARENA talk to directly.
- `PARENA/stdlib/pty.prn` — the real, direct structural precedent this plan's own new primitive
  follows (a raw, FFI-bound POSIX terminal syscall wrapper).
- `PARENA/stdlib/net/tcp.prn` — the real, direct structural precedent this plan's own
  `serial-open`/`-read`/`-write`/`-close` API shape follows.
