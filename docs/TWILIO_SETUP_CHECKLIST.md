# Twilio Elastic SIP Trunk — real setup checklist

Kanban: "ok we have a platform we have a pbx we have a sip phone do all the plumbing while i
sign up for twillio." This is the real, concrete Twilio Console side of that plumbing — the
Asterisk side (`ops/asterisk/*.conf`) is already deployed and waiting for the one real value
this doc produces: your Trunk's own Termination domain.

Verified against Twilio's own current, real docs (2026-09-05) —
[twilio.com/en-us/blog/elastic-sip-trunking-step-by-step-setup](https://www.twilio.com/en-us/blog/elastic-sip-trunking-step-by-step-setup),
[twilio.com/docs/sip-trunking/ip-addresses](https://www.twilio.com/docs/sip-trunking/ip-addresses),
and their own official [Elastic SIP Trunking – Asterisk Configuration Guide](https://docs-resources.prod.twilio.com/documents/TwilioElasticSIPTrunking-AsteriskPBX-Configuration-Guide-Version2-1-FINAL-09012018.pdf)
— not guessed at. The `ops/asterisk/*.conf` files already apply that guide's own real,
documented PJSIP endpoint fields (`rewrite_contact`, `rtp_symmetric`, `dtmf_mode=rfc4733`) and
E.164 number formatting requirement.

## This box's own real values (already deployed, don't re-derive)

- **Public IP**: `198.58.107.85` (confirmed live this session via `curl ifconfig.me`)
- **SIP port**: `5060`, UDP (Asterisk's own PJSIP transport, already listening — confirmed via
  `ss -tulnp`)
- **Extension**: `1000` (the CarePyre SIP Phone's own account — `ops/asterisk/
  pjsip_carepyre_phone.conf`, deployed with a real, freshly-generated password, printed once to
  the terminal that ran `sudo-queue/52-carepyre-asterisk-plumbing-deploy.sh`, not stored here)

## Steps (Twilio Console)

1. Log into the [Twilio Console](https://www.twilio.com/login) with your new account.
2. Left nav → **All Products & Services** → **Elastic SIP Trunking**.
3. **Get Started** → **Create a SIP Trunk**.
4. Friendly name: `CarePyre` (or whatever you'd like — cosmetic only). **Create**.
5. Under the new trunk, select **Termination**.
6. **Termination SIP URI**: pick a unique name, e.g. `carepyre.pstn.twilio.com` — this becomes
   your real Trunk domain.
7. Under **Authentication**, click **+** next to **IP Access Control Lists**:
   - Friendly name: `CarePyre box`
   - IP address: `198.58.107.85` (this box's own real public IP, above)
   - **Create ACL**, then make sure it's attached to this Trunk.
   - **Credential Lists are optional for v0** — `ops/asterisk/pjsip_twilio_trunk.conf` uses IP
     ACL auth only (Twilio's own docs require "IP Access Control Lists **and/or** Credential
     Lists" — IP ACL alone satisfies that). Twilio's own official Asterisk configuration guide
     provisions both as extra redundancy; that config file has a real, ready-to-uncomment block
     for adding Credential List auth later if you want it — not required to place/receive calls
     today.
8. **Save**.
9. Under the trunk, select **Origination** (this is how Twilio routes an INBOUND call — from a
   real phone number — to this box).
10. **Add new Origination URI**: `sip:198.58.107.85:5060`, priority `10`, weight `10`. **Add**.
    (Twilio's own guide shows a `;region=us1` suffix for a specific regional route — not needed
    for a single-box setup like this one; a bare `sip:<ip>:5060` URI is the real, correct,
    simpler form when you don't need per-region routing.)
11. Under the trunk, select **Numbers** → **Buy a Number** → pick one → **Buy**.

## The one real value to bring back here

Step 6 above gives you the real Termination domain (e.g. `carepyre.pstn.twilio.com`). Run this
on the box to finish wiring it in:

```bash
sudo sed -i "s/__TWILIO_TRUNK_DOMAIN__/carepyre.pstn.twilio.com/" /etc/asterisk/pjsip_twilio_trunk.conf
sudo asterisk -rx "pjsip reload"
```

(Replace `carepyre.pstn.twilio.com` with whatever you actually named it in step 6.)

## Testing once both sides are wired up

- **Inbound** (a real phone calls your new Twilio number) → Twilio's Origination sends the
  INVITE to `198.58.107.85:5060` → Asterisk's `[from-twilio]` context
  (`ops/asterisk/extensions_carepyre.conf`) rings extension `1000`.
- **Outbound** (extension `1000` dials a real US number) → Asterisk's `[carepyre-internal]`
  context routes it to `twilio-endpoint` → Twilio's Termination places the real PSTN call.
- Register a real SIP softphone (Zoiper, Linphone — free, real apps) as extension `1000` against
  `198.58.107.85:5060` to test either direction today, independent of whether CarePyre's own
  Android app has its native SIP core wired in yet (it doesn't — see
  `CarePyre/docs/SIP_PHONE_ANDROID_NORTHSTAR.md`'s own gap #2).

## What this doesn't cover (real, honest, not glossed over)

- **Voicemail** — `[from-twilio]`'s own dialplan just hangs up on an unanswered call, no
  voicemail box. Real, separate, named follow-up.
- **The Android app's own native SIP signaling** — still blocked on the SDL2/NDK
  cross-compile gap (`CarePyre/docs/SIP_PHONE_ANDROID_NORTHSTAR.md`). A softphone is the real,
  honest way to test this plumbing today.
- **DNS** (`carepyre.org` pointing Origination at a hostname instead of a bare IP) — real,
  optional polish, not required for calls to work; the bare IP above is real and sufficient.
