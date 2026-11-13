-- runtime/wow/locales/alfar.lua -- Álfar Revel's catalogue (spec addon 64). Data; enUS fallback.
local _, ns = ...
local enUS = {
    NAME = "Álfar Revel",
    HELP = "/revel start [beats] [seconds per beat] -- do the called emote before the next beat   /revel stop",
    START = "%s: the revel begins -- %d beats. Do the called emote on every beat to build your streak!",
    BEAT = "Beat %d: %s",
    OVER = "%s: the music stops. Best streaks:",
    ROW = "  %d. %s -- %d in a row",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
