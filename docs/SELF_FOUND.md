# Self Found

`SelfFound` is a WoW Forever addon, and a REFLUX mod like the rest of the Collection. It records every
transaction on every character and marks each character **self-found** until it breaks the doctrine. A self-found
character's vendor visits are what map the game's vendors: who sells what, where and for how much.

## The doctrine (founder, 2026-11-17)

A character stops being self-found, for good, the first time it does any of these:

| breaks self-found | stays self-found |
| --- | --- |
| buying on the auction house (a bid, a buyout, a commodity purchase) | scanning or searching the auction house |
| posting to the auction house | buying, selling, buying back and repairing at vendors |
| completing a trade with another player that moves anything | looting, quest rewards |
| taking an item or money from mail another player sent, **own alts included** | mail from the auction house or an NPC, and its own returned mail |
| withdrawing items or gold from a guild bank | depositing into a guild bank, sending mail |

On the auction house, the **call** is what counts: trying to buy or post breaks it even if the server then refuses.

Status:
- **self-found since level 1**: the character was first seen at level 1, so nothing was missed.
- **self-found since tracking began**: it was first seen above level 1. Its earlier life is unknown, so it is never
  shown as verified.
- **broken**: Self Found keeps the date and the reason.

## In game

- `/sf`: every character's status and transaction count, and how many vendors have been found.
- `/sf vendors`: each vendor with its NPC id, zone, position, how many items it sells, and how many visits it has had.
- `/sf log`: the latest 15 transactions.
- `/sf why`: any transaction kind this client can't be watched for, if one is missing.

## Where things live

| what | where |
| --- | --- |
| the doctrine and every record | `stdlib/collection/selffound.prn` (PARENA, compiled to Lua) |
| what the client did, from events and hooks | `runtime/wow/self_found.lua` (the source header says how each kind is seen) |
| saved data | `SelfFoundDB` (account-wide): `chars`, `tx` (at most 20,000 rows), `vendors` |
| REFLUX rows | 3070 for each transaction (kind, a, b), 3071 when self-found breaks (kind, status before). The 3070-3079 range is claimed and waiting for the lead to confirm it in BOARD.md |
| test | `lua5.1 tests/reflux_wow/selffound_test.lua <packaged client>`: 65 checks |
| env | `SELFFOUND_ADDON` / `SELFFOUND_VAR` (folder and saved-variables name; default `SelfFound` / `SelfFoundDB`) |

## The feed: Self Found outside the game (SHANKPIT's stream overlay)

```bash
WOW_WTF_DIR="<WoW>/_forever_/WTF" COLLECTION_FEED=D:/WORKSPACE/data/collection/feed.txt bash scripts/collection-sf-feed.sh
```

Run it after logging out of WoW (the client writes SavedVariables on logout). It reads every account's
`SelfFound.lua` and writes one feed file. That file is REFLUX rows plus a name table:

| row | a | b | c |
| --- | --- | --- | --- |
| 3072 character | name id | status | transactions |
| 3073 broken | name id | kind that broke it | when |
| 3074 vendor | name id | npc id | items |
| 3075 summary (last) | characters | still self-found | vendors |

Which rows get written is `selffound.prn` (`sf-feed-merge`, `sf-feed-summary-row`). `COLLECTION_FEED` has no
default, and the file holds play data, so keep it outside every repository.

SHANKPIT reads the same file through its own `COLLECTION_FEED`. In broadcast mode, **Ctrl+Shift+C** shows it on
the stream (SHANKPIT `packages/collection/`). The overlay's decisions are PARENA too:
`stdlib/shankpit/collection_overlay_rules.prn`.

Test: `lua5.1 tests/reflux_wow/sf_feed_test.lua <compiled sf.lua> <scratch dir>`: 15 checks, two accounts.

## Known limits

- None of the client calls is confirmed on WoW Forever yet. Each one is feature-tested, and `/sf why` names any
  that are missing.
- A vendor sale is detected as items leaving the bags while the money goes up with the merchant window open,
  because selling has no call to hook.
- A trade counts once both sides accept and the client reports "Trade complete".
