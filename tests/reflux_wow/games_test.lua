-- tests/reflux_wow/games_test.lua -- the sixteen party games, each played end to end in one stubbed client with a
-- four-player party that has map positions, chat, emotes, loot and a clock. usage: lua5.1 games_test.lua <client dir>
assert(_VERSION == "Lua 5.1", "must run under Lua 5.1")
local client = assert(arg[1])

local passed, failed = 0, 0
local function eq(got, want, what)
    if got == want then passed = passed + 1
    else failed = failed + 1; print("FAIL: " .. what .. " (got " .. tostring(got) .. ", want " .. tostring(want) .. ")") end
end
local function has(s, sub, what) eq(type(s) == "string" and s:find(sub, 1, true) ~= nil, true, what .. " [" .. tostring(s):sub(1, 300) .. "]") end

local function toc_files(dir, name)
    local files = {}
    for line in io.lines(dir .. "/" .. name .. ".toc") do if line ~= "" and not line:match("^#") then files[#files + 1] = line end end
    return files
end

local GAMES = { "HuldufolkHunt", "MjolnirPass", "RuneRiddle", "OdinnSays", "AndvarisMap", "MimirsRiddle", "SleipnirSprint",
                "FreyasRunway", "AegirsCatch", "BragisVerse", "AlfarRevel", "NornsDraw", "Holmgang", "LokisLies",
                "HallTapestry", "NaglfarCrew" }
local PARTY = { "Emily", "Brokkr", "Thora", "Ulf" }
local UNIT = { player = "Emily", party1 = "Brokkr", party2 = "Thora", party3 = "Ulf" }

local W = { now = 1763000000, clock = 0, pos = {}, out = {}, said = {}, shots = 0 }
for _, n in ipairs(PARTY) do W.pos[n] = { 0.5, 0.5 } end

local env = { print = function(...)
    local t = {}
    for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
    W.out[#W.out + 1] = table.concat(t, " ")
end }
for _, k in ipairs({ "pairs", "ipairs", "type", "tostring", "tonumber", "select", "string", "table", "math", "next",
                     "error", "assert", "pcall", "unpack", "setmetatable", "getmetatable" }) do env[k] = _G[k] end
env._G = env
env.time = function() return W.now end
env.GetTime = function() return W.clock end
env.GetLocale = function() return "enUS" end
env.GetRealmName = function() return "Forever" end
env.UnitName = function(u) return UNIT[u] end
env.UnitExists = function(u) return UNIT[u] ~= nil end
env.UnitIsPlayer = function(u) return UNIT[u] ~= nil end
env.UnitClass = function() return "Paladin", "PALADIN" end
env.IsInRaid = function() return false end
env.IsInGroup = function() return true end
env.GetNumGroupMembers = function() return 4 end
env.SendChatMessage = function(text) W.said[#W.said + 1] = text end
env.C_Map = {
    GetBestMapForUnit = function() return 1420 end,
    GetPlayerMapPosition = function(_, unit)
        local p = W.pos[UNIT[unit]]
        if p then return { GetXY = function() return p[1], p[2] end } end
    end,
}
env.GetMoney = function() return 500000 end
env.LOOT_ITEM_SELF = "You receive loot: %s."
env.GetItemInfo = function(id)
    local t = { [6303] = { "Raw Slitherskin Mackerel", 1, 1 }, [6289] = { "Raw Longjaw Mud Snapper", 1, 2 },
                [6358] = { "Oily Blackmouth", 1, 16 }, [4306] = { "Silk Cloth", 1, 150 } }
    local r = t[id]
    if r then return r[1], nil, r[2], nil, nil, nil, nil, nil, nil, nil, r[3] end
end
env.IsFishingLoot = function() return W.fishing end
env.GetNumLootItems = function() return #(W.loot or {}) end
env.GetLootSlotLink = function(i) return "|Hitem:" .. W.loot[i] .. "::|h[x]|h" end
env.Screenshot = function() W.shots = W.shots + 1 end
env.DUEL_WINNER_KNOCKOUT = "%1$s has defeated %2$s in a duel"
env.DUEL_WINNER_KNOCKOUT = "%s has defeated %s in a duel"
env.RaidNotice_AddMessage = function(_, t) W.notice = t end
env.RaidWarningFrame, env.ChatTypeInfo = {}, { RAID_WARNING = {} }
env.JumpOrAscendStart = function() end
env.hooksecurefunc = function(name, fn) local o = env[name]; env[name] = function(...) local r = { o(...) }; fn(...); return unpack(r) end end
local frames = {}
env.CreateFrame = function()
    local f = { events = {}, scripts = {} }
    function f:RegisterEvent(e) self.events[e] = true end
    function f:SetScript(k, fn) self.scripts[k] = fn end
    frames[#frames + 1] = f
    return f
end
env.SlashCmdList = {}

local function fire(event, ...) for _, f in ipairs(frames) do if f.events[event] and f.scripts.OnEvent then f.scripts.OnEvent(f, event, ...) end end end
local function advance(secs)
    local steps = math.max(1, math.floor(secs * 5))
    local base = W.clock
    for i = 1, steps do
        W.clock = base + secs * i / steps
        W.now = math.floor(1763000000 + W.clock)
        for _, f in ipairs(frames) do if f.scripts.OnUpdate then f.scripts.OnUpdate(f, secs / steps) end end
    end
end
local function cmd(key, msg) W.out, W.said = {}, {}; env.SlashCmdList[key](msg or ""); return table.concat(W.out, "\n") .. "\n" .. table.concat(W.said, "\n") end
local function said() local s = table.concat(W.out, "\n") .. "\n" .. table.concat(W.said, "\n"); W.out, W.said = {}, {}; return s end
local function chat(from, text) fire("CHAT_MSG_PARTY", text, from .. "-Forever") end
local function emote(from, word) fire("CHAT_MSG_TEXT_EMOTE", from .. " " .. word .. ".", from .. "-Forever") end
local function at(name, x, y) W.pos[name] = { x, y } end

for _, a in ipairs({ "RefluxCore", "CollectionJournal" }) do
    local dir = client .. "/AddOns/" .. a; local ns = {}
    for _, file in ipairs(toc_files(dir, a)) do local c = assert(loadfile(dir .. "/" .. file)); setfenv(c, env); c(a, ns) end
end
for _, a in ipairs(GAMES) do
    local dir = client .. "/AddOns/" .. a; local ns = {}
    for _, file in ipairs(toc_files(dir, a)) do local c = assert(loadfile(dir .. "/" .. file)); setfenv(c, env); c(a, ns) end
end
for _, a in ipairs({ "RefluxCore", "CollectionJournal" }) do fire("ADDON_LOADED", a) end
for _, a in ipairs(GAMES) do fire("ADDON_LOADED", a) end
fire("PLAYER_LOGIN")
local MOD = env.RefluxCore.modules

-- 54 Huldufólk Hunt ---------------------------------------------------------------------------------------
at("Brokkr", 0.10, 0.10); at("Thora", 0.90, 0.90); at("Ulf", 0.50, 0.20)
local out = cmd("HULDUFOLKHUNT", "start 120")
has(out, "Emily is the seeker", "hunt: the head start is announced")
advance(61)
local first = said()
has(first, "ready or not", "hunt: the seeking starts after 60 s")
has(first, "Huldufólk Hunt: Freezing.", "hunt: a freezing hint when the nearest hider is far")
advance(2)
at("Emily", 0.105, 0.105); advance(2)
has(said(), "found Brokkr! 2 still hidden", "hunt: walking onto Brokkr finds him")
at("Emily", 0.50, 0.205); advance(2); at("Emily", 0.895, 0.895); advance(2)
local s = said()
has(s, "round over -- 3 of 3 found", "hunt: everyone found ends the round")
has(s, "1. Brokkr, found after", "hunt: the board lists the fastest first")
at("Emily", 0.5, 0.5)

-- 55 Mjölnir Pass -----------------------------------------------------------------------------------------
at("Brokkr", 0.20, 0.20); at("Thora", 0.80, 0.80); at("Ulf", 0.20, 0.80)
out = cmd("MJOLNIRPASS", "start 30")
local it = MOD["collection/mjolnir"].tagIt(env.MjolnirPassDB)
has(out, it .. " holds the hammer", "tag: someone holds the hammer")
local other = (it == "Brokkr") and "Thora" or "Brokkr"
local p = W.pos[it]
at(other, p[1] + 0.002, p[2]); advance(1)
has(said(), it .. " passed the hammer to " .. other, "tag: touching passes the hammer")
eq(MOD["collection/mjolnir"].tagIt(env.MjolnirPassDB), other, "tag: the new holder")
advance(1)
eq(MOD["collection/mjolnir"].tagIt(env.MjolnirPassDB), other, "tag: no tag-backs inside 3 s")
advance(30)
s = said()
has(s, "time! Seconds holding the hammer", "tag: time ends the game")
has(W.notice or "", "held Mjölnir longest", "tag: the longest holder gets the banner")
for _, n in ipairs(PARTY) do at(n, 0.5, 0.5) end

-- 56 Rune Riddle ------------------------------------------------------------------------------------------
at("Emily", 0.30, 0.30); cmd("RUNERIDDLE", "mark Where the old oak weeps")
at("Emily", 0.60, 0.60); cmd("RUNERIDDLE", "mark Under the mill wheel")
at("Emily", 0.5, 0.5); at("Brokkr", 0.5, 0.4)
out = cmd("RUNERIDDLE", "start")
has(out, "riddle 1 of 2: Where the old oak weeps", "riddle: the first riddle is read")
at("Thora", 0.302, 0.301); advance(1)
s = said()
has(s, "Thora solved it!", "riddle: Thora reaches stop 1")
has(s, "riddle 2 of 2: Under the mill wheel", "riddle: the next riddle follows")
at("Brokkr", 0.60, 0.60); advance(1)
has(said(), "Brokkr reached the last stop", "riddle: the hunt is done")
for _, n in ipairs(PARTY) do at(n, 0.5, 0.5) end

-- 71 Naglfar Crew (sardines) -------------------------------------------------------------------------------
at("Thora", 0.80, 0.20); at("Brokkr", 0.2, 0.2); at("Ulf", 0.3, 0.3); at("Emily", 0.4, 0.4)
out = cmd("NAGLFARCREW", "start Thora 300")
has(out, "Thora is the sardine", "sardines: the hider is named")
advance(61); said()
at("Brokkr", 0.801, 0.201); advance(1)
has(said(), "Brokkr squeezed into the tin (number 1)", "sardines: Brokkr squeezes in")
at("Ulf", 0.799, 0.199); advance(1)
has(said(), "Emily was left on the dock", "sardines: the last seeker out loses")
for _, n in ipairs(PARTY) do at(n, 0.5, 0.5) end

-- 57 Óðinn Says ---------------------------------------------------------------------------------------------
local WORD = { WAVE = "waves", DANCE = "bursts into dance", CHEER = "cheers", BOW = "bows", CLAP = "claps",
               SALUTE = "salutes", FLEX = "flexes", LAUGH = "laughs", KNEEL = "kneels", ROAR = "roars" }
local TOK = { "WAVE", "DANCE", "CHEER", "BOW", "CLAP", "SALUTE", "FLEX", "LAUGH", "KNEEL", "ROAR" }
out = cmd("ODINNSAYS", "start")
has(out, "3 players", "odinn: three players (the host calls)")
local rounds = 0
while env.OdinnSaysDB.round and rounds < 20 do
    advance(rounds == 0 and 8 or 2)          -- a call every 8 s, each closing 6 s after it
    local r = env.OdinnSaysDB.round
    if r and r.emote then
        local w = WORD[TOK[r.emote]]
        if r.real == 1 then
            -- Brokkr is quick, Thora second, Ulf does the wrong one in round 1
            emote("Brokkr", w)
            if rounds == 0 then emote("Ulf", "laughs" == w and "roars" or "laughs") else emote("Ulf", w) end
            emote("Thora", w)
        else
            emote("Thora", w)          -- Thora falls for every trap
        end
    end
    advance(6)
    rounds = rounds + 1
end
s = said()
eq(env.OdinnSaysDB.round, nil, "odinn: the game ends with one left")
has(s, "is the last one standing", "odinn: a winner is named")
eq(env.OdinnSaysDB.wins.Brokkr, 1, "odinn: quick, careful Brokkr wins")

-- 64 Álfar Revel -------------------------------------------------------------------------------------------
out = cmd("ALFARREVEL", "start 3 4")
for _ = 1, 3 do
    advance(4)
    local e = env.AlfarRevelDB.party and env.AlfarRevelDB.party.emote
    if e and e > 0 then emote("Brokkr", WORD[TOK[e]]); emote("Thora", WORD[TOK[e]]) end
end
emote("Thora", "roars")
advance(4); advance(4)
s = said()
has(s, "the music stops", "revel: the party ends")
has(s, "1. Brokkr -- 3 in a row", "revel: Brokkr hit every beat")
eq(env.AlfarRevelDB.records.Brokkr, 3, "revel: the record is kept")

-- 59 Mímir's Riddle ------------------------------------------------------------------------------------------
out = cmd("MIMIRSRIDDLE", "start 2")
advance(4)
local q = MOD["collection/mimir"].mimirCurrent(env.MimirsRiddleDB)
eq(q > 0, true, "mimir: a question is open")
local answers = { "orgrimmar", "ironforge", "stormwind", "teldrassil", "tauren", "undercity", "gnomeregan", "hogger",
                  "duskwood", "booty bay", "barrens", "defias", "go home", "tirisfal", "stranglethorn", "mulgore", "mining",
                  "blue", "lfg", "bronze", "horde", "shadowglen", "harvest golems", "inn", "boom" }
chat("Ulf", "no idea")
chat("Thora", answers[q]:upper() .. "!")
has(said(), "Thora got it! (1 tonight)", "mimir: a right answer, any case or punctuation, scores")
chat("Brokkr", answers[q])
eq(env.MimirsRiddleDB.alltime.Brokkr, nil, "mimir: only the first right answer scores")
advance(34); said(); advance(34)
s = said()
has(s, "that's the night", "mimir: the night ends after its questions")
eq(env.MimirsRiddleDB.alltime.Thora, 1, "mimir: the all-time board keeps Thora's point")

-- 63 Bragi's Verse -----------------------------------------------------------------------------------------
cmd("BRAGISVERSE", "duel Brokkr Thora")
chat("Brokkr", "An old silent pond / A frog jumps into the pond / splash! Silence again.")
has(said(), "Brokkr: 5-7-5 syllables, 35 points", "verse: a perfect haiku scores 35")
chat("Thora", "I like my sword lots / it is shiny / yes")
s = said()
has(s, "Brokkr wins the duel, 35 to", "verse: the better haiku wins")
out = cmd("BRAGISVERSE", "check the candle makes light / a wanted cat jumped over / boxes in the snow")
has(out, "5-7-5", "verse: /verse check counts privately")

-- 61 Freya's Runway ---------------------------------------------------------------------------------------
cmd("FREYASRUNWAY", "start Brokkr Thora")
advance(3)
has(said(), "On the runway: Brokkr", "runway: the first model walks")
chat("Thora", "9"); chat("Ulf", "7"); chat("Brokkr", "10"); chat("Ulf", "1")
advance(30)
has(said(), "Brokkr scores 8.0 from 2 judges", "runway: own votes and second votes are refused")
advance(3)
chat("Brokkr", "10"); chat("Ulf", "10")
advance(30); advance(3)
s = said()
has(s, "Thora takes Freya's crown", "runway: the best average wins")
eq(env.FreyasRunwayDB.crowns.Thora, 1, "runway: the crown is kept")

-- 60 Sleipnir Sprint ---------------------------------------------------------------------------------------
at("Emily", 0.10, 0.50); cmd("SLEIPNIRSPRINT", "start")
at("Emily", 0.90, 0.50); cmd("SLEIPNIRSPRINT", "finish")
for _, n in ipairs(PARTY) do at(n, 0.10, 0.50) end
out = cmd("SLEIPNIRSPRINT", "go 1")
advance(4.5)
has(said(), "GO!", "sprint: the start")
advance(1)
at("Thora", 0.90, 0.50); advance(0.5)
at("Brokkr", 0.90, 0.50); advance(2)
at("Ulf", 0.90, 0.50); at("Emily", 0.90, 0.50); advance(1)
s = said()
has(s, "Thora finishes in place 1", "sprint: Thora wins")
has(s, "results --", "sprint: results when all are home")
local board = MOD["collection/games"] and nil
eq(env.SleipnirSprintDB.board.Thora ~= nil, true, "sprint: the course board has Thora's time")
for _, n in ipairs(PARTY) do at(n, 0.5, 0.5) end

-- 67 Holmgang ---------------------------------------------------------------------------------------------
out = cmd("HOLMGANG", "start Brokkr Thora Ulf")
has(out, "Thora vs Ulf", "holmgang: seeds 2 and 3 meet; seed 1 has the bye")
fire("CHAT_MSG_SYSTEM", "Ulf has defeated Thora in a duel")
has(said(), "Brokkr vs Ulf", "holmgang: the duel line advances Ulf to the final")
fire("CHAT_MSG_SYSTEM", "Brokkr has defeated Ulf in a duel")
has(said(), "Brokkr is the champion", "holmgang: a champion")
eq(env.HolmgangDB.titles.Brokkr, 1, "holmgang: the title is kept")

-- 66 Norns' Draw --------------------------------------------------------------------------------------------
out = cmd("NORNSDRAW", "new")
has(out, "[x]FREE", "bingo: the centre is free")
local card = env.NornsDrawDB.card
env.JumpOrAscendStart()
local jumpsq = nil
for sq = 1, 25 do if card.squares[sq] == 1 then jumpsq = sq end end
if jumpsq then eq(card.marked[jumpsq], 1, "bingo: jumping marks the jump square") else passed = passed + 1 end
local N = MOD["collection/norns"]
local result = 0
for ev = 1, 30 do if result ~= 2 then result = N.nornsMark_(env.NornsDrawDB, ev, W.now) end end
eq(result, 2, "bingo: marking events reaches a BINGO")

-- 62 Ægir's Catch -------------------------------------------------------------------------------------------
cmd("AEGIRSCATCH", "start 5")
W.fishing, W.loot = true, { 6303 }
fire("LOOT_OPENED")
W.loot = { 6358 }
fire("LOOT_OPENED")
W.fishing = false
fire("CHAT_MSG_LOOT", "Thora receives loot: |cffffffff|Hitem:6303::|h[Raw Slitherskin Mackerel]|h|r.", "Thora-Forever")
fire("CHAT_MSG_LOOT", "Ulf receives loot: |cffffffff|Hitem:4306::|h[Silk Cloth]|h|r.", "Ulf-Forever")
advance(301)
s = said()
has(s, "3 catches", "derby: two own catches and Thora's known fish; Ulf's silk is not a catch")
has(s, "Biggest: Emily with Oily Blackmouth", "derby: the most valuable catch is biggest")
has(s, "Weirdest: Emily with Oily Blackmouth", "derby: the rarest (higher quality on a tie) is weirdest")

-- 58 Andvari's Map -------------------------------------------------------------------------------------------
at("Emily", 0.82, 0.18)
cmd("ANDVARISMAP", "mark")
out = cmd("ANDVARISMAP", "map")
has(out, "X", "treasure: the chart has its X")
has(out, "Clue: X lies near the edge of the land, to the north-east.", "treasure: the clue says where")
at("Emily", 0.5, 0.5); advance(3)
eq(env.AndvarisMapDB.marks[1].found, nil, "treasure: not found from the middle")
at("Emily", 0.821, 0.181); advance(3)
has(said(), "you found treasure number 1", "treasure: standing on it finds it")
at("Emily", 0.5, 0.5)

-- 68 Loki's Lies ----------------------------------------------------------------------------------------------
advance(10)
out = cmd("LOKISLIES")
has(out, "Loki's Lies: ", "rumour: a headline")
eq(out:find("%%[sd]") == nil, true, "rumour: every placeholder filled [" .. out:sub(1, 200) .. "]")
out = cmd("LOKISLIES", "share")
eq(#W.said > 0, true, "rumour: /rumour share says it to the group")

-- 69 Hall Tapestry ------------------------------------------------------------------------------------------
out = cmd("HALLTAPESTRY", "2")
has(out, "2 shots planned", "tapestry: a plan")
advance(30)
s = said()
has(s, "Shot 1 of 2", "tapestry: shot 1 called")
has(s, "that's a wrap -- 2 shots", "tapestry: both shots taken")
eq(W.shots, 2, "tapestry: two screenshots")

-- every game wrote REFLUX rows, in its own block --------------------------------------------------------------
advance(10)
local j, blocks = env.CollectionJournalDB, {}
for seq = j.firstSeq, j.nextSeq - 1 do local t = j.rows[seq].type; blocks[math.floor((t - 2000) / 10) + 1] = true end
for _, n in ipairs({ 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68, 69, 71 }) do
    eq(blocks[n], true, "addon " .. n .. " journalled REFLUX rows in its block")
end

print(string.format("games: %d passed, %d failed", passed, failed))
os.exit(failed == 0 and 0 or 1)
