-- runtime/wow/locales/ecowar.lua -- EcoWar's catalogue. Data, not code; enUS fallback (see hrungnir.lua).
local _, ns = ...
local enUS = {
    NAME = "EcoWar",
    HELP = "/ecowar new <hvh|ava|avh|tri> <cull|harvest|ecowar> <minutes> <Team:H|A> <Team:H|A> [<Team:H|A>] [at <name>]" ..
           "   /ecowar status | stop | code | enter <code> | table",
    MODE_1 = "Horde vs Horde", MODE_2 = "Alliance vs Alliance", MODE_3 = "Alliance vs Horde", MODE_4 = "three teams, two from one side",
    GAME_1 = "Cull (critter kills)", GAME_2 = "Harvest (loot value)", GAME_3 = "EcoWar (kills and loot)",
    BADTEAMS = "Those teams do not fit %s -- check the H/A sides and the number of teams.",
    START = "%s: %s, %s, %d minutes. Go!",
    TEAM = "  Team %d: %s (%s) %s",
    REMOTE = "-- plays from the other faction; their captain sends the score",
    KILL = "[%s] %s: %s, +%d",
    STANDING = "  %d. %s -- %d points (%d critters, %dg of loot)",
    OVER = "%s: time! Final standings --",
    FINAL = "  %d. %s -- %d points (rating %d)",
    WINNER = "%s: %s win the match!",
    TIE = "A tie at %d points!",
    NOMATCH = "%s: no match is running.",
    CODE = "Match code for %s (give it to the other captain): %s",
    BADCODE = "That match code does not check out.",
    CODEREFUSED = "That code does not fit this match (wrong team, a team of yours, or a lower score than we have).",
    CODEOK = "%s's score accepted: %d.",
    TABLE = "%s league --",
    TABLEROW = "  %d. %s  P%d W%d D%d L%d  %d pts  rating %d",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
