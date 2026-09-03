# NORTHSTAR — a real, native PARENA packet-capture implementation

Real, phased plan for the founder's own explicit real-time correction/expansion of kanban card
435423 ("parena PCAP primatives"): *"ok but we want to build pcap into parena on a deep low
level we want a parena pcap implementation and build the std lib deps it needs into the std lib
plan that work."* Same "size it honestly before committing to a pass" discipline this session's
own `V16_NORTHSTAR.md` already applied to an equally structural ask. **Planning only — no code
written for this document.** The already-shipped `stdlib/pentest/pcap.prn` +
`tools/pentest_pcap_host.c` (FFI-bound to libpcap, real and working, kanban card 435423's own
literal first pass) stays exactly as-is; this document plans a real, separate, NATIVE
implementation alongside it, not a replacement — see "Relationship to the existing FFI-bound
`pentest/pcap.prn`" below for why keeping both is the real, honest recommendation.

## What "deep low level" and "native" actually mean here, made concrete

libpcap itself is not one thing — it's two real, distinct jobs bundled into one library:

1. **Capture**: getting raw packet bytes off the wire. On Linux, libpcap's own real
   implementation does this via an `AF_PACKET`/`SOCK_RAW` socket — the exact same real Linux
   kernel facility a native PARENA implementation would use directly. This is the one piece
   "native" genuinely means "skip libpcap, talk to the kernel ourselves."
2. **Decoding**: turning those raw bytes into "this is a TCP packet from X to Y, port Z" —
   libpcap itself does **not** do this at all (that's Wireshark/tshark's own job, a separate,
   much larger library). A native PARENA implementation doing REAL protocol decoding natively
   would, honestly, exceed libpcap's own real scope and start to resemble a (very) minimal
   Wireshark dissector core, not just a libpcap reimplementation.

## Real, checked-not-assumed current foundation

Directly checked against the real `parena`/VS0 source before writing a single phase below —
significantly more of the real "stdlib deps" this ask names already exist than expected:

