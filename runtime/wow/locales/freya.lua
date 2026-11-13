-- runtime/wow/locales/freya.lua -- Freya's Runway's catalogue (spec addon 61). Data; enUS fallback.
local _, ns = ...
local enUS = {
    NAME = "Freya's Runway",
    HELP = "/runway start [names...] -- each model walks for 30 s; judges type a score from 1 to 10   /runway stop",
    START = "%s: %d models tonight. Judges, type a score from 1 to 10 while each one walks!",
    WALK = "On the runway: %s! Judges, your scores (1-10)...",
    SCORED = "%s scores %.1f from %d judges.",
    OVER = "%s: the show is over!",
    ROW = "  %d. %s -- %.1f",
    CROWN = "%s takes Freya's crown!",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
