# What is on which character? -- a tutorial

You have several characters and want to know who has what: gold, bags, bank, mail, gear. This walks you through it from
nothing to answers, in game and outside it.

**The short version, once it is set up:**

| You want to know... | Type in game | Or outside the game |
|---|---|---|
| everyone's gold, level, zone at a glance | `/chars` | `scripts/forge.sh characters` |
| everything one character has (gear, bags, bank, mail) | `/chars emily` | `scripts/forge.sh show emily` |
| who has an item, and where | `/chars find linen` or `/rock who has linen` | `scripts/forge.sh find linen` |
| everyone's coin, and the total | `/rock gold` | |
| who is running out of bag space | `/rock bags` | `scripts/forge.sh needs` |
| the whole account in a spreadsheet | | `scripts/forge.sh ledger` -> open `ledger.csv` in Excel |
| every command there is | `/hhelp` | `scripts/forge.sh help` |
| a report on Slack every time you log out | | `scripts/forge.sh watch` with `FORGE_SLACK_WEBHOOK` set (section 6) |
| what the FirstAid macro will use, and why | `/firstaid` | |

---

## 1. Install the addons (once)

The addons are built from this repository. On Windows, use Git Bash or MSYS2.

**a. Find your client's interface number.** Log in to any character and type:

```
/run print((select(4, GetBuildInfo())))
```

It prints a number (for example `11507`). That is your `WOW_INTERFACE`.

**b. Build and copy the addons into the game.** Close the game first. Then, in this repository:

```sh
export WOW_INTERFACE=11507                                   # the number from step a
export WOW_ADDONS_DIR="C:/Games/World of Warcraft/_classic_/Interface/AddOns"   # your client's AddOns folder
scripts/wow-package-collection.sh --install
```

(Or build without `--install` and copy every folder in `build/wow/client/AddOns/` into your AddOns folder yourself.)

**c. Turn them on.** At character select, click **AddOns** and make sure these are ticked: **REFLUX Core** (everything
needs it), **Collection** (the `/chars` window and `/hhelp`), **Character Board**, **Fafnir's Bed**, **Hrungnir** (the
rock) and **MacroSync**. Tick **Load out of date AddOns** if the game asks.

## 2. Let the addons meet each character (once per character)

The addons can only know what a character has after that character has been online with them installed.

For **each** character:

1. **Log in.** Gold, level, zone, worn gear and bags are recorded straight away.
2. **Open the bank once** (talk to a banker). The bank can only be read while it is open.
3. **Open a mailbox once.** Same for mail.
4. **Log out**, or type `/reload`. WoW only writes the addons' data to disk at logout, `/reload` or exit. Until then,
   the *other* characters can't see this one's latest state.

After that, every character can see every other one, as of when each last logged out. The character you are playing
is always live.

## 3. Look it up in game

### `/chars` -- every character in one window

Type `/chars` (or open `/collection` and click **Characters**).

- **Left:** every character on the account, highest level first: name, level, gold. The account's total gold is
  at the top.
- **Click one** to see its sheet on the right:
  - level, xp, rested, item level, zone, when it was last seen, professions, gold;
  - **Equipped:** what it is wearing, slot by slot (and any gear you planned for it in the forge);
  - **Bags**, **Bank**, **Mail:** every item and how many. If a section says *not read yet*, that character has not
    opened its bank or mailbox since the addons were installed (step 2);
  - **The last week:** its level and gold day by day.
- **Find box** (top of the window): type part of an item name and press Enter to see **who has it and where**, e.g.
  `Ash-Forever  20  (bags 20)`. Click a character to go back to its sheet.

Shortcuts: `/chars emily` opens Emily's sheet directly; `/chars find copper ore` searches straight away.

### `/rock` -- just ask

The pet rock (Hrungnir) answers in chat, in plain words:

```
/rock gold                        every character's coin, and the account total
/rock how much coin does ash have one character's coin
/rock bags                        free bag space per character; FULL / nearly full; empty bag slots
/rock levels                      every character's level, class and where it was
/rock who has linen               who holds it (also: "where is ...", "find ...")
```

It also takes requests and helps you move things between characters:

```
/rock emily needs 20 linen        writes it down; whoever has linen hears about it at their next login
/rock i need a bag                the same, for the character you are on
/rock mail                        what this character could send, and to whom
```

At a **mailbox**, the rock shows a **Pack** button for each request this character can fill. Click it, check the
mail, press **Send**. The request comes off the list.

### Other commands

- `/fafnir` -- the whole account's inventory totals; `/fafnir linen` -- who has it, net count.
- `/need bag`, `/need linen 20` -- the same requests as the rock, the short way.
- `/hhelp` -- every command of every addon; `/hhelp rock` -- one addon in full.

## 4. Look it up outside the game (the forge)

The forge reads the same data from your WoW folder, so you can check while the game is closed (or on a second screen).

Tell it where things are (once per terminal; put these lines in your shell profile to make them stick):

```sh
export FORGE_DIR=~/wow-forge                                   # where the forge keeps its files (made if missing)
export WOW_WTF_DIR="C:/Games/World of Warcraft/_classic_/WTF"  # your client's WTF folder
```

Then, from this repository:

```sh
scripts/forge.sh characters        # everyone: class, level, gold
scripts/forge.sh show emily        # one character: xp, gold, every slot, bags, bank, mail
scripts/forge.sh find linen        # who has it, and where
scripts/forge.sh gear emily        # worn gear by slot
scripts/forge.sh needs             # full bags, empty bag slots, requests, big gear upgrades -- and who can fill each
scripts/forge.sh upgrades          # spare gear that would improve another character
```

