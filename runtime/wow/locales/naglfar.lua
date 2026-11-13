-- runtime/wow/locales/naglfar.lua -- Naglfar Crew's catalogue (spec addon 71). Data; enUS fallback.
local _, ns = ...
local enUS = {
    NAME = "Naglfar Crew",
    HELP = "/sardines start <hider> [seconds] -- the hider hides, everyone else seeks   /sardines stop",
    HIDE = "%s: %s is the sardine! %d seconds to hide -- everyone else, close your eyes.",
    SEEK = "%s: find %s, then squeeze in quietly. Last one out loses!",
    SQUEEZED = "%s squeezed into the tin (number %d).",
    LAST = "%s: %s was left on the dock! (%d of %d made it into the tin)",
    ALLIN = "%s: time's up -- %d of %d found the tin.",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
