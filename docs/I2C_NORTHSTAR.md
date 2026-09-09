# NORTHSTAR — I2C stdlib for hardware platforms (third real bus, same day as SPI)

Real, direct continuation of the same hardware-interfaces thread as `docs/UART_SERIAL_NORTHSTAR.md`
and `docs/SPI_NORTHSTAR.md`. I2C is the third of the three buses any real Arduino-class or
Raspberry-Pi-class hobby sensor overwhelmingly uses — named here directly rather than left
implicit, since UART's own NORTHSTAR named SPI explicitly but never named I2C. Scoped and shipped
in the same pass, matching SPI's own precedent (not a plan-then-build split).

## Real, structural distinction from SPI — the reason this file's API looks like UART's, not SPI's

I2C is addressed and half-duplex-per-direction: a real caller writes a register address, then
separately reads back a value, over the same two wires (SDA/SCL) but never simultaneously in the
full-duplex sense SPI's MOSI/MISO pair is. Linux's real `i2c-dev(4)` userspace API reflects this
directly, and matches its own simplest, most common convention (the same one Python's
`smbus`/`smbus2` and most simple C I2C libraries use): `ioctl(fd, I2C_SLAVE, addr)` fixes ONE
target device address for the lifetime of the open fd, after which plain `read()`/`write()`
syscalls talk to that address — no per-call address parameter, no full-duplex transfer struct.
This is the real, structural reason `hw/i2c.prn`'s own API looks like `hw/serial.prn`'s
`-read`/`-write` pair, not `hw/spi.prn`'s single `-transfer` — a correct, non-arbitrary shape
match to the bus each file actually represents.

## Real, existing foundation checked directly

- Real i2c-dev headers exist in this sandbox (`/usr/include/linux/i2c-dev.h`, `linux/i2c.h`) —
  `I2C_SLAVE`, plain POSIX `read()`/`write()` afterward. No new ioctl-transfer struct needed the
  way SPI's `struct spi_ioc_transfer` was.
- `hw/spi.prn`'s own `-open`/`-close` shape (open, configure via ioctl, roll back the fd on
  configure failure) is the direct structural precedent for `i2c-open`/`-close`.
- `hw/serial.prn`'s own `-read`/`-write` shape is the direct structural precedent for
  `i2c-read`/`-write`, with one real, deliberate difference: `i2c-read` takes an explicit length
  (unlike `serial-read`'s "whatever's available, poll-gated" shape), because that's how a real
  I2C read actually works — a caller reads a specific, already-known number of bytes for the
  register/command it just addressed, not an open-ended stream from a continuously-transmitting
  device.

## A real, genuinely different testing situation from SPI, found directly, not assumed

Checked live: this actual box has a real, functioning I2C controller — `lsmod` shows `i2c_i801`
(an Intel SMBus controller) loaded, and `/dev/i2c-0` genuinely exists. Unlike SPI (where no real
controller of any kind exists in this sandbox), a real device technically exists here to test
against.

**Deliberately not used.** `/dev/i2c-0` is root-owned (`crw------- root root`) and this session
does not open or probe it anywhere, in either the implementation or its tests. An SMBus commonly
carries real system traffic unrelated to any hobby sensor — battery status, thermal management,
RAM SPD reads are all real, ordinary SMBus traffic on real hardware. Issuing an unreviewed real
transaction against it from an automated pass is a real, unnecessary risk to real system hardware
that this stdlib work does not need to take to be correct. This is a genuinely different kind of
limitation from SPI's "no device exists" — named explicitly as "chose not to," not "couldn't."

**What real testing WAS possible, and is done** (`tests/test_i2c.c`, `make test-i2c`, 14
assertions): i2c-dev's actual data-transfer mechanics (`read()`/`write()` once a device is open)
are plain POSIX file I/O — no ioctl involvement at all, distinct from `i2c-open`'s own
`I2C_SLAVE` ioctl. That means, unlike SPI's `SPI_IOC_MESSAGE`-gated transfer, `i2c_read_impl`/
`i2c_write_impl`'s real byte-shuffling logic is honestly testable against ANY real fd — a real
temp file stands in correctly, proving a genuine byte-perfect round trip, real short-read
handling (asking for more bytes than are actually available returns exactly what's there, no
hang), and a real zero-length-read edge case. Only `i2c-open`'s own ioctl SUCCESS path (actually
talking to a genuine I2C device) is untested — its honest FAILURE path (the kernel refusing
`I2C_SLAVE` against a real non-I2C file) is exercised for real.

## Real API surface, as shipped

```clojure
(defstruct I2cDevice (fd : I32))
(defenum I2cError (OpenFailed) (ReadFailed) (WriteFailed) (CloseFailed))

(defn i2c-open [(path : String) (addr : I32) (dest : Arena @ Region)]
  : (Result I2cDevice I2cError) @ Region)
(defn i2c-read [(!dev : &mut I2cDevice) (len : I32) (dest : Arena @ Region)]
  : (Result String I2cError) @ Region)
(defn i2c-write [(!dev : &mut I2cDevice) (data : String @ Region) (dest : Arena @ Region)]
  : (Result Unit I2cError) @ Region)
(defn i2c-close [(!dev : &mut I2cDevice) (dest : Arena @ Region)]
  : (Result Unit I2cError) @ Region)
```

`addr` is the real 7-bit I2C slave address (0x00-0x7F), fixed for the lifetime of the returned
`I2cDevice` — matches real caller usage (one open device per physical sensor/chip, not one open
per bus with an address re-passed on every call). `ReadFailed` is real and honestly named for
symmetry with the other three failure modes but genuinely unreachable through this path, same
class as `hw/serial.prn`'s own `ReadFailed` and `net/tcp.prn`'s own unused `NetError::Timeout`.

## Real, honest, not done

The same write-side embedded-NUL-byte limitation `hw/spi.prn`/`hw/serial.prn`/`net/tcp.prn`
already carry applies here too (see `SPI_NORTHSTAR.md` for the full reasoning) — `i2c-write`
cannot send a payload containing a literal `0x00` byte. `i2c-read`'s own explicit length avoids
the equivalent problem for the raw read itself (the returned buffer is correctly sized even if a
real device response contains a `0x00`), but any caller that then treats the result as an
ordinary NUL-terminated String downstream still only sees up to the first zero — the limitation
moves, it isn't closed. A real fix needs a length-explicit byte-buffer type at the PARENA
core-language level, real, separate, un-derisked, not attempted in this pass (same conclusion
`SPI_NORTHSTAR.md` already reached).

A real hardware round-trip against a genuine I2C sensor (a BME280/MPU6050-class device wired to a
real Raspberry Pi's I2C bus) remains genuinely blocked here for the reasons above — not
"impossible to test" the way SPI's was, but deliberately not attempted against this box's own
real system SMBus.
