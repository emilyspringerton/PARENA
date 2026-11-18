# GuildHerald

`GuildHerald` is a WoW Forever addon and a REFLUX mod like the rest of the Collection. It keeps every guild's
**Message of the Day** and **guild info** text as a log, together with the Discord invites they carry.

The founder's ask (2026-11-17): *"im in these guilds and the message of the day has the discord info but i dont want
to type it in ... type /ascend in the shankpit terminal to sync my discord from the wow data ... store all of the
guild messages of the day into log streaming so we can work with that data to support our community"*.

## In game

`/herald` lists each guild your characters are in: its MOTD, how many messages are logged, and its Discord invites.
`/herald why` names any client call this client lacks.

What it reads (every call is checked before use):
- `GUILD_MOTD` (the MOTD as it is set, and at login);
- `GetGuildRosterMOTD` / `GetGuildInfoText` after asking the server for the roster (at most every 30 s);
- `GetGuildInfo("player")` for the guild's name and realm.

Every account's characters share one log, keyed `guild-realm`. Each entry says which character saw it.

## Decisions (PARENA)

All of these are in `stdlib/collection/guildherald.prn`:
- **A change:** a non-empty text different from the guild's current one. An empty MOTD before the roster loads is
  not a change.
- **History:** 30 entries per guild in the game. The log stream keeps everything.
- **Invite codes:** links are found case-insensitively and codes keep their case. A valid code is 2-32 letters,
  digits or `-`. Recognised forms: `discord.gg/`, `discord.com/invite/`, `discordapp.com/invite/`.
- **REFLUX:** 3080 a change (kind, length, invites). Feed rows 3082 guild, 3083 invite, 3084 summary.
- **Range:** 3080-3089 is claimed and waiting for the lead to confirm it in BOARD.md. SHANKPIT's `/ascend` uses 3085.

## Out of the game

```bash
WOW_WTF_DIR="<WoW>/_forever_/WTF" \
COLLECTION_GUILD_FEED=D:/WORKSPACE/data/collection/guild_feed.txt \
COLLECTION_GUILDS_LOG=D:/WORKSPACE/data/collection/guilds.ndjson \
bash scripts/collection-guild-feed.sh
```

- **`COLLECTION_GUILD_FEED`:** the Collection feed (REFLUX rows + names), rewritten each run. SHANKPIT's `/ascend`
  reads it.
- **`COLLECTION_GUILDS_LOG`:** an NDJSON log stream, appended. One line per logged MOTD or info text:
  `{"v":1,"type":"guild_text","seq":..,"at":..,"guild":..,"realm":..,"kind":"motd"|"info","seen_by":..,"text":..,"guild_invites":[..]}`.
  A `.state` file beside it remembers what has been written, so each entry appears once.

Both hold community and play data. Keep them outside every repository (Disputes D-8). Neither has a default.

**Test:** `lua5.1 tests/reflux_wow/guildherald_test.lua <packaged client> <compiled gh.lua> <scratch dir>`.

## Not yet

- Linking your Discord account, so `/ascend` knows which servers you're already in. It needs Discord sign-in on
  IDUNA (like HSRIP's Twitch) and a Discord application from the founder.
- Discord itself never lets an app join a server for a person, so joining stays one click.
