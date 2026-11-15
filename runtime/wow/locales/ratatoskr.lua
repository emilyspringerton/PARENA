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
    HELP = "Target a critter and /love, /pet or /wave at it to put it in the census.  /census -- this week's top three   /census rumor -- gossip about your other addons",
    NO_HOOK = "%s: this client has no DoEmote to listen to; the census cannot count.",
    -- /rumor: Ratatoskr's voice for a tip about another addon (%s = this addon's NAME, then the other addon's name).
    -- RUMOR_COUNT lines; RUMOR_SELF when the tip is about Ratatoskr Roll itself (only when another addon asks).
    RUMOR_COUNT = "5",
    RUMOR_1 = "%s: Squeak! The eagle at the top of the World Tree wants you to know this about %s --",
    RUMOR_2 = "%s: Psst. I ran all the way down the trunk to tell you about %s --",
    RUMOR_3 = "%s: Nidhogg would hate for you to hear this about %s --",
    RUMOR_4 = "%s: Word in the branches is that %s has a trick worth knowing --",
    RUMOR_5 = "%s: Between you, me and Yggdrasil, here is one about %s --",
    RUMOR_SELF = "%s: Squeak! For once the gossip is about me --",
    RUMOR_ALONE = "%s: I ran the whole World Tree and found no other Collection addon to gossip about. Install one and ask again.",
    NO_TIPS = "%s: this REFLUX Core has no tips API; update RefluxCore to hear the gossip.",
    -- tips (REFLUX tips API, TIP_1..n): true tips about this addon, told when another addon asks for a rumour
    TIP_1 = "Target a critter, then /love, /pet or /wave at it: each one counts as an act of affection in the census.",
    TIP_2 = "/census shows this week's three most loved critters and your all-time totals.",
    TIP_3 = "The census is shared by every character on your account, so all of their affection counts together.",
    TIP_4 = "Weeks run from Monday 00:00 UTC; the first time you log in after one ends, Ratatoskr names its Critter of the Week.",
    TIP_5 = "Only critters count: emotes at players or other creatures are ignored, and so is /dance.",
    TIP_6 = "/census rumor passes on true gossip about one of your other Collection addons.",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
