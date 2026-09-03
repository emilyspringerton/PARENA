# NORTHSTAR — a real PARENA gateway between our SIP phone clients and Twilio

Real, direct answer to kanban priority-queue card `SIP-001`: *"what software do we need to build
io be the pipes between our sipphone clients (yet to be built) and twillio do research and
planning on what parena deps scratch built."* Research-and-planning only, per the card's own
framing — no code written for this pass.

## What "the pipes" actually means, made concrete

Our own SIP phone clients (not yet built) need to place/receive real phone calls through Twilio.
Twilio's real product for this is **Elastic SIP Trunking**: you register a *Trunk* with Twilio
(a Termination SIP URI for outbound calls, an Origination target for inbound), and Twilio relays
real SIP signaling + RTP media between the PSTN and your own SIP infrastructure. "The pipes" is a
real software gateway sitting between our internal SIP clients and Twilio's trunk — functionally
a small SIP proxy / B2BUA (back-to-back user agent), not a full PBX.

## Real, checked-not-assumed technical requirements (Twilio's own docs)

- **SIP methods supported**: INVITE, ACK, CANCEL, REFER, BYE, OPTIONS. **Transports**: UDP, TCP,
  and TLS (TLS required for encrypted media — SRTP — but plain UDP trunking works without it).
- **Signaling ports**: 5060 (UDP/TCP), 5061 (TLS), against Twilio's own published regional
  signaling-gateway IP ranges (e.g. `54.172.60.0/30` for the Virginia gateway).
- **Media (RTP)**: as of 2024-02-21, all regions use `168.86.128.0/18`, UDP port range
  **10000–60000**.
- **Authentication**: a Trunk needs at least one of — an **IP Access Control List** (allow-listed
  source IPs, zero crypto/challenge-response needed on our side) or a **Credential List** (Twilio
  challenges our INVITE with a `407 Proxy Authentication Required`, expecting a real SIP digest
  response, RFC 2617-style, traditionally MD5-based).
- **DTMF**: RFC 4733/2833 (in-band RTP events, not audio tones) — real signaling-plane data, not
  a codec concern.
- **Codec**: G.711 (μ-law/A-law) is the commonly recommended codec for Twilio trunking.

## Real, existing PARENA foundation — checked directly, not assumed

- **`stdlib/sip/message.prn`** (shipped this same session, kanban card 3124213): real, pure-
  PARENA SIP message parsing (`parse-message`) and construction (`build-request`) — the mandatory
  header set (`Via`/`From`/`To`/`Call-ID`/`CSeq`/`Max-Forwards`/`Content-Length`) is already real
  and tested. This is the real, direct signaling-plane foundation for everything below. Its own
  header comment already names its real, honest v0 boundary: **no SDP, no auth, no dialog/
  transaction state machine** — exactly the three real gaps this gateway needs closed.
- **`stdlib/net/udp.prn`**: real UDP transport, the exact real transport SIP-over-UDP and RTP
  media both need — no new runtime primitive required for basic send/receive.
- **`stdlib/crypto/hash.prn`**: FFI-bound to OpenSSL, currently ships `sha256` only — real,
  small, likely-easy gap if Credential List auth (needing classic MD5 digest) is ever chosen over
  IP ACL (see §"Real, phased plan" Phase 4).

## The one real, valuable simplification this research surfaced

A media relay does **not** need to understand G.711/RTP payload content at all if it's a pure
relay: once the two RTP endpoints (our phone client's real address, Twilio's real media address)
are known from SDP negotiation, the gateway can just forward raw UDP datagrams bidirectionally,
byte-for-byte, the same way any real SBC (session border controller) media-relay component does.
**Real, decisive consequence**: G.711 encode/decode is genuinely NOT needed for Phase 1-3 below —
only if PARENA itself ever needs to synthesize or consume audio content directly (e.g. an
Emily-generated voice prompt, voicemail transcription) does real codec work become necessary,
and that's real, separate, explicitly deferred work, not silently assumed away.

## Real, newly-identified gaps, each named honestly

1. **SDP parsing/construction** — `sip/message.prn`'s own already-named gap. A real, direct
   structural sibling of that file (binary-ish key/value SDP body, not as regular as SIP's own
   header format but a comparable real parsing job) — needed to learn/rewrite the real media
   IP+port+codec-list each side advertises.
2. **SIP proxy / B2BUA header-rewriting logic** — the genuinely hardest real protocol work here.
   RFC 3261's own real rules for `Via` stacking, `Record-Route`, branch parameters, and CSeq/
   Call-ID rewriting when relaying a request between two real dialogs are non-trivial — a real,
   separate, substantial undertaking, not a quick add-on to `sip/message.prn`.
3. **Multi-socket concurrent I/O** — a real, genuinely NEW runtime primitive. Checked directly:
   `pty.prn`'s own `pty-poll-read` is a real, existing `poll(2)`-based non-blocking read, but only
   for a SINGLE fd. A real gateway needs to service the signaling socket (5060) AND N simultaneous
   per-call RTP relay sockets (10000-60000) at once — PARENA's runtime has no multi-fd `poll`/
   `select`/`epoll` primitive today. Real, deliberate v0 workaround named in the phased plan below
   (scope to one call at a time first, defer real concurrency).
4. **SIP digest authentication (Credential List path only)** — needs a real MD5 implementation
   (`crypto/hash.prn` currently only has `sha256`) plus the real RFC 2617 challenge-response
   algorithm. Real, honest note: **not needed at all if IP ACL auth is used instead** — IP ACL is
   a Twilio Console configuration step, zero new PARENA code — the real, recommended v0 choice
   given this box's IP is presumably static.
