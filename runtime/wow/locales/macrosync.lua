-- runtime/wow/locales/macrosync.lua -- MacroSync's catalogue. Data, not code; enUS fallback (see hrungnir.lua).
-- H_<moment>_<n> are haiku, three lines split by "/" (the spec: "chat lines and errors print as haiku"); every
-- enUS one is checked 5-7-5 by Bragi's Verse's syllable counter in the test. Moments: 1 ascended, 2 pruned,
-- 3 spells not learned yet (the macros are written anyway), 4 name collision, 5 in combat, 6 no macro API, 7 slots full, 8 no set for this class, 9 too long.
local _, ns = ...
local enUS = {
    HELP = "/ascend -- load your kit (your class's macros, or the kit planned in the forge), prune removed ones   /ascend preview -- see what it would do",
    SUMMARY = "%s: %d in the set -- %d written, %d pruned; %d use spells you have not learned yet (they work once you do).",
    PREVIEW = "%s (preview, nothing changed): would write %d, prune %d; %d use spells not learned yet.",
    PRUNED = "pruned: ", DORMANT = "not learned yet (already on your list, they work once learned): ", COLLIDED = "left alone (your own macro has the name): ",
    FULL = "no slot free for: ", TOOLONG = "the client shortened: ",
    KIT = "%s: this character's kit was planned in the forge (%d changes to the class set).",
    H_1_1 = "The Flame wakes again / your macros rise from the ash / ready in your hands",
    H_1_2 = "We ascend as one / each spell falls into its place / the bar remembers",
    H_1_3 = "Commander, it's done / the old words are written fresh / go and fight again",
    H_2_1 = "What was cast away / returns now to the silence / the list is lighter",
    H_2_2 = "Old macros, let go / no one carries them further / the Flame lets them rest",
    H_3_1 = "Some spells wait asleep / you have not learned them yet, child / they wake when you learn",
    H_3_2 = "Your hand is not taught / the macro waits on your bar / it wakes when you learn",
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
