-- tests/reflux_wow/ecowar_test.lua -- EcoWar in the packaged client, under real Lua 5.1: a captain and players in
-- stubbed clients that pass addon messages to each other, every mode, both carriers, the league and the ratings.
-- usage: lua5.1 ecowar_test.lua <client dir>
assert(_VERSION == "Lua 5.1", "must run under Lua 5.1")
local client = assert(arg[1])

local passed, failed = 0, 0
local function eq(got, want, what)
    if got == want then passed = passed + 1
    else failed = failed + 1; print("FAIL: " .. what .. " (got " .. tostring(got) .. ", want " .. tostring(want) .. ")") end
end
local function has(s, sub, what) eq(type(s) == "string" and s:find(sub, 1, true) ~= nil, true, what .. " [" .. tostring(s) .. "]") end

local function toc_files(dir, name)
    local files = {}
    for line in io.lines(dir .. "/" .. name .. ".toc") do if line ~= "" and not line:match("^#") then files[#files + 1] = line end end
    return files
end

-- the shared world: one clock, one raid roster, addon messages on the wire
local W = { now = 1763000000, clock = 0, raid = {}, wire = {} }

local function session(me, faction, saved)
    local s = { out = {}, said = {}, sent = {}, target = nil, cleu = nil, saved = saved or {} }
    local env = { print = function(...)
        local t = {}
        for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
        s.out[#s.out + 1] = table.concat(t, " ")
    end }
    for _, k in ipairs({ "pairs", "ipairs", "type", "tostring", "tonumber", "select", "string", "table", "math",
                         "next", "error", "assert", "pcall", "unpack", "setmetatable", "getmetatable" }) do env[k] = _G[k] end
    env._G = env
    env.time = function() return W.now end
    env.GetTime = function() return W.clock end
    env.GetLocale = function() return "enUS" end
    env.GetRealmName = function() return "Forever" end
    env.UnitFactionGroup = function() return faction end
    env.UnitName = function(u)
        if u == "player" then return me end
        if u == "target" and s.target then return s.target.name end
        local i = tonumber((u or ""):match("^raid(%d+)$")); if i and W.raid[i] then return W.raid[i][1] end
    end
    env.UnitGUID = function(u) if u == "player" then return "Player-" .. me end if u == "target" and s.target then return s.target.guid end end
    env.UnitExists = function(u) return u == "player" or (u == "target" and s.target ~= nil) end
    env.UnitCreatureType = function(u) return u == "target" and s.target and s.target.ctype or nil end
    env.IsInRaid = function() return #W.raid > 0 end
    env.IsInGroup = function() return #W.raid > 0 end
    env.GetNumGroupMembers = function() return #W.raid end
    env.GetRaidRosterInfo = function(i) local r = W.raid[i]; if r then return r[1], 0, r[2] end end
    env.SendChatMessage = function(text) s.said[#s.said + 1] = text end
    env.LOOT_ITEM_SELF = "You receive loot: %s."
    env.GetItemInfo = function(id) local p = ({ [2589] = 300, [4306] = 600 })[id] or 0; return "Item" .. id, nil, 1, nil, nil, nil, nil, nil, nil, nil, p end
    env.CombatLogGetCurrentEventInfo = function() return unpack(s.cleu) end
    env.C_ChatInfo = {
        RegisterAddonMessagePrefix = function() return true end,
        SendAddonMessage = function(prefix, msg, kind, target) s.sent[#s.sent + 1] = { prefix, msg, kind, target }
            W.wire[#W.wire + 1] = { from = me, prefix = prefix, msg = msg, kind = kind } end,
    }
    env.JoinTemporaryChannel = function(name) s.channel = name end
    env.GetChannelName = function(name) return (s.channel == name) and 5 or 0 end
    env.LeaveChannelByName = function(name) if s.channel == name then s.channel = nil end end
    env.C_Map = { GetBestMapForUnit = function() return 1420 end, GetPlayerMapPosition = function() return { GetXY = function() return 0.5, 0.5 end } end }
    env.RaidNotice_AddMessage = function(_, t) s.notice = t end
    env.RaidWarningFrame, env.ChatTypeInfo = {}, { RAID_WARNING = {} }
    local frames = {}
    env.CreateFrame = function()
        local f = { events = {}, scripts = {} }
        function f:RegisterEvent(e) self.events[e] = true end
        function f:SetScript(k, fn) self.scripts[k] = fn end
        frames[#frames + 1] = f
        return f
    end
    env.SlashCmdList = {}
    for k, v in pairs(s.saved) do env[k] = v end
    s.env = env
    function s.fire(event, ...) for _, f in ipairs(frames) do if f.events[event] and f.scripts.OnEvent then f.scripts.OnEvent(f, event, ...) end end end
    function s.tick(dt) for _, f in ipairs(frames) do if f.scripts.OnUpdate then f.scripts.OnUpdate(f, dt or 0) end end end
    function s.cmd(msg) s.out, s.said = {}, {}; env.SlashCmdList.ECOWAR(msg); return table.concat(s.out, "\n") .. "\n" .. table.concat(s.said, "\n") end
    function s.see_critter(name, guid) s.target = { name = name, guid = guid, ctype = "Critter" }; s.fire("PLAYER_TARGET_CHANGED") end
    function s.kill(killer, guid) s.cleu = { 0, "PARTY_KILL", false, "Player-" .. killer, killer, 0, 0, guid }; s.fire("COMBAT_LOG_EVENT_UNFILTERED") end
    function s.loot(id, n) s.fire("CHAT_MSG_LOOT", "You receive loot: |cffffffff|Hitem:" .. id .. "::|h[Item]|h|r" .. (n > 1 and ("x" .. n) or "") .. ".") end
    function s.hear(from, prefix, msg, kind) s.fire("CHAT_MSG_ADDON", prefix, msg, kind, from .. "-Forever") end
    for _, a in ipairs({ "RefluxCore", "CollectionJournal", "EcoWar" }) do
        local dir = client .. "/AddOns/" .. a
        local ns = {}
        for _, file in ipairs(toc_files(dir, a)) do local c = assert(loadfile(dir .. "/" .. file)); setfenv(c, env); c(a, ns) end
    end
    for _, a in ipairs({ "RefluxCore", "CollectionJournal", "EcoWar" }) do s.fire("ADDON_LOADED", a) end
    s.fire("PLAYER_LOGIN")
    return s
end

-- deliver everything on the wire from `from` to session s
local function deliver(s, from)
    for _, m in ipairs(W.wire) do if m.from == from then s.hear(m.from, m.prefix, m.msg, m.kind) end end
end

-- ===== Horde vs Horde, EcoWar game, in one raid ======================================================
W.raid = { { "Emily", 1 }, { "Brokkr", 1 }, { "Thora", 2 }, { "Ulf", 2 } }
local cap = session("Emily", "Horde")
local out = cap.cmd("new ava ecowar 10 Wolves:H Bears:H")
has(out, "do not fit Alliance vs Alliance", "Horde teams do not fit Alliance vs Alliance")
out = cap.cmd("new hvh ecowar 10 Wolves:H Bears:H")
has(out, "Horde vs Horde, EcoWar (kills and loot), 10 minutes", "the match starts")
local E = cap.env.RefluxCore.modules["collection/ecowar"]
local m = cap.env.EcoWarDB.match
eq(m.teams[1].remote + m.teams[2].remote, 0, "both teams are local")
local id = m.id
local player = session("Thora", "Horde")
deliver(player, "Emily")                       -- S|id reaches the player
W.wire = {}

cap.see_critter("Rabbit", "Creature-1")
cap.kill("Emily", "Creature-1")
eq(m.teams[1].score, 25, "first rabbit: 10 base + 15 first-of-its-kind")
W.clock, W.now = W.clock + 3, W.now + 3
cap.see_critter("Rabbit", "Creature-2")
cap.kill("Emily", "Creature-2")
eq(m.teams[1].score, 36, "second rabbit 3 s later: 10 + 1 streak")
cap.kill("Emily", "Creature-2")
eq(m.teams[1].kills, 2, "a critter scores once")
cap.kill("Brokkr", "Creature-3")
eq(m.teams[1].kills, 2, "an unknown unit is not a critter")

player.see_critter("Squirrel", "Creature-9")
player.kill("Thora", "Creature-9")
eq(#player.sent, 1, "the player's own kill is reported to the captain")
eq(player.sent[1][2], "K|" .. id .. "|Creature-9|Squirrel", "as K|match|critter|kind")
deliver(cap, "Thora")
eq(m.teams[2].score, 25, "the captain credits Thora's team")
cap.see_critter("Squirrel", "Creature-9")
cap.kill("Thora", "Creature-9")
eq(m.teams[2].kills, 1, "the same squirrel seen by the captain is not counted twice")

cap.loot(2589, 5)
eq(m.teams[1].score, 37, "1500 copper of loot is one eco point")
cap.loot(4306, 1)
eq(m.teams[1].eco, 2100, "eco carries the remainder")
eq(m.teams[1].score, 38, "and 2100 copper is two points in all")
cap.env.EcoWarPriceSource = function(itemID, count) return 5000 * count end
cap.loot(2589, 1)
eq(m.teams[1].score, 43, "an auction-house price source, when present, sets the value")
cap.env.EcoWarPriceSource = nil

out = cap.cmd("status")
has(out, "1. Wolves -- 43 points (2 critters", "status ranks the teams")
out = cap.cmd("code")
has(out, "Match code for Wolves", "codes for our teams")
out = cap.cmd("stop")
has(out, "1. Wolves -- 43 points (rating 1516)", "Wolves win 43-25 and gain 16 rating")
has(out, "2. Bears -- 25 points (rating 1484)", "Bears lose 16")
eq(cap.notice, "EcoWar: Wolves win the match!", "a winner's banner")
local lg = cap.env.EcoWarDB.league
eq(lg.Wolves.won .. lg.Wolves.points .. lg.Bears.lost .. lg.Bears.points, "1310", "the league: W1 3pts / L1 0pts")
eq(lg.Wolves.best, 43, "best score kept")
out = cap.cmd("table")
has(out, "1. Wolves  P1 W1 D0 L0  3 pts  rating 1516", "the league table")

-- REFLUX rows reached the journal
W.clock = W.clock + 10
cap.tick(10)
local j, types = cap.env.CollectionJournalDB, {}
for seq = j.firstSeq, j.nextSeq - 1 do local t = j.rows[seq].type; types[t] = (types[t] or 0) + 1 end
eq(types[3010], 1, "3010 match start journalled")
eq(types[3011], 3, "three 3011 kills")
eq((types[3012] or 0) >= 2, true, "3012 eco rows")
eq(types[3014], 1, "3014 match end")
eq(types[3015], 2, "3015 one rating row per team")

-- ===== Cull scores no loot; Harvest scores no kills =======================================================
cap.cmd("new hvh cull 5 Wolves:H Bears:H")
cap.loot(4306, 3)
eq(cap.env.EcoWarDB.match.teams[1].score, 0, "Cull: loot scores nothing")
cap.cmd("stop")
cap.cmd("new hvh harvest 5 Wolves:H Bears:H")
cap.see_critter("Rat", "Creature-20")
cap.kill("Emily", "Creature-20")
eq(cap.env.EcoWarDB.match.teams[1].score, 0, "Harvest: kills score nothing")
cap.loot(4306, 5)
eq(cap.env.EcoWarDB.match.teams[1].score, 3, "Harvest: 3000 copper is three points")
cap.cmd("stop")

-- ===== mode rules =========================================================================================
eq(E.ewModeOk(4, { { "A", "Horde" }, { "B", "Horde" }, { "C", "Alliance" } }), 1, "tri: two Horde and one Alliance")
eq(E.ewModeOk(4, { { "A", "Alliance" }, { "B", "Horde" }, { "C", "Alliance" } }), 1, "tri: two Alliance and one Horde")
eq(E.ewModeOk(4, { { "A", "Horde" }, { "B", "Horde" }, { "C", "Horde" } }), 0, "tri: three of one side is refused")
eq(E.ewModeOk(3, { { "A", "Alliance" }, { "B", "Horde" } }), 1, "avh: one of each")
eq(E.ewModeOk(3, { { "A", "Horde" }, { "B", "Horde" } }), 0, "avh: two Horde refused")
eq(E.ewModeOk(1, { { "A", "Horde" } }), 0, "hvh needs two teams")

-- ===== Alliance vs Horde: the Alliance team is remote ======================================================
W.wire = {}
out = cap.cmd("new avh ecowar 10 Lions:A Wolves:H at Friday")
m = cap.env.EcoWarDB.match
eq(m.teams[1].remote, 1, "Lions (Alliance) are remote for a Horde captain")
eq(m.teams[2].remote, 0, "Wolves are local")
eq(cap.channel, "EcoWarFriday", "the captain joins the shared channel")
cap.see_critter("Rabbit", "Creature-30")
cap.kill("Emily", "Creature-30")
eq(m.teams[2].score, 25, "a Horde kill scores for Wolves")
cap.hear("Aria", "ECOWAR", "R|Lions|50|3|0", "CHANNEL")
eq(m.teams[1].score, 50, "the Lions' score arrives over the channel (carrier 2)")
cap.hear("Aria", "ECOWAR", "R|Lions|40|2|0", "CHANNEL")
eq(m.teams[1].score, 50, "a lower, late score cannot take points away")
cap.hear("Aria", "ECOWAR", "R|Wolves|999|9|0", "CHANNEL")
eq(m.teams[2].score, 25, "nobody can set a local team's score over the channel")
-- the Alliance captain's code (their own client: Lions local, Wolves remote)
local ally = session("Aria", "Alliance")
ally.cmd("new avh ecowar 10 Lions:A Wolves:H at Friday")
local am = ally.env.EcoWarDB.match
eq(am.teams[1].remote .. am.teams[2].remote, "01", "for the Alliance captain the Wolves are the remote team")
local AE = ally.env.RefluxCore.modules["collection/ecowar"]
local code = AE.ewCode(m.id, 1, 70, 4, 1200)
out = cap.cmd("enter " .. code)
has(out, "Lions's score accepted: 70", "a signed match code is accepted (carrier 4)")
eq(m.teams[1].score, 70, "and sets the remote score")
out = cap.cmd("enter " .. code:gsub("%-70%-", "-90-"))
has(out, "does not check out", "a tampered code is refused")
out = cap.cmd("enter " .. AE.ewCode(m.id, 2, 500, 1, 0))
has(out, "does not fit this match", "a code for our own team is refused")
out = cap.cmd("stop")
has(out, "1. Lions -- 70 points", "the remote team can win")
eq(cap.channel, nil, "the channel is left at the end")
ally.cmd("stop")

-- ===== three teams, two from one side ======================================================================
W.raid = { { "Emily", 1 }, { "Brokkr", 2 } }
local before_b, before_l = cap.env.EcoWarDB.league.Bears.played, cap.env.EcoWarDB.league.Lions.played
out = cap.cmd("new tri ecowar 10 Wolves:H Bears:H Lions:A")
m = cap.env.EcoWarDB.match
eq(#m.teams, 3, "three teams")
cap.see_critter("Toad", "Creature-40")
cap.kill("Brokkr", "Creature-40")
eq(m.teams[2].score, 25, "Brokkr (subgroup 2) scores for Bears")
cap.hear("Aria", "ECOWAR", "R|Lions|10|1|0", "CHANNEL")
out = cap.cmd("stop")
has(out, "1. Bears -- 25 points", "Bears top the three")
lg = cap.env.EcoWarDB.league
eq(lg.Bears.played - before_b, 2, "each team plays the other two")
eq(lg.Lions.played - before_l, 2, "Lions too")
eq(#cap.env.EcoWarDB.history, 5, "every match is in the history")

-- ===== a player's report goes nowhere when no match is announced =============================================
local idle = session("Ulf", "Horde")
idle.see_critter("Rabbit", "Creature-50")
idle.kill("Ulf", "Creature-50")
eq(#idle.sent, 0, "no match announced: nothing is reported")

print(string.format("ecowar: %d passed, %d failed", passed, failed))
os.exit(failed == 0 and 0 or 1)
