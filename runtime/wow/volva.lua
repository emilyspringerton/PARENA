-- runtime/wow/volva.lua -- Völva's host glue (spec addon 72, Fortune Cookie).
--
-- /fortune   today's fortune again
--
-- Which fortune, which kind and the lucky numbers are PARENA (stdlib/collection/volva.prn,
-- RefluxCore.modules["collection/volva"]). This file offers the stat lines it can fill right now, prints the
-- fortune at login, and shows it again on /fortune. Every word comes from the catalogue
-- (runtime/wow/locales/volva.lua).
--
-- Stat lines are offered only when their number is really there: gold (GetMoney), level (UnitLevel), the
-- account's net inventory (Fafnir's Bed's saved table), the rock's level (Hrungnir's), critters loved (Ratatoskr
-- Roll's), and how many fortunes this character has opened. A missing addon removes its line; nothing is guessed.

local ADDON = "@ADDON@"
local VAR = "@VAR@"
-- the sibling addons' saved tables, by the names the packager gave them (env-driven, never assumed)
local function sibling(var) local t = _G[var]; return type(t) == "table" and t or nil end
local FAFNIR_VAR, HRUNGNIR_VAR, RATATOSKR_VAR, VOLVA_VAR = "@FAFNIR_VAR@", "@HRUNGNIR_VAR@", "@RATATOSKR_VAR@", "@VOLVA_VAR@"
local _, ns = ...

local V = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/volva"]
if not V then
    print("|cffff5555" .. ADDON .. ": RefluxCore's collection/volva module is missing; the seeress is silent.|r")
    return
end

local function L(key)
    local c = ns and ns.catalogue
    if not c then return key end
    local t = c[GetLocale and GetLocale() or "enUS"]
    return (t and t[key]) or (c.enUS and c.enUS[key]) or key
end

local db = nil

local function character()
    local name = UnitName and UnitName("player")
    if not name then return nil end
    local realm = GetRealmName and GetRealmName()
    return (realm and realm ~= "") and (name .. "-" .. realm) or name
end

local function money(copper)
    return string.format("%dg %ds %dc", math.floor(copper / 10000), math.floor(copper / 100) % 100, copper % 100)
end

local function count_keys(t)
    local n = 0
    if type(t) == "table" then for _ in pairs(t) do n = n + 1 end end
    return n
end

-- The stat lines that can be filled right now, each {catalogue key, value}.
local function stat_offers()
    local offers = {}
    if GetMoney then offers[#offers + 1] = { "STAT_GOLD", money(GetMoney() or 0) } end
    if UnitLevel then offers[#offers + 1] = { "STAT_LEVEL", UnitLevel("player") or 1 } end
    local fafnir = RefluxCore.modules["collection/inventory"]
    if fafnir and sibling(FAFNIR_VAR) and sibling(FAFNIR_VAR).chars then
        offers[#offers + 1] = { "STAT_NET", fafnir.invTotals(sibling(FAFNIR_VAR))[3] }
    end
    local rock, census = sibling(HRUNGNIR_VAR), sibling(RATATOSKR_VAR)
    if rock and rock.level then offers[#offers + 1] = { "STAT_ROCK", rock.level } end
    if census and census.critters then offers[#offers + 1] = { "STAT_CRITTERS", count_keys(census.critters) } end
    offers[#offers + 1] = { "STAT_OPENED", (db.opened or 0) + 1 }
    return offers
end

local function text_of(last)
    if last.kind == 2 then return string.format(L(last.key), last.value) end
    return L("PLAIN_" .. last.index)
end

local function show(last)
    print(string.format(L("OPENED"), L("NAME"), text_of(last)))
    print(string.format(L("LUCKY"), table.concat(last.lucky, " ")))
end

local function open()
    local who = character()
    if not (db and who) then return end
    local offers = stat_offers()
    local r = V.fortuneOpen(db, who, time(), tonumber(L("PLAIN_COUNT")) or 0, #offers)
    local last = { kind = r[1], index = r[2], lucky = { r[3], r[4], r[5], r[6], r[7], r[8] } }
    if last.kind == 2 then
        local o = offers[last.index]
        last.key, last.value = o[1], o[2]
    end
    db.last = last
    show(last)
end

local f = CreateFrame("Frame")
f:RegisterEvent("ADDON_LOADED")
f:RegisterEvent("PLAYER_LOGIN")
f:SetScript("OnEvent", function(_, event, arg1)
    if event == "ADDON_LOADED" then
        if arg1 == ADDON then
            _G[VAR] = V.fortuneInit(_G[VAR] or {})
            db = _G[VAR]
        end
    elseif event == "PLAYER_LOGIN" then
        open()
    end
end)

SLASH_VOLVA1 = "/fortune"
SLASH_VOLVA2 = "/volva"
SlashCmdList["VOLVA"] = function()
    if not db then print(ADDON .. ": not loaded yet."); return end
    if db.last then show(db.last) else print(string.format(L("NONE_YET"), L("NAME"))) end
end
