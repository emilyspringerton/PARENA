-- runtime/wow/locales/holmgang.lua -- Holmgang's catalogue (spec addon 67). Data; enUS fallback.
local _, ns = ...
local enUS = {
    NAME = "Holmgang",
    HELP = "/holmgang start [names...] -- seed a cup (default: the group, shuffled)   /holmgang win <name>   /holmgang bracket | stop",
    START = "%s: a cup of %d fighters! Duel results are read automatically.",
    MATCH = "  Round %d: %s vs %s",
    WON = "%s advances!",
    NOMATCH = "%s has no open match.",
    CHAMPION = "%s: %s is the champion of the holmgang!",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
