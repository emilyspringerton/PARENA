-- runtime/wow/ratatoskr_roll.lua -- Ratatoskr Roll's host glue (spec addon 65, Critter Census).
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

-- ---- the rumour carrier (REFLUX tips API, runtime/wow/reflux_tips.lua) --------------------------------------
-- Ratatoskr runs gossip up and down the World Tree, so it voices /rumor: a true tip about ANOTHER installed
-- Collection addon in its words (RUMOR_n), the plain tip beside it. /census rumor asks directly; /rumor is Loki's
-- Lies' command when that is installed (its rumour, then this), and Ratatoskr's own when nobody else claimed it.
-- The rotation is kept in db.rumor, so it carries over between sessions and characters.
if RefluxTips then RefluxTips.publish({ addon = ADDON, spec = 65, catalogue = ns.catalogue }) end

local function voice(name, own)
    if own then return string.format(L("RUMOR_SELF"), L("NAME")) end
    return string.format(L("RUMOR_" .. RefluxTips.line(tonumber(L("RUMOR_COUNT")) or 0)), L("NAME"), name)
end

local function rumor()
    if not RefluxTips then print(string.format(L("NO_TIPS"), L("NAME"))); return end
    for _, line in ipairs(RefluxTips.rumor(ADDON)) do print(line) end
end

local f = CreateFrame("Frame")
f:RegisterEvent("ADDON_LOADED")
f:RegisterEvent("PLAYER_LOGIN")
f:SetScript("OnEvent", function(_, event, arg1)
    if event == "ADDON_LOADED" then
        if arg1 == ADDON then
            _G[VAR] = C.censusInit(_G[VAR] or {})
            db = _G[VAR]
            db.rumor = type(db.rumor) == "table" and db.rumor or {}
            if RefluxTips then
                RefluxTips.carrier({ addon = ADDON, state = db.rumor, voice = voice,
                                     alone = function() return string.format(L("RUMOR_ALONE"), L("NAME")) end })
            end
        end
    elseif event == "PLAYER_LOGIN" and db then
        if not hooked and hooksecurefunc and DoEmote then
            hooksecurefunc("DoEmote", on_emote)
            hooked = true
        end
        announce_award()
        -- every addon has loaded by now, so this sees Loki's Lies' /rumor whatever the load order
        if RefluxTips and not SlashCmdList["RATATOSKRRUMOR"] then
            RefluxTips.claim_slash("RATATOSKRRUMOR", { "/rumor", "/rumour" }, rumor)
        end
    end
end)

SLASH_RATATOSKRROLL1 = "/census"
SLASH_RATATOSKRROLL2 = "/ratatoskr"
SlashCmdList["RATATOSKRROLL"] = function(msg)
    if not db then print(ADDON .. ": not loaded yet."); return end
    if (msg or ""):match("^%s*rumou?r") then rumor(); return end
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
