-- runtime/wow/locales/norns.lua -- Norns' Draw's catalogue (spec addon 66). Data; enUS fallback. E<n> is the event's
-- name, S<n> its short label on the card (6 characters or fewer).
local _, ns = ...
local enUS = {
    NAME = "Norns' Draw",
    HELP = "/bingo -- your card   /bingo new -- deal a fresh one",
    DEALT = "%s: a fresh card. The Norns are watching.",
    FREE = "FREE",
    LINES = "Lines complete: %d",
    BINGO = "%s: BINGO!",
    BINGO_SAY = "%s: BINGO for %s! (the last square: %s)",
    E1 = "you jump", S1 = "jump", E2 = "someone dances", S2 = "dance", E3 = "someone waves", S3 = "wave",
    E4 = "someone cheers", S4 = "cheer", E5 = "someone roars", S5 = "roar", E6 = "someone laughs", S6 = "laugh",
    E7 = "a rogue appears", S7 = "rogue", E8 = "a mage appears", S8 = "mage", E9 = "a warlock appears", S9 = "lock",
    E10 = "a paladin appears", S10 = "pally", E11 = "a hunter appears", S11 = "huntr", E12 = "a druid appears", S12 = "druid",
    E13 = "a priest appears", S13 = "priest", E14 = "a gnome appears", S14 = "gnome", E15 = "a dwarf appears", S15 = "dwarf",
    E16 = "a tauren appears", S16 = "tauren", E17 = "an undead appears", S17 = "undead", E18 = "a troll appears", S18 = "troll",
    E19 = "a night elf appears", S19 = "nelf", E20 = "an orc appears", S20 = "orc",
    E21 = "you loot something grey", S21 = "grey", E22 = "you loot something green", S22 = "green",
    E23 = "a quest is turned in", S23 = "quest", E24 = "you die", S24 = "dead", E25 = "you level up", S25 = "ding",
    E26 = "mail arrives", S26 = "mail", E27 = "you enter combat", S27 = "fight", E28 = "you target a critter", S28 = "critr",
    E29 = "someone says lol", S29 = "lol", E30 = "your gold goes up", S30 = "gold",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
