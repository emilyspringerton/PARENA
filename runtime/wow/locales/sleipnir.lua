-- runtime/wow/locales/sleipnir.lua -- Sleipnir Sprint's catalogue (spec addon 60). Data; enUS fallback.
local _, ns = ...
local enUS = {
    NAME = "Sleipnir Sprint",
    HELP = "/sprint start | finish -- set the lines where you stand   /sprint go [laps]   /sprint board | stop",
    SET_START = "%s: start line set here (the course's board is cleared).",
    SET_FINISH = "%s: finish line set here.",
    NEEDSTART = "%s: set a start line first (and a finish on the same map).",
    READY = "%s: racers to the line! %d lap(s). Starting in...",
    GO = "GO!",
    FINISHED = "%s finishes in place %d -- %s s!",
    RESULTS = "%s: results --",
    ROW = "  %d. %s -- %s s",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
