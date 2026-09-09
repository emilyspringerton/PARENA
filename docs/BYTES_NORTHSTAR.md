# NORTHSTAR — a real Bytes core-language type, closing the embedded-NUL gap 3 stdlib files named

Real, direct follow-up to the same-day discovery in `docs/SPI_NORTHSTAR.md`/`docs/I2C_NORTHSTAR.md`
(and silently already present, unnamed, in `hw/serial.prn`/`net/tcp.prn`/`pty.prn`): every raw
host primitive in this runtime that speaks `String` treats it as a NUL-terminated C string
(`strlen`-based), so a binary payload with a real, ordinary embedded `0x00` byte — an SPI/I2C
register address of `0x00` is completely normal, the RFM9x's own `RegFifo` register IS address
`0x00` — silently truncates. Unlike UART/SPI/I2C, this isn't a new hardware bus; it's a real
core-language gap those three buses all independently surfaced the same day.

**Unlike UART's own northstar-then-build split, this doc was written AFTER the real investigation
that proved it tractable, not before it** — the investigation itself is the real content worth
recording, since it overturned the initial assumption (made explicitly, in writing, in
`SPI_NORTHSTAR.md`) that this would need a large, multi-subsystem compiler change.

## Real investigation, checked directly, not assumed

**First wrong assumption, corrected by reading the actual source**: `compress/lz4.prn`'s own
header comment claims a "real, pure-PARENA `(Vec I32)` byte buffer" is the existing precedent for
a byte-buffer shape. Checked directly: `stdlib/vec.prn` (the file describing a generic, `(Raw T)`-
backed Vec design) genuinely does **not compile** against the real compiler today —
`./parena build stdlib/vec.prn` fails with `"unsupported return type form"`, since `(Raw T)` was
never implemented in `src/emit.c`'s type resolver. `Vec` as it actually, really works
(`compress/lz4.prn` and others genuinely use it) is a *different*, hand-written, fully built-in
runtime type (`runtime/parena_runtime.h`'s own `Vec` struct: `Arena *`/`void **items`/`count`/
`capacity`), recognized natively by `src/emit.c` (`"Vec"` appears at real dispatch points:
return-type resolution, `get-field`, parameter handling — roughly 58 real call sites across the
emitter touch the `vec_*` runtime function family). `stdlib/vec.prn` itself is stale, aspirational
documentation-as-code, superseded by that real built-in, and should not be trusted as a working
example — a real, useful correction to leave written down, since `lz4.prn`'s own comment currently
points the wrong way.

