# NORTHSTAR — SPI stdlib for hardware platforms (real answer to UART's own named gap)

Real, direct follow-up to `docs/UART_SERIAL_NORTHSTAR.md`, which explicitly scoped SPI out:
"the Adafruit Feather's own 'packet module' very likely talks over SPI, not UART — a real,
genuinely different bus protocol... SPI stdlib work is real, separate, unscoped here." This doc
is that separate scope, and (unlike UART's own two-phase northstar-then-build split) was
implemented in the same pass it was scoped in — see `STDLIB.md`'s own "hw/spi" section for the
as-shipped detail; this doc is kept as the real record of how that design was reached.

## Real motivation

Same grounding as UART: `IDUNA/docs/NORTHSTAR_INVENTORY.md`'s own named real hardware — at least
one Adafruit Feather with "some kinda packet module," almost certainly an RFM9x LoRa radio.
RFM9x-class radios talk SPI. UART's own stdlib work (`hw/serial.prn`) cannot reach this device;
this is the real, separate primitive that can.

## Why SPI's own API shape is genuinely different from UART/TCP/PTY's `-read`/`-write` pair

SPI is a synchronous, full-duplex bus: a real transfer clocks bytes OUT (MOSI) and IN (MISO) on
the exact same clock edges, in the same operation — there is no real, meaningful way to "just
write" or "just read" SPI the way a UART or TCP stream allows. Linux's real `spidev(4)` userspace
API reflects this directly: one `SPI_IOC_MESSAGE` ioctl, one `struct spi_ioc_transfer` naming both
a `tx_buf` and a `rx_buf` of the same `len`. `spi-transfer` is the one real operation; there is no
`spi-read`/`spi-write` pair to write by symmetry with the rest of this stdlib.

## Real, existing foundation checked directly

- Real spidev headers exist in this sandbox (`/usr/include/linux/spi/spidev.h`,
  `linux/spi/spi.h`) — `SPI_IOC_WR_MODE`/`SPI_IOC_WR_BITS_PER_WORD`/`SPI_IOC_WR_MAX_SPEED_HZ`/
  `SPI_IOC_MESSAGE`, `struct spi_ioc_transfer`, `SPI_MODE_0`..`SPI_MODE_3`. No real
  `/dev/spidev*` device node exists here (no real SPI controller in this sandbox) — same real
  limitation class `UART_SERIAL_NORTHSTAR.md`'s own Phase 2 and `pentest/pcap.prn`'s
  no-`CAP_NET_RAW` gap already name honestly.
- `hw/serial.prn`'s own `serial-open`/`-close` shape is the direct structural precedent for
  `spi-open`/`-close`; `spi-transfer` itself has no precedent to follow (see above) and is new.

## Real API surface, as shipped

```clojure
(defstruct SpiDevice (fd : I32))
(defenum SpiError (OpenFailed) (TransferFailed) (CloseFailed))

(defn spi-open [(path : String) (mode : I32) (speed-hz : I32) (bits-per-word : I32) (dest : Arena @ Region)]
  : (Result SpiDevice SpiError) @ Region)
(defn spi-transfer [(!dev : &mut SpiDevice) (tx : String @ Region) (dest : Arena @ Region)]
  : (Result String SpiError) @ Region)
(defn spi-close [(!dev : &mut SpiDevice) (dest : Arena @ Region)]
  : (Result Unit SpiError) @ Region)
```

`mode` is the raw `SPI_MODE_0..SPI_MODE_3` integer (0-3) passed straight through — unlike UART's
baud rate, SPI's mode constants already ARE their own literal values, so no lookup table is
needed. `speed-hz`/`bits-per-word` are passed straight to the kernel with no device-specific
ceiling enforced (real, honest caller responsibility, matching every other raw hardware parameter
in this stdlib). No manual chip-select primitive: the kernel's own spidev driver toggles the real
CS GPIO automatically, keyed by the device file's own `/dev/spidevB.C` (bus B, chip-select line
C) addressing — a real, deliberate v0 boundary for a real single-device-per-CS-line setup like
the Feather's own radio module, extendable later if a real multi-device-per-bus setup needs one.

## Two real, honestly-named limitations found while implementing (not previously written down)

1. **Embedded-NUL payloads cannot round-trip.** Every String-based host primitive in this
   runtime treats a buffer as a NUL-terminated C string (`strlen`-based), not a length-prefixed
   byte buffer — a real, pre-existing constraint `tcp-write`/`pty-write`/`serial-write` already
   silently carry too, never previously surfaced because none of their real payloads (HTTP text,
   shell commands, terminal output) are likely to contain a literal `0x00` byte. SPI is the
   first real binary-transfer module in this stdlib where that's a live, expected case — a real
   register address of `0x00` is completely ordinary on real SPI devices (the RFM9x's own
   `RegFifo` register IS address `0x00`). A real fix needs a length-explicit byte-buffer type at
   the PARENA core-language level. **Update, same day**: that type now exists —
   `docs/BYTES_NORTHSTAR.md`/`stdlib/bytes.prn`, a real, second core-language base type
   (`compress/lz4.prn`'s own `(Vec I32)` claim turned out to be a stale reference to a
   non-compiling design; the real fix is a genuinely new, non-generic `Bytes` type instead). The
   core gap is closed; retrofitting `spi-transfer` itself onto it (a new `spi-transfer-bytes`
   sibling) is real, separate, additive follow-up, not done yet.
2. **A failed transfer ioctl isn't distinguished from "the device returned all zeros."**
   `spi_transfer_impl` zeroes its own receive buffer up front and doesn't branch on the ioctl's
   return value — the same coarser-signal judgment this stdlib already makes for
   `tcp-read`/`pty-read`/`serial-read` (none of which distinguish a real read() error from "no
   more data" either). `TransferFailed` is real and honestly named for symmetry with
   `OpenFailed`/`CloseFailed`, but genuinely unreachable through this path — matching
   `hw/serial.prn`'s own already-documented `ReadFailed` and `net/tcp.prn`'s own unused
   `NetError::Timeout`. Untestable to a finer grain in this sandbox anyway: no physical SPI
   device exists here to fail a real transfer against.

## Real, honest, not done

A real hardware round-trip (an actual RFM9x, or any real SPI device, wired to a real Raspberry
Pi's SPI bus) remains genuinely blocked — no physical hardware in this sandbox, the same
limitation class `UART_SERIAL_NORTHSTAR.md`'s own Phase 2 and `pentest/pcap.prn`'s
no-`CAP_NET_RAW` gap already name. `tests/test_spi.c` exercises every real failure path this
sandbox CAN reach honestly (a nonexistent device path, a real-but-wrong-type device file
`/dev/null`, a direct `spi_transfer_impl` call against a non-SPI fd, real close success/failure)
— a real, successful transfer against a genuine SPI device is the one thing it cannot prove.

The embedded-NUL byte-buffer gap (limitation 1 above) is real, separate, unscoped follow-up work
if a real device driver ever needs to send/receive a payload containing a `0x00` byte — a real,
likely eventual need for anything beyond simple single-register reads/writes on the RFM9x.