**The spreadsheet.** `scripts/forge.sh ledger` writes `ledger.csv` in your `FORGE_DIR`: one row per item per character
(worn, bags, bank, mail), each with the character's class, race, level, gold and zone. Open it in Excel and filter or
sort: by `item` to see who has something, by `character` to see one character's things, by `location` for banks only.

**Keep it up to date by itself.** Leave this running in a terminal:

```sh
scripts/forge.sh watch
```

Every time WoW saves (each logout or `/reload`), it rewrites `ledger.csv` and `board.json`/`board.tsv`.

**The page.** `scripts/forge.sh serve` opens the same views at http://127.0.0.1:7744 in your browser.

**SHANKPIT's big screen.** With `FORGE_DIR` set and `forge.sh watch` running, press **I** in SHANKPIT: every
character, the needs list, and each character's gear on a full-screen board (PageUp/PageDown to turn pages).

## 5. FirstAid -- one bandage button for every character

After `/ascend`, every character has a macro called **FirstAid** (MacroSync puts it on your bars). Press it:

- it uses **the best bandage you carry that your First Aid skill can use** -- on the friend under your mouse, else your
  friendly target, else yourself;
- **shift**-press drinks **the best healing potion** you have that your level allows;
- with no bandages, a plain press drinks the potion; with nothing at all, it **opens First Aid** so you can make some.

It keeps itself up to date: when a stack runs out, you loot better bandages, your First Aid goes up or you level, the
macro changes by itself half a second later (after the fight, if you are in one -- the game does not let macros change in
combat). It is one account macro, so it never takes one of your character's macro slots, and it always follows whoever
is logged in.

`/firstaid` says what it holds and why -- for example *"Heavy Linen Bandage (you carry 3, heals 114) ... First Aid: 50"*,
*"you also carry Wool Bandage: it needs First Aid 80"*, *"Shift-click: Minor Healing Potion (you carry 2)"*.
`/ascend firstaid off` stops it following your bags (`on` to resume). If you already had your own macro called
FirstAid, MacroSync leaves it alone and says so -- rename yours to get this one.

Limits: it knows the classic bandages (Linen to Heavy Runecloth) and healing potions (Minor to Major) by their classic
item ids, not battleground bandages. While you are *Recently Bandaged*, a press still bandages a friend and on yourself
drinks the potion -- but only once the fight is over (in combat the macro cannot change, so a second press on yourself
within that minute is refused; use shift).

## 6. The rock on Slack -- a report every time you stop playing

WoW addons cannot reach the internet, so the rock speaks from outside the game: `forge watch` (section 4) notices every
save WoW makes (logout, `/reload`, quitting) and the rock posts a short report to a Slack channel. It **remembers** what it
told you last time, so each report says only what changed: who levelled, who gained or lost a gold or more, whose bags
are filling up, and the account's needs (urgent first), each with who on the account can fill it. Things already reported
are not marked new again.

**Set it up once:**

1. In Slack, make an incoming webhook for the channel you want: <https://api.slack.com/messaging/webhooks> -- create an
   app, turn on *Incoming Webhooks*, *Add New Webhook to Workspace*, pick the channel, copy the URL
   (`https://hooks.slack.com/services/...`). **Treat that URL like a password:** anyone with it can post to the channel.
2. Put it in your environment, never in a file inside a repository. In Git Bash or MSYS2:
   `export FORGE_SLACK_WEBHOOK='https://hooks.slack.com/services/...'` (add the line to `~/.bashrc` to keep it). Or for
   every new window on Windows: `setx FORGE_SLACK_WEBHOOK "https://hooks.slack.com/services/..."`, then open a new terminal.
3. Leave the watcher running while you play: `scripts/forge.sh watch`. After each logout or `/reload` it prints
   `rock: reported to Slack`.

**Try it without Slack:** `scripts/forge.sh rockreport --dry` prints what the rock would say now and changes nothing.
`scripts/forge.sh rockreport` says it and remembers (the memory is `rock_memory.lua` in your `FORGE_DIR`; delete it and
the next report starts fresh).

Limits: the post uses `curl` (built into Windows 10 and 11, Git Bash and MSYS2); it is a stopgap until PARENA has its own
HTTPS client. Tested against a simulated WoW save and a captured message, not yet against a real Slack workspace.

## 7. When something looks wrong

| You see | Why, and what to do |
|---|---|
| A character is missing | It has not logged in since the addons were installed. Log in once, then log out. |
| A character's info is old | The game saves at logout. Log out (or `/reload`) on that character. |
| Bank or mail says *not read yet* | Open the bank / a mailbox once on that character, then log out. |
| `/chars` or `/rock` does nothing | The addons are not loaded: tick them in the AddOns list at character select, including REFLUX Core. |
| An addon says it is *out of date* | Rebuild with your current `WOW_INTERFACE` (step 1), or tick *Load out of date AddOns*. |
| The forge shows only planned characters | `WOW_WTF_DIR` is not set, or points to the wrong folder (it must contain `Account/`). |
| `ledger.csv` did not update | It is open in Excel. Close it; the new one is waiting beside it as `ledger.csv.tmp`. |
| Bag space shows as unknown | That character has not logged in since bag space started being recorded. Log in once. |

Honest status: everything here is tested against a simulated WoW client, not yet against the live one. If a command
behaves differently in the real game, `/hhelp` and the messages it prints are the quickest way to tell what it expected.
