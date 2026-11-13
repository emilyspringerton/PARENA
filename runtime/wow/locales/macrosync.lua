-- runtime/wow/locales/macrosync.lua -- MacroSync's catalogue. Data, not code; enUS fallback (see hrungnir.lua).
-- H_<moment>_<n> are haiku, three lines split by "/" (the spec: "chat lines and errors print as haiku"); every
-- enUS one is checked 5-7-5 by Bragi's Verse's syllable counter in the test. Moments: 1 ascended, 2 pruned,
-- 3 dormant, 4 name collision, 5 in combat, 6 no macro API, 7 slots full, 8 no set for this class, 9 too long.
local _, ns = ...
local enUS = {
    HELP = "/ascend -- load your class's macros, prune removed ones   /ascend preview -- see what it would do",
    SUMMARY = "%s: %d in the set -- %d written, %d pruned, %d waiting for spells.",
    PREVIEW = "%s (preview, nothing changed): would write %d, prune %d; %d wait for spells.",
    PRUNED = "pruned: ", DORMANT = "waiting (learn the spell, then /ascend): ", COLLIDED = "left alone (your own macro has the name): ",
    FULL = "no slot free for: ", TOOLONG = "the client shortened: ",
    H_1_1 = "The Flame wakes again / your macros rise from the ash / ready in your hands",
    H_1_2 = "We ascend as one / each spell falls into its place / the bar remembers",
    H_1_3 = "Commander, it's done / the old words are written fresh / go and fight again",
    H_2_1 = "What was cast away / returns now to the silence / the list is lighter",
    H_2_2 = "Old macros, let go / no one carries them further / the Flame lets them rest",
    H_3_1 = "Some spells wait asleep / you have not learned them yet, child / come back when you have",
    H_3_2 = "Not each word will wake / a spell you do not yet know / keeps its macro dark",
    H_4_1 = "A name not our own / already lives in that slot / we will not touch it",
    H_5_1 = "Steel is in the air / no macro may change in war / ascend after it",
    H_5_2 = "The blade is still drawn / the Flame will not move in fights / wait for the calm now",
    H_6_1 = "This world has no hands / to write macros in your book / the Flame cannot reach",
    H_7_1 = "All the slots are full / there is no room for the rest / clear some, then ascend",
    H_8_1 = "Your class has no set / the Flame holds no words for you / not yet, but it will",
    H_9_1 = "The words were too long / the book cut them where they stood / shorter, then ascend",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
