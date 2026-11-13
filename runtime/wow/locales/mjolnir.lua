-- runtime/wow/locales/mjolnir.lua -- Mjölnir Pass's catalogue (spec addon 55). Data; enUS fallback.
local _, ns = ...
local enUS = {
    NAME = "Mjölnir Pass",
    HELP = "/tag start [seconds] -- the hammer goes to someone at random; touch someone to pass it   /tag stop",
    START = "%s: %s holds the hammer! Touch someone to pass it on. No tag-backs for 3 seconds.",
    PASSED = "%s passed the hammer to %s!",
    OVER = "%s: time! Seconds holding the hammer:",
    ROW = "  %d. %s -- %d s",
    LONGEST = "%s held Mjölnir longest: %d seconds. Worthy? Debatable.",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
