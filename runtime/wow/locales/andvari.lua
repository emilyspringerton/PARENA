-- runtime/wow/locales/andvari.lua -- Andvari's Map's catalogue (spec addon 58). Data; enUS fallback.
local _, ns = ...
local enUS = {
    NAME = "Andvari's Map",
    HELP = "/treasure mark -- bury an X here   /treasure map [n] -- draw it (into an open mail too)",
    MARKED = "%s: X number %d is buried here.",
    NOMARK = "%s: no X buried yet.",
    FOUND = "%s: you found treasure number %d!",
    INMAIL = "%s: the map is in your mail -- add a name and press Send.",
    SUBJECT = "A map. Do not ask.",
    CLUE_MIDDLE = "Clue: X lies at the very heart of the land.",
    CLUE = "Clue: X lies %s, %s.",
    BAND_1 = "a short walk from the middle", BAND_2 = "halfway out", BAND_3 = "near the edge of the land",
    DIR_1 = "to the north", DIR_2 = "to the north-east", DIR_3 = "to the east", DIR_4 = "to the south-east",
    DIR_5 = "to the south", DIR_6 = "to the south-west", DIR_7 = "to the west", DIR_8 = "to the north-west",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
