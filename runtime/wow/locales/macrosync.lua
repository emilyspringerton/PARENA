-- runtime/wow/locales/macrosync.lua -- MacroSync's catalogue. Data, not code; enUS fallback (see hrungnir.lua).
-- H_<moment>_<n> are haiku, three lines split by "/" (the spec: "chat lines and errors print as haiku"); every
-- enUS one is checked 5-7-5 by Bragi's Verse's syllable counter in the test. Moments: 1 ascended, 2 pruned,
-- 3 spells not learned yet (the macros are written anyway), 4 name collision, 5 in combat, 6 no macro API, 7 slots full, 8 no set for this class, 9 too long.
local _, ns = ...
local enUS = {
    HELP = "/firstaid -- what the universal FirstAid macro holds for you and why   /ascend firstaid off|on -- stop or resume its updates   /ascend -- load your kit (your class's macros, or the kit planned in the forge), prune removed ones, and put them on your action bars   /ascend preview -- see what it would do   /ascend bars off|on -- leave the action bars alone / fill them again",
    BARS = "put on the action bars (%d): ",
    BARS_WOULD = "would put on the action bars (%d): ",
    BARS_TAKEN = "not placed -- the planned slot holds something of yours: ",
    BARS_FULL = "not placed -- no empty action slot left: ",
    BARS_HIDDEN = "some went on the bottom action bars, which are switched off: turn them on in Options > Action Bars.",
    BARS_OFF = "MacroSync will leave your action bars alone. /ascend bars on to fill them again.",
    BARS_ON = "MacroSync will put your macros on empty action slots at the next /ascend.",
    SUMMARY = "%s: %d in the set -- %d written, %d pruned; %d use spells you have not learned yet (they work once you do).",
    PREVIEW = "%s (preview, nothing changed): would write %d, prune %d; %d use spells not learned yet.",
    PRUNED = "pruned: ", DORMANT = "not learned yet (already on your list, they work once learned): ", COLLIDED = "left alone (your own macro has the name): ",
    FULL = "no slot free for: ", TOOLONG = "the client shortened: ",
    KIT = "%s: this character's kit was planned in the forge (%d changes to the class set).",
    FA_WRITTEN = "FirstAid is up to date for this character (an account macro: every character's FirstAid follows who is logged in).",
    FA_COLLIDED = "FirstAid: you already have a macro called FirstAid of your own -- left alone. Rename it to let MacroSync make its own.",
    FA_FULL = "FirstAid: no account macro slot free.",
    FA_COMBAT = "FirstAid will update when the fight ends (macros cannot change in combat).",
    FA_BANDAGE = "FirstAid: %s (you carry %d, heals %d) on your mouseover friend, else your friendly target, else you. First Aid: %s.",
    FA_BANDAGE_LOCKED = "FirstAid: you carry %s but it needs First Aid %d (you have %s) -- no bandage you can use yet.",
    FA_NO_BANDAGE = "FirstAid: no bandages in your bags.",
    FA_SKILL_UP = "you also carry %s: it needs First Aid %d -- train up and FirstAid switches to it by itself.",
    FA_SKILL_UNKNOWN = "unknown",
    FA_POTION_SHIFT = "Shift-click: %s (you carry %d).",
    FA_POTION = "FirstAid drinks %s instead (you carry %d).",
    FA_RECENT = "You are Recently Bandaged (%d s): a press bandages a friend you mouse over or target, and on you drinks the potion. It switches back when the debuff ends.",
    FA_RECENT_NOPOT = "You are Recently Bandaged (%d s): a press still bandages a friend; on you the game will refuse it until then.",
    FA_OPENS = "FirstAid opens First Aid, so you can make bandages.",
    FA_OFF = "FirstAid will no longer update by itself. /ascend firstaid on to turn it back on.",
    FA_ON = "FirstAid will follow your bags, skill and level again.",
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
-- The First Aid skill line's name in each client language (GetSkillLineInfo returns it translated); the first is also
-- the spell FirstAid casts when there is nothing to use.
ns.recently_bandaged_names = { "Recently Bandaged", "Kürzlich bandagiert", "Récemment bandé", "Vendado recientemente",
                               "Recentemente enfaixado", "Bendato di recente", "Недавняя перевязка", "최근에 붕대를 감음", "近期包扎过" }
ns.first_aid_names = { "First Aid", "Erste Hilfe", "Secourisme", "Primeros auxilios", "Primeiros Socorros", "Primo Soccorso",
                       "Первая помощь", "응급치료", "急救" }
