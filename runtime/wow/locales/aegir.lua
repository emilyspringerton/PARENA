-- runtime/wow/locales/aegir.lua -- Ægir's Catch's catalogue (spec addon 62). Data; enUS fallback.
local _, ns = ...
local enUS = {
    NAME = "Ægir's Catch",
    HELP = "/derby start [minutes] -- a fishing derby for the group   /derby stop | record",
    START = "%s: the derby is on -- %d minutes! Biggest catch (vendor value) and weirdest (rarest) win.",
    BIGGEST = "%s takes the lead: %s, worth %s!",
    OVER = "%s: lines in! %d catches.",
    WIN_BIG = "  Biggest: %s with %s (%s).",
    WIN_WEIRD = "  Weirdest: %s with %s.",
    RECORD = "All-time biggest: %s with %s (%s).",
    NORECORD = "%s: no derby has finished yet.",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