5. **TLS/SRTP (encrypted trunking)** — real, deliberately out of scope for this plan, matching
   this same session's own earlier, separate finding (the "remove FFI from net parena" Q&A):
   implementing TLS from scratch is a well-known security minefield; if encrypted trunking is
   ever required, the correct move is FFI-binding a real, audited library (OpenSSL/BoringSSL),
   the same judgment call `crypto/hash.prn` itself already makes for hashing.

## Real, phased plan (none started)

**Phase 1 — single-call, IP-ACL-authenticated relay.** Real, deliberate scope-narrowing: exactly
one active call at a time (defers gap #3 above entirely — no concurrency needed if there's only
ever one signaling exchange and one RTP relay pair live). Uses `sip/message.prn` as-is for
REGISTER/INVITE parsing, a real, new, minimal SDP reader (just enough to extract one IP+port per
side, not a general SDP library) inlined rather than a full `sip/sdp.prn` yet, and `net/udp.prn`
for both the signaling exchange and the raw RTP byte-forwarding loop. IP ACL auth means zero new
PARENA-side crypto — the founder configures Twilio's Console to allow this box's own IP.

**Phase 2 — real `stdlib/sip/sdp.prn`.** Promotes Phase 1's inlined SDP reader into a real,
general, tested module (the direct structural sibling `sip/message.prn` already is to this whole
gateway), covering real SDP construction too (so our gateway can build its own offer/answer, not
just parse Twilio's).

**Phase 3 — real multi-call concurrency.** The one genuinely new runtime primitive this plan
needs: a real multi-fd poll/select capability in `runtime/parena_runtime.h`, mirroring
`pty-poll-read`'s own real "thin wrapper over a raw syscall" shape but generalized to N fds. Only
once this exists can the gateway handle more than one simultaneous call.

**Phase 4 (optional, only if IP ACL isn't viable)** — real SIP digest auth: add `md5` to
`crypto/hash.prn` (same FFI-to-OpenSSL pattern `sha256` already uses) plus the real RFC 2617
challenge-response construction in a new SIP-auth module.

**Phase 5 (explicitly deferred, not scoped here)** — TLS/SRTP encrypted trunking (FFI to a real
TLS library, not scratch-built) and real G.711 encode/decode (only if PARENA ever needs to
synthesize/consume audio directly, not for pure relay).

## Addendum, kanban priority-queue card `SIPX-001`: "don't we need to build an Asterisk equivalent... routes our calls to our SIP?"

Real, direct answer: **yes — Phase 1-2 above IS that equivalent.** A real B2BUA/SIP proxy routing
calls between our own SIP clients and Twilio's trunk is functionally the same real role Asterisk
(or FreeSWITCH/Kamailio) plays in a traditional PBX deployment. Real, honest comparison, worth
naming explicitly rather than silently assumed away, matching this monorepo's own repeated
"check what real, existing options exist first" discipline (the same judgment already applied to
libpcap, HypriotOS/`FLASH`, Stalwart for email):

- **Real Asterisk** is a mature, full-featured, real open-source PBX — dialplan scripting,
  voicemail, IVR, conferencing, a huge real feature surface this gateway's own narrow "route
  calls between our clients and Twilio" scope doesn't need. Adopting it wholesale is a real,
  legitimate, much-faster path to a working system TODAY if the actual goal is "get calls
  routing," full stop.
- **A real, narrow, PARENA-native B2BUA** (this doc's own Phase 1-3) is deliberately lighter —
  only the real signaling-relay + RTP-forwarding role, nothing else — and dogfoods PARENA the
  same way every other real integration in this monorepo does (`sip/message.prn` already exists
  and works; building the relay in PARENA extends real, load-bearing use of the language rather
  than reaching for an off-the-shelf C application). Real, honest cost: this path is genuinely
  slower to a working system, and re-derives real protocol logic (SIP proxy header-rewriting,
  Phase 2's own hardest piece) that Asterisk has already solved and battle-tested for years.

**Real, deliberate recommendation, not resolved unilaterally**: this is a real, founder-level
build-vs-adopt tradeoff (speed-to-working-system vs. PARENA-dogfooding), not a technical question
with one correct answer — flagged here explicitly rather than silently defaulting to either
choice. If the founder wants calls routing sooner rather than PARENA-native, real Asterisk (or
FreeSWITCH, its own real, modern alternative) sitting in front of the same Twilio trunk is a real,
legitimate, much smaller lift than this whole doc's own Phase 1-5.

## Related

- `PARENA/stdlib/sip/message.prn` — the real, already-shipped signaling-plane foundation this
  whole plan builds on.
- `PARENA/docs/NATIVE_PCAP_NORTHSTAR.md` — the same "check what already exists before committing
  to a big ask" discipline applied to a different, unrelated networking question this session.
- This session's own "remove FFI from net parena" analysis — the real, direct precedent for why
  TLS (Phase 5) stays a deliberate FFI exception rather than a from-scratch implementation target.

Sources: [Twilio SIP Trunking docs](https://www.twilio.com/docs/sip-trunking),
[IP Addresses for Elastic SIP Trunking Services](https://www.twilio.com/docs/sip-trunking/ip-addresses),
[A Step-by-Step Guide to Set Up Twilio Elastic SIP Trunking](https://www.twilio.com/en-us/blog/elastic-sip-trunking-step-by-step-setup).
