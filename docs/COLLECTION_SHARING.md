# Sharing the Collection of Value addons

How a teammate gets every Collection addon (the REFLUX mods PARENA builds for WoW Forever) without
building PARENA themselves.

## What ships

One zip, `CollectionOfValue.zip`, holds every addon `scripts/wow-package-collection.sh` builds:
`RefluxCore` (the shared REFLUX log that every other addon lists in `## Dependencies:`), `CollectionHub`
(`/collection`), `CollectionJournal`, `FafnirsBed`, `CharacterBoard`, `MacroSync`, `Huginn`, the fun addons
(`Hrungnir`, `RatatoskrRoll`, `Volva`, `SagaOfFools`) and the party games. It also holds `reader/`, the
outside-the-game journal reader and CSV export, plus `README.txt` and `VERSION`. Tabled addons (EcoWar) are
never in it.

The addons are shared as one set, not as separate downloads, because each one is a REFLUX mod that needs
`RefluxCore`, and `CollectionHub` reads its registry from the addons packaged beside it.

## Where it ships

Every green push to `main` makes a GitHub Release (`Tag & Release` in `.github/workflows/ci.yml`). When the
`collection_client` job packaged the client, that release carries:

* `CollectionOfValue-<sha>.zip`, pinned to the commit;
* `CollectionOfValue.zip`, the same file under a fixed name, so this URL always gives the newest one:
  `https://github.com/emilyspringerton/PARENA/releases/latest/download/CollectionOfValue.zip`

PARENA is a public repository, so no GitHub account is needed to download it.

### The one setting that turns it on

The packager will not guess the TOC interface number. CI reads it from the repository variable
`WOW_INTERFACE`, and **that variable is not set yet**, so every release so far has shipped without the
zip. The founder confirmed the value in game on 2026-11-13: **16001** (client 1.60.1, see
`docs/COLLECTION_OF_VALUE_STEP1_AND_DISPUTES.md` in the monorepo). The founder sets it once:

> GitHub, `emilyspringerton/PARENA`, Settings, Secrets and variables, Actions, Variables tab,
> New repository variable: name `WOW_INTERFACE`, value `16001`.

The next green push to `main` then attaches the zip. When a client patch changes the interface number,
update the variable. Agents cannot set it, because repository variables are not on the `ghapi` allowlist.

## Installing (teammate)

### Windows, with the script

Download `scripts/install-collection.ps1` from this repository, then in PowerShell:

```powershell
$env:WOW_ADDONS_DIR = 'D:\World of Warcraft\_forever_\Interface\AddOns'   # your own client's AddOns folder
powershell -ExecutionPolicy Bypass -File .\install-collection.ps1
```

It downloads the latest `CollectionOfValue.zip`, replaces only the Collection's own addon folders, and leaves
other addons alone. Two optional variables change that:

* `COLLECTION_RELEASE=v0.397.0` pins a release.
* `COLLECTION_ZIP=<path>` installs a zip already on disk, without downloading.

`WOW_ADDONS_DIR` has no default, because the script cannot know where a teammate installed the client.

### Any OS, by hand

1. Download `CollectionOfValue.zip` from the latest release (URL above).
2. Copy every folder in its `AddOns/` into the client's `Interface/AddOns` (WoW Forever:
   `<WoW>/_forever_/Interface/AddOns`).
3. Restart the client or `/reload`, then run `/collection` in game.

### From source (developers)

```bash
. ../scripts/collection-env.sh          # monorepo env, including WOW_INTERFACE from collection.local.env
bash scripts/wow-package-collection.sh --install
```

## Not covered here

* CurseForge, Wago and WoWInterface accept addons for retail and Classic. Whether any of them list WoW
  Forever addons has not been checked. Publishing there is an outward-facing step for the founder to decide.
* `PARENA/plugins/static_analysis` is a Python developer tool, not a game addon, and is not shipped.
