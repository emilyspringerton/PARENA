-- runtime/wow/locales/bragi.lua -- Bragi's Verse's catalogue (spec addon 63). Data; enUS fallback. The syllable
-- checker counts English only; other locales still play, judged by the host.
local _, ns = ...
local enUS = {
    NAME = "Bragi's Verse",
    HELP = "/verse duel <poet> <poet> -- each posts one line: first / second / third   /verse check a / b / c   /verse stop",
    START = "%s: %s and %s, a haiku each -- one chat line, the three lines split by \" / \". Five, seven, five!",
    SCORED = "%s: %d-%d-%d syllables, %d points.",
    WINNER = "%s: %s wins the duel, %d to %d!",
    TIE = "%s: a tie at %d points. Bragi is moved.",
    CHECK = "Your haiku: %d-%d-%d syllables -- %d points.",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
