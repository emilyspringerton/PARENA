-- runtime/wow/locales/collection_hub.lua -- the Collection hub's catalogue. Data, not code; enUS fallback.
local _, ns = ...
local enUS = {
    TITLE = "Collection",
    BUILD = "Build %s  ·  interface %s  ·  journal keeps %d rows, polls every %d s",
    BUS = "REFLUX: %d events this session, %d held in the log",
    LOADED = "loaded", NOTLOADED = "not loaded", NOTHING = "nothing saved yet",
    PERCHAR = "saves per character in %s", ACCOUNT = "saves account-wide in %s", NOSAVE = "saves nothing",
    ENTRIES = "%d entries", ROWS = "%d rows held",
    CLOSE = "Close",
    FINISH = "Click to put it in the chat box, then type the rest.",
    TYPEIT = "Type it in chat: %s",
    NOCMD = "no command",
    HELP = "/collection -- every Collection addon, its settings and a button for each command   /collection list -- the same, in chat",
    NOFRAMES = "%s: this client has no frames API; here is the list instead.",
    MISSING = "%s: RefluxCore's collection/hub module or this package's registry is missing.",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