**Why `Vec`'s own real complexity (58 call sites, generic-element boxing, hint tracking) does NOT
mean a new byte-buffer type needs the same scope**: `Vec` is generic over element type `T`, which
is the actual source of most of that complexity (per-instantiation scalar boxing, element-type hint
tracking for `vec-get`'s own cast, etc.). A byte buffer has exactly ONE possible element type
(a byte) — structurally much closer to how `String`/`Arena` are already simple, non-generic,
built-in base types than to `Vec`'s own generic machinery.

**The real, single dispatch point checked and confirmed sufficient**: `src/emit.c`'s
`resolve_base_type_name()` is the one function every bare-symbol type (`Unit`/`I32`/`Bool`/`F64`/
`String`/`Arena`/a registered enum or struct name) resolves through, for both return types AND
struct fields (`resolve_declared_type` calls it for both). `resolve_result_option_payload_type`/
`resolve_result_error_type` (the functions that resolve `X`/`E` inside `(Result X E)`) both just
recurse into `resolve_declared_type` generically — no per-type-name special-casing there either.
**`ensure_box_helper`** (the function that generates a `TypeName_box(Arena*, TypeName)` helper for
any `(Result X E)`/`(Option X)` payload) was read in full before writing any code: it works
**purely off the C type string**, generic over any type that doesn't end in `*`
(`is_pointer_c_type`) — the exact same generic path real `defstruct` types (`SpiDevice`,
`SerialPort`, `I2cDevice` — all shipped earlier the same day) already go through successfully, with
zero type-name awareness. This was the key finding that made the whole thing tractable: a new base
type needs no box-helper-generation work at all, only registration in the bare-symbol table.

**One real, additional dispatch point found only by actually testing** (not by reading — this is
exactly why this repo's own discipline insists on compiling, not just parsing): parameter-type
resolution in `emit_defn`'s own parameter loop is a **separate, hand-maintained** `I32`/`Bool`/
`F64`/`String` allowlist, distinct from `resolve_base_type_name`'s own return-type table — two real
places needed the same one-line addition, not one. A third, `resolve_param_prototype_type` (used
for forward-declaration prototypes), carries an identical allowlist and needed the identical fix.
Found by literally attempting to compile a `Bytes`-typed parameter and reading the exact error VS0
gave back, the same "confirmed live, not assumed" method this whole session already uses.

## Real, live bug found and fixed while implementing (not just designing)

A first draft named the runtime's own `Bytes` helper functions without an `_impl` suffix
(`bytes_alloc`, `bytes_get`, etc.), reasoning by analogy to `arena_alloc`/`vec_new` (which never
carry one). This is wrong: `arena_alloc`/`vec_new` are never ALSO the literal name of a
PARENA-level wrapper function, but `stdlib/bytes.prn`'s own `bytes-alloc` mangles to the bare C
identifier `bytes_alloc` (VS0 emits top-level `defn` names unprefixed) — the exact real
symbol-collision class `net/tcp.prn`'s and `pty.prn`'s own header comments already document and
fixed via a `tcp-`/`pty-` naming convention, and the exact real reason every runtime primitive
those files call carries an `_impl` suffix. `parena build` did not catch this (by design — it never
validates `inline-c` content); only an actual `gcc` compile did, producing a real
`"expected ';' before '}' token"`-adjacent redeclaration failure once the two conflicting
`bytes_alloc` signatures collided. Fixed by renaming every runtime `Bytes` function with the same
`_impl` suffix convention. Verified again afterward: `gcc -Wall -Wextra -pedantic`, zero warnings.

## Real API surface, as shipped

```c
/* runtime/parena_runtime.h */
typedef struct { unsigned char *data; int len; } Bytes;
```

```clojure
;; stdlib/bytes.prn
(defn bytes-alloc [(len : I32) (dest : Arena @ Region)] : Bytes @ Region)
(defn bytes-len [(b : Bytes)] : I32)
(defn bytes-get [(b : Bytes) (idx : I32)] : I32)             ;; -1 sentinel, out-of-bounds
(defn bytes-set! [(b : Bytes) (idx : I32) (val : I32)] : Unit)  ;; honest no-op, out-of-bounds
(defn bytes-from-string [(s : String @ Region) (dest : Arena @ Region)] : Bytes @ Region)
(defn bytes-to-string-lossy [(b : Bytes) (dest : Arena @ Region)] : String @ Region)
```

`bytes-set!` takes `Bytes` by value, not `&mut Bytes` — correct, not an oversight: only the
struct's own `data` pointer is copied, and a mutation through that copy is real and visible to
every other holder of the same value, since it still addresses the identical backing bytes. No
reallocation ever happens (unlike `Vec`'s own `push!`), so no `&mut` is needed.

## Real, honest, named limitations — the type closes the CORE gap, not every consequence of it

- `bytes-from-string`/`bytes-to-string-lossy` are real, explicitly-named LOSSY boundaries at the
  `String` interop edge: a `String` that's already been truncated upstream (by any of today's
  existing `String`-only primitives — `tcp-read`, `serial-read`, `spi-transfer`) cannot be
  un-truncated by converting it to `Bytes` after the fact; and converting `Bytes` back to `String`
  still truncates at the first embedded `0x00`. `Bytes` helps once data enters the system as real
  Bytes FROM THE START — it does not retroactively fix anything already lost.
- **No retrofit of `hw/spi.prn`'s `spi-transfer` / `hw/i2c.prn`'s `i2c-read`/`i2c-write` /
  `hw/serial.prn`'s `serial-read`/`serial-write` to a `Bytes`-based sibling was done in this pass.**
  This is real, separate, additive follow-up (e.g. a new `spi-transfer-bytes` function, not a
  breaking change to the existing `String`-based ones) — named explicitly here so it isn't
  silently assumed solved just because the core type now exists.
- v0 scope is deliberately narrow: allocate/length/indexed get/set, String interop. No slicing,
  concatenation, comparison, or hex encoding yet — real, separate, additive work if a real caller
  needs it.

## Real, phased plan

**Phase 1 — shipped, this pass.** The core `Bytes` type (compiler + runtime + `stdlib/bytes.prn`),
verified via `make test-bytes` (18 real assertions, including a genuine embedded-`0x00`
survive-with-length-intact proof) plus the full existing `make test` (347/347, zero regressions —
run repeatedly through the compiler-change process, not just once at the end).

**Phase 2 — shipped, same day.** Retrofit `hw/spi.prn`/`hw/i2c.prn`/`hw/serial.prn` with real
`Bytes`-based sibling functions (`spi-transfer-bytes`, `i2c-read-bytes`/`i2c-write-bytes`,
`serial-read-bytes`/`serial-write-bytes`) — every existing `String`-based function left
unchanged, exactly as planned. See `STDLIB.md`'s own "Phase 2" section for the full detail,
including a real, byte-perfect embedded-`0x00` round-trip proven end-to-end against a real open
pty device (`hw/serial.prn`) and a real temp-file fd (`hw/i2c.prn`) — not just proven to compile.
