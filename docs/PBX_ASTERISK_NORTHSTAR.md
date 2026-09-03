# NORTHSTAR — filling the gap between PARENA's low-level PBX primitives and a real Asterisk

Real, direct answer to kanban priority-queue card `PBX-002`: *"full featured PBX add asterisk
bindings to parena whatever needs to fill in the gaps between our low level implementation and a
real deal."* Research-and-planning pass, building directly on this same session's own real work:
`stdlib/sip/message.prn` (SIP signaling), `stdlib/sip/rtp.prn` (RTP media headers, kanban
`PBX-001`), and `docs/SIP_TWILIO_GATEWAY_NORTHSTAR.md`'s own explicit Asterisk build-vs-adopt
addendum (kanban `SIPX-001`).

## The real gap this card names

`PBX-001`'s own `sip/rtp.prn` and the existing `sip/message.prn` are real, low-level, close-to-
the-metal PROTOCOL primitives (parsing/building the actual bytes SIP and RTP put on the wire).
They are NOT a PBX — there's no call-state machine, no dialplan, no registration database, no
media mixing. "Add Asterisk bindings" is the real, pragmatic path to a full-featured PBX this
card names directly: rather than building all of that from scratch in PARENA (a real, multi-year
undertaking for a mature project like Asterisk), **bind to a real, already-running Asterisk
instance's own control interfaces** and let Asterisk itself keep doing the heavy lifting — PARENA
supplies real, PARENA-native decision logic driving it.

## Real, checked-not-assumed research: Asterisk's own real control interfaces

Asterisk exposes three real, distinct control surfaces, each suited to a different real job:

1. **AMI (Asterisk Manager Interface, TCP port 5038)** — a real, ASCII, line-based protocol:
   "an AMI message – action or event – is composed of fields delineated by `\r\n`... each field
   is a key:value pair... terminated by an additional `\r\n`." A client sends **Actions**
   (e.g. `Action: Login\r\nUsername: admin\r\nSecret: mysecret\r\n\r\n`), Asterisk replies with
   **Responses**, and separately pushes **Events** (call state changes, hangups, registration
   status) asynchronously on the same connection. This is the real, standard way an external
   program monitors/controls a live Asterisk instance.
2. **ARI (Asterisk REST Interface)** — a modern HTTP + WebSocket API. Real, more capable for
   fine-grained call control, but needs a WebSocket client — PARENA has no WebSocket primitive
   today (a real, separate, larger gap than AMI's plain-TCP-text shape).
3. **AGI (Asterisk Gateway Interface)** — a real, simple stdin/stdout protocol for dialplan
   scripts to shell out to an external program per-call. Real, minimal, but only lets PARENA
   react WITHIN a single call already routed to it by Asterisk's own dialplan, not monitor/drive
   the system as a whole.

## Real, decisive finding: AMI's own wire format is a direct structural sibling of `sip/message.prn`

AMI's real message shape — `\r\n`-terminated key:value header lines, a blank line ending the
message — is nearly identical to a SIP message's own header block, the exact real shape
`sip/message.prn`'s own `parse-headers`/`SipHeader`/`(Vec SipHeader)` already parse and build.
**This is the real, high-value finding this research pass surfaces**: AMI binding is not a new
kind of parsing problem for this stdlib, it's the same real problem `sip/message.prn` already
solved, applied to a different (simpler — no start-line/body distinction) real protocol.

## Real, recommended path: AMI first, not ARI/AGI

Real, deliberate recommendation, matching this whole session's own "narrowest real, buildable
slice first" discipline: **AMI**, not ARI (needs WebSocket, a real, separate, larger gap) or AGI
(only reactive within a single already-routed call, not a real monitor/control surface). AMI is
real, plain TCP (`net/tcp.prn` already exists), real, plain ASCII text (`sip/message.prn`'s own
proven parsing shape applies directly), and gives real, immediate, useful capability: originate
calls, monitor channel/registration state, query queues — the real "control an Asterisk PBX from
PARENA" capability this card actually asks for.

## Real, phased plan (none started)

**Phase 1 — `stdlib/pbx/ami.prn`, the narrowest real slice.** `build-login-action(username,
secret, dest) -> String` (a real, concrete single use case, matching `sip/message.prn`'s own
`build-request` precedent of naming one real, working case rather than a generic, unproven
key-value-builder). `parse-ami-message(raw, dest) -> Result AmiMessage AmiError` — a real, direct
structural port of `sip/message.prn`'s own `parse-headers`, producing a `(Vec AmiField)` +
`header-value`-style lookup, the same real API shape. Real, honest note carried over from this
same session's own `sip/rtp.prn` fix: any of these functions handling raw bytes must take an
explicit real length parameter, never trust `string/length` (`strlen`) on data that might
legitimately contain embedded NUL bytes — checked directly, AMI's own real fields are ASCII text
(usernames, channel names, response codes), so this is a real, lower-risk case than RTP's binary
header was, but the same discipline applies if a future field is ever binary.

**Phase 2 — real login + one real action/response round trip.** `net/tcp.prn`'s own real
`tcp-connect`/`tcp-write`/`tcp-read` sends a real `Login` action to a real, live Asterisk
instance and parses the real `Response: Success`/`Response: Error` reply — the real, first proof
this binding actually works end to end, mirroring `net/http.prn`'s own real request/response
round-trip shape.

**Phase 3 — real event parsing + a small, useful action set.** Parse real, asynchronously-pushed
Events (not just Action responses) using the same `parse-ami-message` from Phase 1 (AMI doesn't
distinguish Events from Responses at the wire-format level — real, useful reuse). Real, useful
first actions beyond Login: `Originate` (place a call), `Hangup`, `QueueStatus`.

**Phase 4 (deferred, real, separate, larger work)** — ARI/WebSocket binding, if AMI's own real
limitations (polling-shaped, no fine-grained per-call media control) ever become a real,
concrete blocker. Not scoped further here.

## Real, honest, not-yet-resolved question

No real, live Asterisk instance exists in this sandbox to test against (same real limitation
class `pentest/pcap.prn`'s own no-`CAP_NET_RAW` gap and `UART_SERIAL_NORTHSTAR.md`'s own
no-physical-serial-device gap already named honestly) — Phase 2's own real end-to-end proof needs
a real, running Asterisk somewhere reachable, not attempted or faked here.

## Related

- `PARENA/docs/SIP_TWILIO_GATEWAY_NORTHSTAR.md` — the real, sibling B2BUA/relay plan this doc's
  own AMI binding complements (a relay moves audio/signaling; AMI binding CONTROLS a real,
  separate Asterisk instance doing the same real job more completely).
- `PARENA/stdlib/sip/message.prn` — the real, direct structural precedent `pbx/ami.prn`'s own
  Phase 1 parsing/construction follows almost line-for-line.
- `PARENA/stdlib/sip/rtp.prn` — the real, sibling low-level primitive (kanban `PBX-001`) this
  card's own "narrow scope primitives... do that first" framing already produced.
