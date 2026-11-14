-- runtime/wow/ratatoskr_roll.lua -- Ratatoskr Roll's host glue (spec addon 65, Critter Census).
--
-- /census   this week's top three critters (target one and /love, /pet or /wave at it to count it)
--
-- The census is PARENA (stdlib/collection/ratatoskr.prn, RefluxCore.modules["collection/ratatoskr"]). This file
-- listens for the emotes and decides whether the target is a critter; every word comes from the catalogue
-- (runtime/wow/locales/ratatoskr.lua), including the creature-type string that identifies a critter in this locale.
--
-- Client calls used: hooksecurefunc("DoEmote", ...) -- /love, /pet and /wave all go through DoEmote(token, unit)
-- in the default UI; UnitExists, UnitIsPlayer, UnitCreatureType, UnitName; C_Map.GetBestMapForUnit for the zone
-- (0 when missing); GetLocale, time, print. Not one of these is probed on Forever yet: a client without
-- hooksecurefunc or DoEmote gets a clear message instead of a census that silently never counts.

local ADDON = "@ADDON@"
local VAR = "@VAR@"
local _, ns = ...

local C = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/ratatoskr"]
if not C then
    print("|cffff5555" .. ADDON .. ": RefluxCore's collection/ratatoskr module is missing; no census today.|r")
    return
end

local function L(key)
    local c = ns and ns.catalogue
    if not c then return key end
    local t = c[GetLocale and GetLocale() or "enUS"]
    return (t and t[key]) or (c.enUS and c.enUS[key]) or key
end

local db = nil
local hooked = false

local function zone()
    if C_Map and C_Map.GetBestMapForUnit then return C_Map.GetBestMapForUnit("player") or 0 end
    return 0
end

local function is_critter(unit)
    return UnitExists and UnitExists(unit) and not (UnitIsPlayer and UnitIsPlayer(unit))
        and UnitCreatureType and UnitCreatureType(unit) == L("CRITTER_TYPE")
end

local function on_emote(token, unit)
    if not db or type(token) ~= "string" then return end
    if C.censusEmoteCode(token:upper()) == 0 then return end
    local who = (type(unit) == "string" and unit ~= "" and unit ~= "none") and unit or "target"
    if not is_critter(who) then return end
    local name = UnitName(who)
    if not name then return end
    local total = C.censusRecord(db, name, token:upper(), time(), zone())
    print(string.format(L("LOGGED"), L("NAME"), name, total))
end

local function announce_award()
    local w = C.censusAwardDue(db, time())
    if w then print(string.format(L("AWARD"), L("NAME"), w[1], w[2])) end
end

local f = CreateFrame("Frame")
f:RegisterEvent("ADDON_LOADED")
f:RegisterEvent("PLAYER_LOGIN")
f:SetScript("OnEvent", function(_, event, arg1)
    if event == "ADDON_LOADED" then
        if arg1 == ADDON then
            _G[VAR] = C.censusInit(_G[VAR] or {})
            db = _G[VAR]
        end
    elseif event == "PLAYER_LOGIN" and db then
        if not hooked and hooksecurefunc and DoEmote then
            hooksecurefunc("DoEmote", on_emote)
            hooked = true
        end
        announce_award()
    end
end)

SLASH_RATATOSKRROLL1 = "/census"
SLASH_RATATOSKRROLL2 = "/ratatoskr"
SlashCmdList["RATATOSKRROLL"] = function()
    if not db then print(ADDON .. ": not loaded yet."); return end
    if not hooked then print(string.format(L("NO_HOOK"), L("NAME"))) end
    local top = C.censusTop(db, C.censusWeek(time()), 3)
    if #top == 0 then
        print(L("NONE_WEEK"))
    else
        print(string.format(L("TOP_HEAD"), L("NAME")))
        for i, row in ipairs(top) do print(string.format(L("TOP_ROW"), i, row[1], row[2])) end
    end
    local t = C.censusTotals(db)
    print(string.format(L("TOTALS"), t[1], t[2]))
    print(L("HELP"))
end
