-- runtime/wow/locales/volva.lua -- Völva's catalogue (spec addon 72). Data, not code; see hrungnir.lua for the
-- fallback rule. PLAIN_n are the plain fortunes (PLAIN_COUNT of them); each STAT_* line takes one value.
local _, ns = ...
local enUS = {
    NAME = "Völva",
    OPENED = "%s cracks a cookie: \"%s\"",
    LUCKY = "  Lucky numbers: %s",
    NONE_YET = "%s: no fortune opened yet. Log in again and one will be waiting.",
    PLAIN_COUNT = "20",
    PLAIN_1 = "A short journey brings you to a vendor you forgot existed.",
    PLAIN_2 = "The next quest you abandon will remember you.",
    PLAIN_3 = "Beware of murlocs bearing gifts. Beware of murlocs generally.",
    PLAIN_4 = "Today the loot will be grey, but the company will be golden.",
    PLAIN_5 = "Someone is thinking of you. It is the innkeeper. You owe them for a room.",
    PLAIN_6 = "You will find what you seek in the last bag slot you check.",
    PLAIN_7 = "A great fall awaits you. Mind the bridge.",
    PLAIN_8 = "The Norns have woven your thread with extra sparkles.",
    PLAIN_9 = "Patience: the rare spawns for those who wait. Just not for you, today.",
    PLAIN_10 = "Your hearthstone knows the way home, even when you do not.",
    PLAIN_11 = "An old friend will mail you something useless and precious.",
    PLAIN_12 = "Fortune favours the bold. Also the well-geared.",
    PLAIN_13 = "Do not trust a quest giver with punctuation for a face.",
    PLAIN_14 = "You will dance in a city square, and it will be glorious.",
    PLAIN_15 = "A critter nearby needs a hug. You know the one.",
    PLAIN_16 = "The auction house giveth, the auction house undercutteth.",
    PLAIN_17 = "Your next level is closer than your last one was.",
    PLAIN_18 = "Rest well tonight; the rested bar is a kind of prophecy.",
    PLAIN_19 = "Something shiny is in your future. Probably a pebble.",
    PLAIN_20 = "The seeress sees all, but she will not tell you where you left your mount.",
    STAT_GOLD = "Your purse holds %s. The Norns advise: buy a hat.",
    STAT_LEVEL = "At level %d, the threads of your fate are still loosely spun.",
    STAT_NET = "%d items rest across your whole account. A dragon would be proud.",
    STAT_ROCK = "Your rock is level %d. It believes in you, quietly.",
    STAT_CRITTERS = "%d critters have felt your affection. They speak of you in the hedgerows.",
    STAT_OPENED = "This is fortune number %d. The seeress needs a nap.",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
enGB.PLAIN_12 = "Fortune favours the bold. Also the well-armoured."
ns.catalogue = { enUS = enUS, enGB = enGB }