- **Bitwise operators already exist**: `bit-and`/`bit-or`/`bit-xor`/`shl`/`shr`/`mod` are real,
  working VS0 binops (`src/emit.c`), added 2026-08-20 for `stdlib/compress/lz4.prn`'s own
  byte-level codec work — the exact real primitive class a wire-format parser needs for extracting
  packed bitfields (e.g. an IP header's own 4-bit version + 4-bit IHL nibble).
- **Byte-level string access already exists**: `char-at` (real, already documented as "returns a
  character as its raw byte value (I32)... real callers compare it against ASCII literals
  directly") is already exactly the right primitive to read a raw byte from a captured packet
  buffer — this stdlib already treats `String` as a byte buffer wherever it needs to
  (`bstree.prn`'s own byte-wise compare, `v16/lexer.prn`'s own real tokenizer).
- **Multi-byte field extraction is buildable TODAY from what exists, no new primitive needed**:
  a real big-endian 16-bit read is just `(+ (shl (char-at buf i) 8) (char-at buf (+ i 1)))` —
  network byte order is always big-endian, and `shl`+`char-at` already express this exactly.
  This means the "std lib deps it needs" the founder names are, honestly, MOSTLY already real —
  what's genuinely missing is a real, NAMED module providing these as reusable, well-tested
  functions (`read-u16-be`/`read-u32-be`/`mac-to-string`/`ipv4-to-string`), not new compiler
  primitives.
- **What's genuinely, completely missing**: any raw-socket primitive at all. `net/tcp.prn`/
  `net/udp.prn` (`runtime/parena_runtime.h`'s own `tcp_*_impl`/`udp_*_impl` functions) only ever
  open `AF_INET`+`SOCK_STREAM`/`SOCK_DGRAM` sockets — ordinary connected/datagram sockets, not a
  raw, all-traffic-visible capture socket. This is the one real, load-bearing new capability.

## Real, honest platform trade-off, named explicitly up front

Going native means committing to `AF_PACKET` — a real, **Linux-only** kernel facility. libpcap's
own real value (the reason it exists as a library at all, not just "open a raw socket") is
abstracting THIS exact platform difference away: BSD/macOS use BPF device nodes
(`/dev/bpf*`), Windows uses Npcap/WinPcap, Linux uses `AF_PACKET`. A native PARENA
implementation, scoped honestly, is a **Linux-only capability** unless/until a second,
platform-specific backend is built later — matching this monorepo's own real, already-accepted
"pick the real target platform, name the real gap, don't build false cross-platform generality
up front" discipline (e.g. `FLASH`'s own real, named "no Windows support yet" gap). Given every
real host this monorepo actually deploys to is Linux (this box included), this is a real,
acceptable v0 trade-off, not a silently-hidden one.

## Real, phased plan (none started)

### Phase 1 — the one genuinely new runtime primitive: a raw capture socket

New `runtime/parena_runtime.h` functions, the exact same real "thin FFI over a raw syscall, real
PARENA code builds everything else" shape `tcp_connect_impl`/`udp_bind_impl` already establish
(not a new pattern, just a new instance of an already-proven one):

- `pcap_raw_open_impl(const char *iface)` — `socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))`,
  then `bind()` to the named interface's own real `ifindex` (via `SIOCGIFINDEX`, a real, standard
  ioctl every real Linux raw-capture tool uses this exact way — `tcpdump`/libpcap included).
  Returns a real fd, or -1 on failure (permission denied without `CAP_NET_RAW`/root — the same
  real privilege requirement libpcap's own `pcap_open_live` already has, this doesn't go away by
  going native).
- `pcap_raw_read_impl(int fd, Arena *dest)` — a real `recvfrom()` into an arena-allocated buffer,
  returning the real byte count (or -1). Real, honest v0 scope: blocking, one frame per call,
  matching `pentest_pcap_read_packet`'s own already-shipped "one Option Packet at a time" shape,
  not a new design.
- `pcap_raw_close_impl(int fd)` — a real `close()`.

New `stdlib/net/raw.prn` (or `pcap/raw.prn` — real naming decision, not made here) wraps these
three exactly the way `net/tcp.prn`'s own `tcp-connect`/`tcp-read`/`tcp-close` wrap their own
`tcp_*_impl` primitives: a real `RawSocket` struct (an fd, same opaque-handle discipline
`pentest/pcap.prn`'s own `Capture` already uses), `Result`/`Option`-returning PARENA functions,
zero raw syscalls visible above this one file.

### Phase 2 — the real "stdlib deps" ask: a byte/wire-format primitives module

New `stdlib/net/wire.prn` (naming TBD), built ENTIRELY from primitives that already exist today
(no new compiler/runtime work needed for this phase at all — confirmed above): `read-u16-be`/
`read-u32-be` (big-endian multi-byte field extraction via `char-at`+`shl`+`bit-or`), `mac-to-
string` (6 raw bytes → `"aa:bb:cc:dd:ee:ff"`, needs a real hex-digit formatter — `i32-to-string`
only does decimal today, a real, small, separate gap to close), `ipv4-to-string` (4 raw bytes →
dotted-decimal, buildable from `i32-to-string`+`concat` already). This is the module a real
Ethernet/IP/TCP header parser (Phase 3) is built on top of, and it's real, useful, and
independently testable on its own (a real unit test can byte-construct a fake buffer and check
the field-extraction functions directly, no real socket or root privilege needed for THIS phase's
own tests).

### Phase 3 — real, native Ethernet/IPv4/TCP/UDP header parsing

Once Phase 2 exists, a real `EthernetFrame`/`Ipv4Header`/`TcpHeader`/`UdpHeader` struct set and
real `parse-ethernet`/`parse-ipv4`/`parse-tcp`/`parse-udp` functions, pure PARENA, no FFI — the
direct structural sibling of `stdlib/sip/message.prn`'s own real text-protocol parser shipped
this same session, just binary instead of text. Real, honest v0 scope: the "table stakes" header
fields only (Ethernet: src/dst MAC + ethertype; IPv4: src/dst IP + protocol + total length; TCP/
UDP: src/dst port) — no IPv6, no IP option parsing, no TCP option parsing, no checksum
verification. Each of those is real, named, separate, later work, not silently promised here.

### Phase 4 — a real filter, PARENA-native (not full BPF compatibility)

Real, honest, deliberate scope-narrowing named up front: reimplementing BPF bytecode
compilation+execution (what `pcap_compile`/`pcap_setfilter` do, letting a caller type a real
`tcpdump`-style filter STRING like `"tcp port 80"`) is a real, separate, much larger undertaking
— a small bytecode VM plus a real filter-expression parser, comparable in size to this session's
own `stdlib/v16/lexer.prn` work, not a quick add-on. The real, honestly-scoped v0 instead: a
filter is just a real PARENA predicate function taking a parsed header struct (Phase 3's own
output) and returning `Bool` — `(fn [(pkt : Ipv4Header)] : Bool (= (get-field pkt :protocol) 6))`
for "TCP only," written directly in PARENA, no separate filter language at all. Real BPF-string
compatibility is named as explicit, separate, later work if it's ever actually needed.

## Relationship to the existing FFI-bound `pentest/pcap.prn`

Real, deliberate recommendation: **keep both, don't replace.** `pentest/pcap.prn` (shipped this
same session, kanban card 435423's own first pass) is real, working, and gives real, immediate
value: libpcap's own actual cross-platform portability (a real path to non-Linux capture later)
and real BPF-string filter compatibility (`pcap_compile` already accepts any real `tcpdump`
filter string, for free, today). The native implementation this document plans is a real,
separate, additional capability — "PARENA can do this without leaning on a third-party C
library at all," the same real dogfooding motivation `compress/lz4.prn` (a pure-PARENA LZ4
implementation, not FFI-bound to the reference library) already established as a real, standing,
monorepo-wide default. Real, open, undecided question, not resolved here: should the two share a
common `Packet`-shaped result type so downstream code can use either backend interchangeably? A
real, small, later design question once Phase 3 actually exists to compare against.

## Related

- `PARENA/STDLIB.md`'s own "pentest/pcap" section — the real, shipped FFI-bound implementation
  this document's own native plan sits alongside, not replaces.
- `PARENA/STDLIB.md`'s own "compress/lz4" section — the real, direct precedent for "pure-PARENA
  reimplementation over FFI-binding a reference library" as this monorepo's own standing default.
- `PARENA/stdlib/sip/message.prn` — the real, direct structural precedent for a native, from-
  scratch protocol parser (text, not binary, but the same real "parse a real wire format in pure
  PARENA" shape Phase 3 above follows).
- `PARENA/docs/V16_NORTHSTAR.md` — the same "size a large ask honestly, phase it, name the real
  platform/scope trade-offs up front" discipline applied to a different large ask.
