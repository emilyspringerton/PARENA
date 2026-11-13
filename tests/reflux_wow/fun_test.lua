-- tests/reflux_wow/fun_test.lua -- the four fun addons (Hrungnir 70, Ratatoskr Roll 65, Völva 72, Saga of Fools 73)
-- in the packaged client, end to end, under real Lua 5.1.
-- usage: lua5.1 fun_test.lua <client dir (wow-package-collection.sh output)>
--
-- Every addon in the package is loaded the way the client loads it (each .toc's files in order, each given
-- (addonName, namespace)) into a stubbed WoW per session. The test plays the moments each addon reacts to --
-- minutes online, quests, a level, deaths, pats, emotes at critters, jumps, mails from yourself, logins across a
-- week -- and checks what they say, what they save and the REFLUX rows that reach the journal. It ends with the
-- missing-string check over all 12 spec locales.
assert(_VERSION == "Lua 5.1", "must run under Lua 5.1, got " .. tostring(_VERSION))
local client = assert(arg[1])

local passed, failed = 0, 0
local function eq(got, want, what)
    if got == want then passed = passed + 1
    else failed = failed + 1; print("FAIL: " .. what .. " (got " .. tostring(got) .. ", want " .. tostring(want) .. ")") end
end
local function has(s, sub, what) eq(type(s) == "string" and s:find(sub, 1, true) ~= nil, true, what .. " [" .. tostring(s) .. "]") end
local function hasnt(s, sub, what) eq(type(s) == "string" and s:find(sub, 1, true) == nil, true, what .. " [" .. tostring(s) .. "]") end

