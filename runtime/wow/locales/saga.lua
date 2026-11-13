-- runtime/wow/locales/saga.lua -- Saga of Fools' catalogue (spec addon 73). Data, not code; see hrungnir.lua for
-- the fallback rule. Tn / Dn are the title and description of achievement n (stdlib/collection/saga.prn).
local _, ns = ...
local enUS = {
    NAME = "Saga of Fools",
    EARNED = "%s: Achievement (sort of) earned -- %s: %s",
    BANNER = "Achievement (sort of): %s",
    LIST_HEAD = "%s: %d of %d earned --",
    ROW_DONE = "  [x] %s -- %s",
    ROW_TODO = "  [ ] %s -- %s (%d/%d)",
    NO_HOOK = "  (this client has no %s to listen to, so that counter cannot move here)",
    T1 = "Bunny Hop Hobbyist", D1 = "Jump 100 times.",
    T2 = "Leap of Faith", D2 = "Jump 1,000 times. Nothing was gained.",
    T3 = "Sir Jumps-a-Lot", D3 = "Jump 10,000 times. Your knees file a complaint.",
    T4 = "Floor Inspector", D4 = "Die 10 times. Thorough work.",
    T5 = "Frequent Flier to the Spirit Healer", D5 = "Die 100 times. She knows your name now.",
    T6 = "Pen Pal of One", D6 = "Open 10 mails from yourself.",
    T7 = "Return to Sender", D7 = "Open 50 mails from yourself.",
    T8 = "Dance Like Nobody Is Watching", D8 = "/dance 25 times. Everybody was watching.",
    T9 = "Dance Floor Fixture", D9 = "/dance 250 times.",
    T10 = "Professional Sitter", D10 = "/sit 50 times. Excellent posture.",
    T11 = "Broke as a Joke", D11 = "Log in with less than one silver to your name.",
    T12 = "Rock Steady", D12 = "Raise Hrungnir, your pet rock, to level 10.",
    T13 = "Critter Whisperer", D13 = "Show affection to 25 different critters.",
    T14 = "Destiny Shopper", D14 = "Open 30 fortunes from Völva.",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
enGB.D10 = "/sit 50 times. Excellent posture, old bean."
ns.catalogue = { enUS = enUS, enGB = enGB }
