-- runtime/wow/saga_of_fools.lua -- Saga of Fools' host glue (spec addon 73, Achievement Nonsense).
--
-- /saga   every nonsense achievement: earned, and how far along the rest are
--
-- The achievements -- what counts, how much, and when one is earned -- are PARENA (stdlib/collection/saga.prn,
-- RefluxCore.modules["collection/saga"]). This file moves the counters and announces what they earn. Every word
-- comes from the catalogue (runtime/wow/locales/saga.lua).
--
-- Counters and where they come from:
--   jumps     hooksecurefunc("JumpOrAscendStart")            deaths   PLAYER_DEAD
--   dances    DoEmote("DANCE")   sits  DoEmote("SIT")         broke    logging in with GetMoney() under 100 copper
--   selfmail  hooksecurefunc("GetInboxText", i) -- the default UI calls it when a mail is opened -- when
--             GetInboxHeaderInfo(i)'s sender is one of this account's characters (Fafnir's Bed knows them;
--             without it, only this character counts), once per mail per session
--   rocklevel, critters, fortunes   read at login from Hrungnir's, Ratatoskr Roll's and Völva's saved tables
-- A hook the client does not have leaves its counter still, and /saga says which.

local ADDON = "@ADDON@"
local VAR = "@VAR@"
-- the sibling addons' saved tables, by the names the packager gave them (env-driven, never assumed)
local function sibling(var) local t = _G[var]; return type(t) == "table" and t or nil end
local FAFNIR_VAR, HRUNGNIR_VAR, RATATOSKR_VAR, VOLVA_VAR = "@FAFNIR_VAR@", "@HRUNGNIR_VAR@", "@RATATOSKR_VAR@", "@VOLVA_VAR@"
local _, ns = ...

local S = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/saga"]
if not S then
    print("|cffff5555" .. ADDON .. ": RefluxCore's collection/saga module is missing; no saga today.|r")
    return
end

local function L(key)
    local c = ns and ns.catalogue
    if not c then return key end
    local t = c[GetLocale and GetLocale() or "enUS"]
    return (t and t[key]) or (c.enUS and c.enUS[key]) or key
end

local db = nil
local missing = {}          -- hook name -> true when this client does not have it
local opened = {}           -- mails counted this session

local function announce(ids)
    for _, id in ipairs(ids) do
        print(string.format(L("EARNED"), L("NAME"), L("T" .. id), L("D" .. id)))
        if RaidNotice_AddMessage and RaidWarningFrame and ChatTypeInfo then
            RaidNotice_AddMessage(RaidWarningFrame, string.format(L("BANNER"), L("T" .. id)), ChatTypeInfo["RAID_WARNING"])
        end
    end
end

local function bump(key, n) if db then announce(S.sagaBump(db, key, n or 1, time())) end end
local function set(key, v) if db and type(v) == "number" then announce(S.sagaSet(db, key, v, time())) end end

local function own_names()
    local names = {}
    if UnitName then local me = UnitName("player"); if me then names[me] = true end end
    if sibling(FAFNIR_VAR) and type(sibling(FAFNIR_VAR).chars) == "table" then
        for who in pairs(sibling(FAFNIR_VAR).chars) do names[(who:match("^([^-]+)")) or who] = true end
    end
    return names
end

local function count_keys(t)
    local n = 0
    if type(t) == "table" then for _ in pairs(t) do n = n + 1 end end
    return n
end

local function hook(name, fn)
    if hooksecurefunc and _G[name] then hooksecurefunc(name, fn) else missing[name] = true end
end

local hooked = false
local function install_hooks()
    if hooked then return end
    hooked = true
    hook("JumpOrAscendStart", function() bump("jumps") end)
    hook("DoEmote", function(token)
        if type(token) ~= "string" then return end
        token = token:upper()
        if token == "DANCE" then bump("dances") elseif token == "SIT" then bump("sits") end
    end)
    hook("GetInboxText", function(i)
        if not GetInboxHeaderInfo then return end
        local _, _, sender, subject, money, cod, days = GetInboxHeaderInfo(i)
        if not sender then return end
        local key = sender .. "\1" .. tostring(subject) .. "\1" .. tostring(days)
        if opened[key] then return end
        opened[key] = true
        if own_names()[sender] then bump("selfmail") end
    end)
end

local f = CreateFrame("Frame")
f:RegisterEvent("ADDON_LOADED")
f:RegisterEvent("PLAYER_LOGIN")
f:RegisterEvent("PLAYER_DEAD")
f:SetScript("OnEvent", function(_, event, arg1)
    if event == "ADDON_LOADED" then
        if arg1 == ADDON then
            _G[VAR] = S.sagaInit(_G[VAR] or {})
            db = _G[VAR]
        end
    elseif not db then
        return
    elseif event == "PLAYER_LOGIN" then
        install_hooks()
        if GetMoney and (GetMoney() or 0) < 100 then set("broke", 1) end
        if sibling(HRUNGNIR_VAR) then set("rocklevel", sibling(HRUNGNIR_VAR).level) end
        if sibling(RATATOSKR_VAR) then set("critters", count_keys(sibling(RATATOSKR_VAR).critters)) end
        if sibling(VOLVA_VAR) then set("fortunes", sibling(VOLVA_VAR).opened) end
    elseif event == "PLAYER_DEAD" then
        bump("deaths")
    end
end)

SLASH_SAGAOFFOOLS1 = "/saga"
SLASH_SAGAOFFOOLS2 = "/fools"
SlashCmdList["SAGAOFFOOLS"] = function()
    if not db then print(ADDON .. ": not loaded yet."); return end
    print(string.format(L("LIST_HEAD"), L("NAME"), S.sagaEarnedCount(db), S.sagaCount()))
    for id = 1, S.sagaCount() do
        local p = S.sagaProgress(db, id)
        if p[3] > 0 then
            print(string.format(L("ROW_DONE"), L("T" .. id), L("D" .. id)))
        else
            print(string.format(L("ROW_TODO"), L("T" .. id), L("D" .. id), math.min(p[1], p[2]), p[2]))
        end
    end
    for name in pairs(missing) do print(string.format(L("NO_HOOK"), name)) end
end
