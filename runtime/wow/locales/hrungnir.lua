-- runtime/wow/locales/hrungnir.lua -- Hrungnir's catalogue (spec addon 70). Data, not code: one table per locale,
-- every key in enUS. The other ten spec locales are the translators' (spec: "translators supply catalogues");
-- until theirs arrive, a missing key falls back to enUS and the packaged missing-string check lists it.
local _, ns = ...
local enUS = {
    NAME = "Hrungnir",
    FACE_1 = "[o_o]", FACE_2 = "[._.]", FACE_3 = "[-_-]",
    LEVEL = "Lv %d",
    SAYS = "%s says: %s",
    STATUS = "%s, a level %d rock. %d pebble points (%d to the next level). Patted %d times. You have died %d times; it has not.",
    MOOD_1 = "It seems content.", MOOD_2 = "It looks lonely. A pat would help.", MOOD_3 = "It looks smug.",
    HELP = "/rock -- status   /rock pat -- pat it   /rock hide, /rock show -- the little frame",
    HIDDEN = "Hrungnir is in your pocket now. /rock show brings it back.",
    LEVEL_UP = "%s is now level %d!",
    -- 1 levelled
    C1_1 = "I have grown. Imperceptibly. As rocks do.",
    C1_2 = "Another level. I did all the hard work of sitting here.",
    C1_3 = "Level up! I feel slightly more rock.",
    C1_4 = "My ancestors were mountains. I am getting there.",
    C1_5 = "Do not make a fuss. (Make a small fuss.)",
    -- 2 patted
    C2_1 = "...", C2_2 = "That was acceptable.", C2_3 = "I felt that. Geologically.",
    C2_4 = "Pat logged. Gratitude pending erosion.", C2_5 = "Again in a minute, if you must.",
    -- 3 patted too soon
    C3_1 = "Patience. I am a rock.", C3_2 = "One pat a minute. I have standards.", C3_3 = "You are wearing me smooth.",
    -- 4 you died
    C4_1 = "I did not die. Just saying.", C4_2 = "Have you tried being a rock?",
    C4_3 = "Lie still. It is what I do best.", C4_4 = "Floor-level perspective. Welcome.",
    C4_5 = "I would have helped, but I am a rock.",
    -- 5 idle remarks
    C5_1 = "Still here.", C5_2 = "I have been thinking about gravel. Fondly.",
    C5_3 = "Is that a mountain? Do not introduce us.", C5_4 = "I count the seconds. There have been many.",
    C5_5 = "Your bags are heavy. I approve of heavy.", C5_6 = "Somewhere a pebble is having an adventure. Not me.",
    C5_7 = "I am not ignoring you. I am being still at you.", C5_8 = "Fun fact: I am mostly rock.",
    -- 6 hello on login
    C6_1 = "Oh. You are back.", C6_2 = "I kept your spot warm. No I did not, I am a rock.",
    C6_3 = "Hello. I did nothing while you were away. Excellent.", C6_4 = "Ready when you are. I am always ready. I am a rock.",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
