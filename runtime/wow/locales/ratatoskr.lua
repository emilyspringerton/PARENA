-- runtime/wow/locales/ratatoskr.lua -- Ratatoskr Roll's catalogue (spec addon 65). Data, not code; see hrungnir.lua
-- for the fallback rule. CRITTER_TYPE is what UnitCreatureType() returns for a critter IN THIS LOCALE -- the one
-- string here that changes behaviour, so a translator must copy it from the client, not translate it.
local _, ns = ...
local enUS = {
    NAME = "Ratatoskr Roll",
    CRITTER_TYPE = "Critter",
    LOGGED = "%s: %s -- %d acts of affection, all time.",
    AWARD = "%s: Critter of the Week was %s, with %d acts of affection. Squeak.",
    NONE_WEEK = "No critters have been loved this week. Ratatoskr is disappointed.",
    TOP_HEAD = "%s: this week's most loved critters --",
    TOP_ROW = "  %d. %s (%d)",
    TOTALS = "All time: %d critters, %d acts of affection.",
    HELP = "Target a critter and /love, /pet or /wave at it to put it in the census.  /census -- this week's top three",
    NO_HOOK = "%s: this client has no DoEmote to listen to; the census cannot count.",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
