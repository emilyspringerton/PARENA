# The Hoard: every character's gear, on this machine

One page, and one CSV, showing what every character holds: what they wear (slot by slot), their bags, bank
and mail, plus mail in flight between your characters. It runs on your PC at `http://127.0.0.1:8765/`. There's
no login and no public host; it only reads the files WoW already writes.

How it works: **Fafnir's Bed** (a Collection REFLUX mod) writes each character's gear into
`WTF/Account/<account>/SavedVariables/FafnirsBed.lua`. `tools/hoard` reads that file for every account under
`WOW_WTF_DIR`.

## Settings (env, from `MONOREPO/scripts/collection-env.sh`)

| var | meaning |
|---|---|
| `WOW_WTF_DIR` | the client's `WTF` folder (**required**, no default in the tool) |
| `WOW_ADDONS_DIR` | the client's `Interface/AddOns` folder (for `--install`) |
| `FAFNIR_ADDON` / `FAFNIR_VAR` | addon folder / SavedVariables global (`FafnirsBed` / `FafnirsBedDB`) |
| `HOARD_ADDR` | listen address, default `127.0.0.1:8765`; non-loopback refused unless `HOARD_ALLOW_LAN=1` |

## 1. Install the addon (once, and after each update)

With WoW closed, from Git Bash in `MONOREPO/PARENA`:

```bash
. ../scripts/collection-env.sh && bash scripts/wow-package-collection.sh --install
```

## 2. In game: record each character

Fafnir's Bed records a character only while that character is logged in. Do this once per character:

1. At the character select screen, open **AddOns** and make sure **RefluxCore** and **Fafnir's Bed** are ticked.
2. Log the character in and wait a few seconds. Bags, gold and **equipped gear** are read on login.
3. **Bank:** visit a banker and open the bank window. The bank can only be read while it's open, so a
   character that never opens it shows "Not opened".
4. **Mail:** open a mailbox, which reads the attachments.
5. Type `/fafnir` to check. It prints the account totals and names anything this client could not read.
6. Type `/reload`, or log out. WoW writes the SavedVariables file **only** on `/reload` or logout.
7. Switch to the next character and repeat.

After that, gear changes, looting, banking and mailing are kept up to date while you play. The page
follows each `/reload` or logout within a few seconds.

## 3. Out of game: the page and the CSV

From Git Bash in `MONOREPO/PARENA`:

```bash
bash scripts/hoard.sh
```

Then open `http://127.0.0.1:8765/`. Search an item ("where is it?") or a character name ("what do they
have?"); **By item** / **By character** switch views; **Download CSV** saves the whole table.

For the CSV alone:

```bash
bash scripts/hoard.sh -csv D:/WORKSPACE/data/collection/hoard.csv
```

The CSV columns are `character, realm, account, location, slot, item_id, name, count`. Location is one of
`equipped`, `bags`, `bank`, `mail` and `post`. `slot` (Head, Main hand, ...) is filled only for equipped
rows, because bags, bank and mail are kept as counts per item. Keep the CSV outside the repositories (D-8):
it is your play data.
