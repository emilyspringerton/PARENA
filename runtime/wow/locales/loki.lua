-- runtime/wow/locales/loki.lua -- Loki's Lies' catalogue (spec addon 68). Data; enUS fallback.
-- R<type>_<n> templates take, in order: the player's class (%s), the row's main number (%d: gold in whole gold for
-- 2011, else the row's a), and the row's b (%d) -- a template may leave the later ones out.
local _, ns = ...
local enUS = {
    NAME = "Loki's Lies",
    HEADLINE = "%s: %s",
    SOMEONE = "Adventurer",
    QUIET = "Slow news day. Loki is making something up as we speak.",
    R2011_1 = "Local %s's purse moves by %d gold; shiny hat sales suspected.",
    R2011_2 = "BREAKING: %s spotted counting %d gold twice, just to be sure.",
    R2011_3 = "Economists alarmed as %s shifts %d gold without consulting anyone.",
    R2011_4 = "%s denies all knowledge of the %d missing gold. Witnesses mention a vendor.",
    R2010_1 = "Hoarding crisis: %s seen with item %d, again.",
    R2010_2 = "Bags of %s reportedly 'full of rocks'. Item %d implicated.",
    R2010_3 = "Sources say %s will never, ever sell item %d.",
    R2692_1 = "%s dies again (death number %d); the floor issues a statement.",
    R2692_2 = "Spirit Healer to name a bench after %s (death number %d).",
    R2692_3 = "%s 'just resting', insists the corpse, %d times now.",
    R2691_1 = "%s's pet rock reaches level %d and takes all the credit.",
    R2691_2 = "%s's rock, now level %d, considers a career in politics.",
    R2640_1 = "Wildlife groups praise %s for unusual kindness to critters.",
    R2640_2 = "Critters form a fan club for %s. Membership: one squirrel.",
    R2640_3 = "%s caught waving at a toad. Toad unavailable for comment.",
    R2720_1 = "%s earns joke achievement %d; nobody is surprised.",
    R2720_2 = "Bards compose a short, silly song about %s and achievement %d.",
    R2710_1 = "Seeress reads the fortune of a %s again; nobody pretends to be surprised.",
    R2710_2 = "Fortune cookie futures soar after %s cracks another.",
    R2591_1 = "%s finishes a race in place %d and demands a recount.",
    R2591_2 = "Sleipnir himself impressed by %s's place-%d finish. Allegedly.",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
