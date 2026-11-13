-- runtime/wow/locales/odinn.lua -- Óðinn Says' catalogue (spec addon 57). Data; enUS fallback.
local _, ns = ...
local enUS = {
    NAME = "Óðinn Says",
    HELP = "/odinn start -- you call, the group copies (copy only when Óðinn says!)   /odinn stop",
    START = "%s: %d players. Copy my emote ONLY when Óðinn says. Slowest or wrong is out!",
    REAL = "%s %s",
    TRAP = "%s",
    OUT_1 = "%s missed it -- out!", OUT_2 = "%s did the wrong emote -- out!",
    OUT_3 = "%s was the slowest -- out!", OUT_4 = "%s fell for it -- Óðinn did not say! Out!",
    WINNER = "%s: %s is the last one standing!",
    NOBODY = "%s: everyone is out -- Óðinn wins this one.",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