local function toc_files(dir, name)
    local files = {}
    for line in io.lines(dir .. "/" .. name .. ".toc") do
        if line ~= "" and not line:match("^#") then files[#files + 1] = line end
    end
    return files
end

local function fontstring()
    local fs = { text = "" }
    function fs:SetPoint() end
    function fs:SetText(t) self.text = t end
    function fs:GetText() return self.text end
    return fs
end

local function session(world, saved)
    local env = { print = function(...)
        local t = {}
        for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
        world.out[#world.out + 1] = table.concat(t, " ")
    end }
    for _, k in ipairs({ "pairs", "ipairs", "type", "tostring", "tonumber", "select", "string", "table", "math",
                         "next", "error", "assert", "pcall", "unpack", "setmetatable", "getmetatable", "rawget", "rawset" }) do
        env[k] = _G[k]
    end
    env._G = env
    env.time = function() return world.now end
    env.GetLocale = function() return world.locale end
    env.UnitName = function(u)
        if u == "player" then return world.name end
        if u == "target" and world.target then return world.target.name end
    end
    env.UnitLevel = function() return world.level end
    env.UnitExists = function(u) return u == "player" or (u == "target" and world.target ~= nil) end
    env.UnitIsPlayer = function(u) return u == "player" or (u == "target" and world.target and world.target.player) end
    env.UnitCreatureType = function(u) return u == "target" and world.target and world.target.ctype or nil end
    env.GetRealmName = function() return world.realm end
    env.GetMoney = function() return world.money end
    env.NUM_BAG_SLOTS = 4
    env.BANK_CONTAINER = -1
    env.NUM_BANKBAGSLOTS = 7
    env.ATTACHMENTS_MAX_RECEIVE = 16
    env.C_Container = { GetContainerNumSlots = function() return 0 end, GetContainerItemInfo = function() return nil end }
    env.GetInboxNumItems = function() return #world.mail end
    env.GetInboxItem = function() return nil end
    env.GetInboxHeaderInfo = function(i) local m = world.mail[i]; if m then return nil, nil, m.sender, m.subject, 0, 0, m.days end end
    env.GetInboxText = function(i) return "body" end
    env.C_Map = { GetBestMapForUnit = function() return 1420 end,
                  GetPlayerMapPosition = function() return { GetXY = function() return 0.5, 0.5 end } end }
    env.DoEmote = function() end
    env.JumpOrAscendStart = function() end
    env.hooksecurefunc = function(name, fn)
        local orig = env[name]
        env[name] = function(...) local r = { orig(...) }; fn(...); return unpack(r) end
    end
    env.UIParent = {}
    env.RaidWarningFrame = {}
    env.ChatTypeInfo = { RAID_WARNING = {} }
    env.RaidNotice_AddMessage = function(_, text) world.notices[#world.notices + 1] = text end
    local frames = {}
    env.CreateFrame = function(_, name)
        local f = { events = {}, scripts = {}, shown = true, name = name }
        function f:RegisterEvent(e) self.events[e] = true end
        function f:SetScript(k, fn) self.scripts[k] = fn end
        function f:GetScript(k) return self.scripts[k] end
        function f:SetSize() end
        function f:SetPoint() end
        function f:SetMovable() end
        function f:EnableMouse() end
        function f:RegisterForDrag() end
        function f:StartMoving() end
        function f:StopMovingOrSizing() end
        function f:Show() self.shown = true end
        function f:Hide() self.shown = false end
        function f:CreateFontString() local fs = fontstring(); self.fonts = self.fonts or {}; self.fonts[#self.fonts + 1] = fs; return fs end
        frames[#frames + 1] = f
        if name then env[name] = f end
        return f
    end
    env.SlashCmdList = {}
    for k, v in pairs(saved) do env[k] = v end

    local s = { env = env, frames = frames }
    function s.fire(event, ...)
        for _, f in ipairs(frames) do
            if f.events[event] and f.scripts.OnEvent then f.scripts.OnEvent(f, event, ...) end
        end
    end
    function s.tick(dt) for _, f in ipairs(frames) do if f.scripts.OnUpdate then f.scripts.OnUpdate(f, dt) end end end
    function s.slash(key, msg) world.out = {}; env.SlashCmdList[key](msg or ""); return table.concat(world.out, "\n") end
    function s.load(addon)
        local dir = client .. "/AddOns/" .. addon
        local ns = {}
        for _, file in ipairs(toc_files(dir, addon)) do
            local chunk = assert(loadfile(dir .. "/" .. file))
            setfenv(chunk, env)
            chunk(addon, ns)
        end
    end
    function s.login()
        for _, a in ipairs({ "RefluxCore", "CollectionJournal", "FafnirsBed", "Hrungnir", "RatatoskrRoll", "Volva", "SagaOfFools" }) do
            s.load(a)
        end
        for _, a in ipairs({ "RefluxCore", "CollectionJournal", "FafnirsBed", "Hrungnir", "RatatoskrRoll", "Volva", "SagaOfFools" }) do
            s.fire("ADDON_LOADED", a)
        end
        world.out = {}
        s.fire("PLAYER_LOGIN")
        return table.concat(world.out, "\n")
    end
    function s.out() local o = table.concat(world.out, "\n"); world.out = {}; return o end
    return s
end

-- the REFLUX rows the journal has seen, by type
local function journal_types(s)
    s.tick(10)
    local j, types = s.env.CollectionJournalDB, {}
    for seq = j.firstSeq, j.nextSeq - 1 do local t = j.rows[seq].type; types[t] = (types[t] or 0) + 1 end
    return types
end

local function persist(s, saved)
    for _, k in ipairs({ "FafnirsBedDB", "HrungnirDB", "RatatoskrRollDB", "VolvaDB", "SagaOfFoolsDB", "CollectionJournalDB" }) do
        saved[k] = s.env[k]
    end
end

local MONDAY = 1762732800          -- Monday 2025-11-10 00:00 UTC
local world = { out = {}, notices = {}, now = MONDAY + 3600, name = "Emily", realm = "Forever", money = 1234567,
                level = 12, locale = "enUS", mail = {} }
local saved = {}

-- ===== session 1 ==========================================================================================
local s = session(world, saved)
local hello = s.login()

-- Hrungnir
local rockframe = s.env.HrungnirFrame
eq(type(rockframe), "table", "Hrungnir made its tiny frame")
eq(rockframe.fonts and rockframe.fonts[1]:GetText(), "[o_o]", "a new rock is content")
eq(rockframe.fonts[2]:GetText(), "Lv 1", "and level 1")
has(hello, "Hrungnir says:", "the rock says hello at login")
s.tick(60)
eq(s.env.HrungnirDB.points, 1, "a minute online is a pebble point")
for _ = 1, 4 do s.fire("QUEST_TURNED_IN") end
eq(s.env.HrungnirDB.points, 41, "four quests are 40 more")
s.out()
s.fire("PLAYER_LEVEL_UP")
local o = s.out()
eq(s.env.HrungnirDB.level, 2, "91 points is level 2 (needs 50)")
has(o, "Hrungnir is now level 2!", "the level-up is announced")
eq(rockframe.fonts[2]:GetText(), "Lv 2", "the frame shows it")
o = s.slash("HRUNGNIR", "pat")
eq(s.env.HrungnirDB.pats, 1, "a pat counts")
eq(s.env.HrungnirDB.points, 93, "and is worth 2")
o = s.slash("HRUNGNIR", "pat")
eq(s.env.HrungnirDB.pats, 1, "a second pat inside a minute does not count")
local c3 = false
for i = 1, 3 do if o:find(({ "Patience. I am a rock.", "One pat a minute. I have standards.", "You are wearing me smooth." })[i], 1, true) then c3 = true end end
eq(c3, true, "and the rock says so [" .. o .. "]")
o = s.slash("HRUNGNIR", "")
has(o, "a level 2 rock. 93 pebble points (57 to the next level)", "/rock reports progress")
s.slash("HRUNGNIR", "hide")
eq(rockframe.shown, false, "/rock hide hides the frame")
s.slash("HRUNGNIR", "show")
eq(rockframe.shown, true, "/rock show brings it back")

-- Ratatoskr Roll
world.target = { name = "Squirrel", ctype = "Critter" }
s.out()
s.env.DoEmote("LOVE", "")
has(s.out(), "Ratatoskr Roll: Squirrel -- 1 acts of affection", "loving a critter is logged")
s.env.DoEmote("PET")
s.env.DoEmote("wave", "target")
eq(s.env.RatatoskrRollDB.critters.Squirrel.love, 1, "one love")
eq(s.env.RatatoskrRollDB.critters.Squirrel.pet, 1, "one pet")
eq(s.env.RatatoskrRollDB.critters.Squirrel.wave, 1, "one wave (token case does not matter)")
eq(s.env.RatatoskrRollDB.critters.Squirrel.zone, 1420, "with the zone")
s.env.DoEmote("DANCE")
eq(s.env.RatatoskrRollDB.critters.Squirrel.love + s.env.RatatoskrRollDB.critters.Squirrel.pet +
   s.env.RatatoskrRollDB.critters.Squirrel.wave, 3, "/dance is not affection")
world.target = { name = "Hogger", ctype = "Humanoid" }
s.env.DoEmote("LOVE")
eq(s.env.RatatoskrRollDB.critters.Hogger, nil, "a humanoid is not a critter")
world.target = { name = "Brokkr", ctype = "Humanoid", player = true }
s.env.DoEmote("WAVE")
eq(s.env.RatatoskrRollDB.critters.Brokkr, nil, "a player is not a critter")
world.target = { name = "Toad", ctype = "Critter" }
s.env.DoEmote("PET")
world.target = nil
s.env.DoEmote("LOVE")
o = s.slash("RATATOSKRROLL")
has(o, "1. Squirrel (3)", "/census ranks this week's critters")
has(o, "2. Toad (1)", "second place")
has(o, "All time: 2 critters, 4 acts of affection.", "and the totals")

-- Völva
has(hello, "Völva cracks a cookie:", "a fortune opens at login")
has(hello, "Lucky numbers:", "with lucky numbers")
eq(s.env.VolvaDB.opened, 1, "one fortune opened")
local nums = {}
for n in hello:match("Lucky numbers: ([%d ]+)"):gmatch("%d+") do nums[#nums + 1] = tonumber(n) end
eq(#nums, 6, "six lucky numbers")
local inrange = true
for _, n in ipairs(nums) do if n < 1 or n > 49 then inrange = false end end
eq(inrange, true, "all from 1 to 49")
o = s.slash("VOLVA")
eq(o, hello:match("(Völva cracks a cookie:[^\n]*\n[^\n]*)"), "/fortune shows the same fortune again")

-- Saga of Fools
s.out()
for _ = 1, 99 do s.env.JumpOrAscendStart() end
eq(s.env.SagaOfFoolsDB.earned[1], nil, "99 jumps earn nothing")
s.env.JumpOrAscendStart()
o = s.out()
has(o, "Achievement (sort of) earned -- Bunny Hop Hobbyist: Jump 100 times.", "the 100th jump earns it")
eq(world.notices[#world.notices], "Achievement (sort of): Bunny Hop Hobbyist", "with a banner")
for _ = 1, 25 do s.env.DoEmote("DANCE") end
eq(s.env.SagaOfFoolsDB.earned[8] ~= nil, true, "25 dances")
for i = 1, 12 do world.mail[i] = { sender = "Emily", subject = "note " .. i, days = 30 } end
world.mail[13] = { sender = "Somebody", subject = "hi", days = 30 }
for i = 1, 13 do s.env.GetInboxText(i) end
s.env.GetInboxText(1)
eq(s.env.SagaOfFoolsDB.counters.selfmail, 12, "12 mails from yourself; a stranger's and a re-read do not count")
eq(s.env.SagaOfFoolsDB.earned[6] ~= nil, true, "Pen Pal of One")
for _ = 1, 10 do s.fire("PLAYER_DEAD") end
eq(s.env.SagaOfFoolsDB.earned[4] ~= nil, true, "ten deaths: Floor Inspector")
eq(s.env.HrungnirDB.deaths, 10, "the rock counted them too")
eq(rockframe.fonts[1]:GetText(), "[-_-]", "and looks smug")
o = s.slash("SAGAOFFOOLS")
has(o, "Saga of Fools: 4 of 14 earned --", "/saga counts")
has(o, "[x] Floor Inspector", "earned rows are ticked")
has(o, "[ ] Leap of Faith -- Jump 1,000 times. Nothing was gained. (100/1000)", "the rest show progress")

-- REFLUX: every addon's rows reached the journal
local types = journal_types(s)
eq((types[2690] or 0) > 0, true, "Hrungnir 2690 points rows journalled")
eq(types[2691], 1, "one 2691 level-up row")
eq(types[2692], 10, "ten 2692 death rows")
eq(types[2640], 4, "four 2640 affection rows")
eq(types[2710], 1, "one 2710 fortune row")
eq(types[2720], 4, "four 2720 earned rows")

-- every line printed this session had its placeholders filled
s.slash("HRUNGNIR", ""); s.slash("RATATOSKRROLL"); s.slash("VOLVA")
for _, line in ipairs(world.out) do hasnt(line, "%", "no unfilled format in output") end
persist(s, saved)

-- ===== session 2: a week later, broke ======================================================================
world.now = MONDAY + 7 * 86400 + 7200
world.money = 50
world.out, world.notices = {}, {}
s = session(world, saved)
hello = s.login()
has(hello, "Ratatoskr Roll: Critter of the Week was Squirrel, with 3 acts of affection.", "last week's award at login")
eq(saved.RatatoskrRollDB.awarded[s.env.RefluxCore.modules["collection/ratatoskr"].censusWeek(MONDAY)] , 1, "and marked given")
has(hello, "Broke as a Joke", "logging in with 50 copper is an achievement")
eq(s.env.VolvaDB.opened, 2, "a second login opens a second fortune")
eq(s.env.HrungnirDB.level, 2, "the rock remembers its level")
persist(s, saved)
world.now = world.now + 60
s = session(world, saved)
hello = s.login()
hasnt(hello, "Critter of the Week", "the award is given once")

-- ===== Völva over many logins: stat fortunes appear, deterministic per (character, day, count) ==========
local V = s.env.RefluxCore.modules["collection/volva"]
local stat_seen, kinds = 0, {}
for i = 1, 40 do
    world.now = world.now + 86400
    persist(s, saved)
    s = session(world, saved)
    hello = s.login()
    local line = hello:match("Völva cracks a cookie: \"([^\"]*)\"") or ""
    if line:find("Your purse holds", 1, true) or line:find("At level", 1, true) or line:find("Your rock is level", 1, true)
        or line:find("critters have felt", 1, true) or line:find("This is fortune number", 1, true)
        or line:find("items rest across", 1, true) then stat_seen = stat_seen + 1 end
    hasnt(line, "%", "fortune " .. i .. " is filled")
    eq(line ~= "", true, "fortune " .. i .. " was opened")
end
eq(stat_seen > 3 and stat_seen < 30, true, "some (not all) fortunes read your stats: " .. stat_seen .. " of 40")
local a1 = V.fortuneOpen({ opened = 5, lastday = 0 }, "Emily-Forever", MONDAY, 20, 4)
local a2 = V.fortuneOpen({ opened = 5, lastday = 0 }, "Emily-Forever", MONDAY, 20, 4)
local b1 = V.fortuneOpen({ opened = 5, lastday = 0 }, "Brokkr-Forever", MONDAY, 20, 4)
eq(table.concat(a1, ","), table.concat(a2, ","), "the same character, day and count give the same fortune")
eq(table.concat(a1, ",") ~= table.concat(b1, ","), true, "another character gets another one")

-- ===== Saga: gauges from the sibling addons =================================================================
s.env.HrungnirDB.level = 10
persist(s, saved)
s = session(world, saved)
hello = s.login()
has(hello, "Rock Steady", "Hrungnir at level 10 earns Rock Steady")

-- ===== missing-string check, all 12 spec locales ==========================================================
local LOCALES = { "enUS", "enGB", "deDE", "esES", "esMX", "frFR", "itIT", "ptBR", "ruRU", "koKR", "zhCN", "zhTW" }
local report = {}
for _, addon in ipairs({ "Hrungnir", "RatatoskrRoll", "Volva", "SagaOfFools" }) do
    local ns = {}
    local chunk = assert(loadfile(client .. "/AddOns/" .. addon .. "/locale.lua"))
    chunk(addon, ns)
    local cat = ns.catalogue
    local keys = 0
    local gaps = {}
    for _ in pairs(cat.enUS) do keys = keys + 1 end
    for _, loc in ipairs(LOCALES) do
        local missing = 0
        for k in pairs(cat.enUS) do if not (cat[loc] and cat[loc][k]) then missing = missing + 1 end end
        if loc == "enUS" or loc == "enGB" then eq(missing, 0, addon .. " " .. loc .. " is complete") end
        if missing > 0 then gaps[#gaps + 1] = loc end
    end
    if #gaps > 0 then report[#report + 1] = string.format("%s: %d strings; no catalogue yet for %s (falls back to enUS)", addon, keys, table.concat(gaps, " ")) end
end
-- the catalogue holds every line the PARENA modules can ask for
local R = s.env.RefluxCore.modules["collection/hrungnir"]
local ns = {}; assert(loadfile(client .. "/AddOns/Hrungnir/locale.lua"))("Hrungnir", ns)
local all = true
for m = 1, 6 do for i = 1, R.rockCommentCount(m) do if not ns.catalogue.enUS["C" .. m .. "_" .. i] then all = false end end end
eq(all, true, "every rock remark the module can pick exists")
ns = {}; assert(loadfile(client .. "/AddOns/SagaOfFools/locale.lua"))("SagaOfFools", ns)
local S = s.env.RefluxCore.modules["collection/saga"]
all = true
for id = 1, S.sagaCount() do if not (ns.catalogue.enUS["T" .. id] and ns.catalogue.enUS["D" .. id]) then all = false end end
eq(all, true, "every achievement has a title and description")
ns = {}; assert(loadfile(client .. "/AddOns/Volva/locale.lua"))("Volva", ns)
all = true
for i = 1, tonumber(ns.catalogue.enUS.PLAIN_COUNT) do if not ns.catalogue.enUS["PLAIN_" .. i] then all = false end end
eq(all, true, "every plain fortune PLAIN_COUNT promises exists")

print(string.format("fun addons: %d passed, %d failed", passed, failed))
print("missing-string check (translators' catalogues not yet supplied):")
for _, line in ipairs(report) do print("  " .. line) end
os.exit(failed == 0 and 0 or 1)
