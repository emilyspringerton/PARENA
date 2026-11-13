-- runtime/wow/locales/mimir.lua -- Mímir's Riddle's catalogue (spec addon 59). Data; enUS fallback.
-- Q<n> is a question, A<n> its accepted answers split by "|" (matched lower case, punctuation ignored).
-- General Azeroth lore of the original continents only, so Forever's own changes do not make an answer wrong;
-- the founder's quest guides are the place to add Forever-specific questions.
local _, ns = ...
local enUS = {
    NAME = "Mímir's Riddle",
    HELP = "/mimir start [questions] [guild] -- trivia in group (or guild) chat, 30 s each   /mimir stop | scores",
    START = "%s: trivia night! %d questions. First right answer in chat scores.",
    ASK = "Question %d of %d: %s",
    RIGHT = "%s got it! (%d tonight)",
    NOBODY = "Time! The answer was: %s",
    OVER = "%s: that's the night --",
    ROW = "  %d. %s -- %d",
    Q_COUNT = "25",
    Q1 = "What is the capital city of the orcs in Durotar?", A1 = "Orgrimmar|org",
    Q2 = "Which city do the dwarves of Khaz Modan call home?", A2 = "Ironforge|if",
    Q3 = "What is the name of the human capital in Elwynn Forest?", A3 = "Stormwind|stormwind city|sw",
    Q4 = "The night elves' capital sits in which giant tree?", A4 = "Teldrassil",
    Q5 = "Thunder Bluff is the home of which race?", A5 = "Tauren|the tauren",
    Q6 = "Under which city do the Forsaken make their home?", A6 = "Lordaeron|Undercity",
    Q7 = "The Gnomes' capital, lost to trogg and radiation, is called?", A7 = "Gnomeregan",
    Q8 = "Which gnoll with a bounty on his head prowls Elwynn and Westfall's border?", A8 = "Hogger",
    Q9 = "Which shadowy zone of wolves, spiders and worgen surrounds the town of Darkshire?", A9 = "Duskwood",
    Q10 = "Which goblin port town sits on the southern tip of Stranglethorn Vale?", A10 = "Booty Bay",
    Q11 = "What is the name of the Horde zone of rolling plains around Crossroads?", A11 = "The Barrens|Barrens",
    Q12 = "Which brotherhood of bandits hides in the Deadmines?", A12 = "Defias|The Defias Brotherhood|Defias Brotherhood",
    Q13 = "What is the hearthstone's job?", A13 = "Teleport home|return home|go home|teleport to inn|home",
    Q14 = "Which zone is home to the Scarlet Monastery?", A14 = "Tirisfal Glades|Tirisfal",
    Q15 = "What is the name of the troll-filled jungle between Duskwood and Booty Bay?", A15 = "Stranglethorn Vale|Stranglethorn|STV",
    Q16 = "Which zone holds the tauren's starting village of Camp Narache?", A16 = "Mulgore",
    Q17 = "What profession turns ore into bars?", A17 = "Mining|Smelting",
    Q18 = "How many quality colours sit below epic: grey, white, green, and what?", A18 = "Blue|Rare",
    Q19 = "What do you call a character waiting for a group outside an instance, in three letters?", A19 = "LFG",
    Q20 = "Which dragonflight guards time itself?", A20 = "Bronze|the bronze dragonflight|bronze dragonflight",
    Q21 = "Which faction's banner is red with a black emblem: Alliance or Horde?", A21 = "Horde",
    Q22 = "What is the starting zone of night elves called?", A22 = "Teldrassil|Shadowglen",
    Q23 = "Westfall's farms are threatened by what kind of walking machines?", A23 = "Harvest Golems|Harvest Golem|Golems",
    Q24 = "What is a rested bonus earned by: logging out in an inn, or running around?", A24 = "Inn|Logging out in an inn|resting",
    Q25 = "Which three-letter word does every gnome engineer fear from a failed gadget?", A25 = "Boom|Kaboom",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
