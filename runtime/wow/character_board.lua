-- runtime/wow/character_board.lua -- the Total Character Board's host glue (spec addon 1). Rules and the progression
-- record: stdlib/collection/board.prn. Words: locale.lua.
-- /board        every character on the account: level, xp, rested, gold, zone, last seen
-- /board snap   take a snapshot now (one is taken at login, every level and logout anyway)
--
-- Client calls (retail-lineage; none probed on Forever yet, each feature-tested; a missing one leaves its field 0
-- or empty, never a guess): UnitName, GetRealmName, UnitClass, UnitRace, UnitLevel, UnitXP, UnitXPMax,
-- GetXPExhaustion, GetMoney, GetZoneText, C_Map.GetBestMapForUnit, RequestTimePlayed + TIME_PLAYED_MSG,
-- GetAverageItemLevel, and professions from GetProfessions/GetProfessionInfo (retail) or
-- GetNumSkillLines/GetSkillLineInfo (classic).
local ADDON, VAR = "@ADDON@", "@VAR@"
local _, ns = ...
local B = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/board"]
if not B then print(ADDON .. ": RefluxCore's collection/board module is missing.") return end

local function L(key)
    local c = ns and ns.catalogue
    if not c then return key end
    local t = c[GetLocale and GetLocale() or "enUS"]
    return (t and t[key]) or (c.enUS and c.enUS[key]) or key
end

local db, played = nil, 0

local function character()
    local name = UnitName and UnitName("player")
    if not name then return nil end
    local realm = GetRealmName and GetRealmName()
    return (realm and realm ~= "") and (name .. "-" .. realm) or name
end

local function professions()
    local out = {}
    if GetProfessions and GetProfessionInfo then
        for _, idx in ipairs({ GetProfessions() }) do
            if idx then
                local name, _, rank, max = GetProfessionInfo(idx)
                if name then out[#out + 1] = string.format("%s %d/%d", name, rank or 0, max or 0) end
            end
        end
    elseif GetNumSkillLines and GetSkillLineInfo then
        for i = 1, GetNumSkillLines() do
            local name, header, _, rank, _, _, max = GetSkillLineInfo(i)
            if name and not header and max and max > 1 then out[#out + 1] = string.format("%s %d/%d", name, rank or 0, max) end
        end
    end
    table.sort(out)
    return table.concat(out, "; ")
end

local function snapshot()
    local who = character()
    if not (db and who) then return end
    -- not `UnitClass and UnitClass(...)`: `and` keeps one value, and the token is the second
    local class, race
    if UnitClass then class = select(2, UnitClass("player")) end
    if UnitRace then race = select(2, UnitRace("player")) end
    local ilvl = 0
    if GetAverageItemLevel then ilvl = math.floor((select(2, GetAverageItemLevel())) or 0) end
    B.boardSnap_(db, {
        t = time(), character = who, class = class or "", race = race or "",
        level = UnitLevel and UnitLevel("player") or 0,
        xp = UnitXP and UnitXP("player") or 0, xpmax = UnitXPMax and UnitXPMax("player") or 0,
        rested = GetXPExhaustion and GetXPExhaustion() or 0,
        gold = GetMoney and GetMoney() or 0,
        zone = GetZoneText and GetZoneText() or "",
        map = (C_Map and C_Map.GetBestMapForUnit and C_Map.GetBestMapForUnit("player")) or 0,
        played = played, ilvl = ilvl, profs = professions(),
    })
end

local pending = nil
local f = CreateFrame("Frame")
for _, e in ipairs({ "ADDON_LOADED", "PLAYER_LOGIN", "PLAYER_LEVEL_UP", "TIME_PLAYED_MSG", "PLAYER_LOGOUT" }) do f:RegisterEvent(e) end
f:SetScript("OnEvent", function(_, event, a)
    if event == "ADDON_LOADED" then
        if a == ADDON then _G[VAR] = B.boardInit(_G[VAR] or {}); db = _G[VAR] end
    elseif event == "PLAYER_LOGIN" then
        if RequestTimePlayed then RequestTimePlayed() end
        pending = 3                         -- snapshot once /played answers, or after 3 s without it
    elseif event == "TIME_PLAYED_MSG" then
        played = a or played
        if pending then pending = nil; snapshot() end
    elseif event == "PLAYER_LEVEL_UP" then
        pending = 1                         -- UnitLevel catches up a moment after the event
    elseif event == "PLAYER_LOGOUT" then
        snapshot()
    end
end)
f:SetScript("OnUpdate", function(_, elapsed)
    if pending then
        pending = pending - (elapsed or 0)
        if pending <= 0 then pending = nil; snapshot() end
    end
end)

local function money(c) return string.format("%dg", math.floor((c or 0) / 10000)) end

SLASH_CHARACTERBOARD1 = "/board"
SlashCmdList["CHARACTERBOARD"] = function(msg)
    if not db then print(ADDON .. ": not loaded yet.") return end
    if (msg or ""):match("snap") then snapshot(); print(string.format(L("SNAPPED"), L("NAME"), #db.rows)) return end
    local whos = {}
    for who in pairs(db.chars) do whos[#whos + 1] = who end
    table.sort(whos)
    print(string.format(L("HEAD"), L("NAME"), #whos, #db.rows))
    for _, who in ipairs(whos) do
        local s = db.chars[who]
        print(string.format(L("ROW"), who, s.level or 0, B.boardXpPermille(s.xp or 0, s.xpmax or 0) / 10,
            math.floor((s.rested or 0) / math.max(1, s.xpmax or 1) * 100), money(s.gold), s.zone or "", date and date("%Y-%m-%d %H:%M", s.t) or tostring(s.t)))
    end
end
