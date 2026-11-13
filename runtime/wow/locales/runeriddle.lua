-- runtime/wow/locales/runeriddle.lua -- Rune Riddle's catalogue (spec addon 56). Data; enUS fallback.
local _, ns = ...
local enUS = {
    NAME = "Rune Riddle",
    HELP = "/riddle mark <riddle> -- this spot is the next stop   /riddle list | clear   /riddle start | stop",
    MARKED = "%s: stop %d saved where you stand.",
    CLEARED = "%s: all stops forgotten.",
    EMPTY = "%s: no stops yet -- /riddle mark <riddle> at each spot first.",
    RIDDLE = "%s, riddle %d of %d: %s",
    REACHED = "%s solved it!",
    DONE = "%s: the hunt is done -- %s reached the last stop. %d seconds in all.",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
