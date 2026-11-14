-- runtime/wow/macrosync.lua -- MacroSync's host glue. The decisions are PARENA (stdlib/collection/macrosync.prn) and
-- the macro sets are PARENA (stdlib/collection/macros.prn); this file makes the client calls and speaks in haiku.
--
-- /ascend          load this character's class set: create or update managed macros, prune removed ones
-- /ascend preview  what /ascend would do, changing nothing
--
-- Client calls (retail-lineage; none probed on Forever yet, each feature-tested):
--   InCombatLockdown (macros cannot change in combat), GetNumMacros, GetMacroIndexByName, GetMacroBody,
--   CreateMacro(name, icon, body, perCharacter), EditMacro(index, name, icon, body), DeleteMacro(index),
--   MAX_CHARACTER_MACROS (the slot limit, read from the client -- never assumed), UnitClass,
--   C_Spell.GetSpellInfo or GetSpellInfo (which of a {A|B} choice this character knows).
-- Every macro is created per character with the question-mark icon, so #showtooltip shows the live spell.
-- A body the client shortens on save (its length limit is not confirmed) is caught by reading it back.

local ADDON = "@ADDON@"
local VAR = "@VAR@"
local LIMIT = tonumber("__MS_LIMIT__") or 0   -- filled by the packager from MACROSYNC_LIMIT
local _, ns = ...

local M = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/macrosync"]
local SETS = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/macros"]
if not (M and SETS) then
    print("|cffff5555" .. ADDON .. ": RefluxCore's MacroSync modules are missing.|r")
    return
end

local ICON = "INV_MISC_QUESTIONMARK"
local INK = "|cff8c6b27"            -- Valhöll gold

local function L(key)
    local c = ns and ns.catalogue
    if not c then return key end
    local t = c[GetLocale and GetLocale() or "enUS"]
    return (t and t[key]) or (c.enUS and c.enUS[key]) or key
end

local db = nil
local seed = 0

local function haiku(moment)
    seed = seed + 1
    local i = M.msHaiku(moment, (time and time() or 0) + seed)
    local text = L("H_" .. moment .. "_" .. i)
    for line in (text .. "/"):gmatch("%s*([^/]-)%s*/") do print(INK .. line .. "|r") end
end

local function spell_known(name)
    if C_Spell and C_Spell.GetSpellInfo then return C_Spell.GetSpellInfo(name) ~= nil end
    if GetSpellInfo then return GetSpellInfo(name) ~= nil end
    return false
end

-- Resolve {A|B} choices: the first spell this character knows, else the FIRST name listed -- every macro is
-- written either way, because the client looks a macro's spell up when it is pressed, so a macro for a spell not
-- learned yet simply starts working the day it is learned. Returns the body and the choices nobody knew.
local function resolve(template)
    local unlearned = {}
    local body = template:gsub("{([^}]*)}", function(alts)
        local first = nil
        for alt in alts:gmatch("[^|]+") do
            first = first or alt
            if spell_known(alt) then return alt end
        end
        unlearned[#unlearned + 1] = first
        return first or ""
    end)
    return body, unlearned
end

local function ascend(preview)
    if not db then return end
    if InCombatLockdown and InCombatLockdown() then haiku(5); M.msRefused_(1, 0); return end
    if not (CreateMacro and EditMacro and DeleteMacro and GetMacroIndexByName and GetNumMacros) then
        haiku(6); M.msRefused_(2, 0); return
    end
    local _, class = UnitClass("player")
    local set = SETS.macroSet(class or "")
    -- MACROSYNC_LIMIT (package time, 0 = the whole set): keep only the first N of the class set. Macros past the
    -- cap that MacroSync wrote earlier are pruned like any removed macro.
    if LIMIT > 0 and #set > LIMIT then
        local cut = {}
        for i = 1, LIMIT do cut[i] = set[i] end
        set = cut
    end
    if #set == 0 then haiku(8) end

    local keep, ready, dormant = {}, {}, {}
    for _, m in ipairs(set) do
        local body, unlearned = resolve(m[2])
        keep[m[1]] = 1
        ready[#ready + 1] = { m[1], body }
        if #unlearned > 0 then dormant[#dormant + 1] = m[1] .. " (" .. table.concat(unlearned, ", ") .. ")" end
    end

    local pruned, loaded, collided, full, short = {}, {}, {}, {}, {}
    for _, name in ipairs(M.msPruneList(db, keep)) do
        pruned[#pruned + 1] = name
        if not preview then
            local idx = GetMacroIndexByName(name)
            if idx and idx > 0 then DeleteMacro(idx) end
            M.msForget_(db, name)
        end
    end
    local max = type(MAX_CHARACTER_MACROS) == "number" and MAX_CHARACTER_MACROS or -1
    for _, r in ipairs(ready) do
        local name, body = r[1], r[2]
        local idx = GetMacroIndexByName(name) or 0
        if idx > 0 and M.msManaged(db, name) == 0 then
            collided[#collided + 1] = name
        elseif idx > 0 then
            if GetMacroBody(idx) ~= body then
                if not preview then EditMacro(idx, name, ICON, body) end
                loaded[#loaded + 1] = name
            end
        else
            -- When the client names its slot limit, check it first. When it doesn't (WoW Forever has no
            -- MAX_CHARACTER_MACROS), no number is assumed: the client itself is asked, and only its refusal
            -- counts as "full". A macro counts as loaded only once the client has actually created it.
            local _, perchar = GetNumMacros()
            if max >= 0 and M.msFits(perchar or 0, max, 1) == 0 then
                full[#full + 1] = name
            elseif preview then
                loaded[#loaded + 1] = name
            else
                local ok, made = pcall(CreateMacro, name, ICON, body, true)
                if ok and type(made) == "number" and made > 0 then
                    idx = made
                    M.msAdopt_(db, name)
                    loaded[#loaded + 1] = name
                else
                    idx = 0
                    full[#full + 1] = name
                end
            end
        end
        if not preview and idx and idx > 0 and M.msManaged(db, name) == 1 and GetMacroBody(idx) ~= body then
            short[#short + 1] = name
        end
    end

    if preview then
        print(string.format(L("PREVIEW"), ADDON, #loaded, #pruned, #dormant))
    else
        M.msAscended_(db, #loaded, #pruned, #dormant)
        haiku(1)
        print(string.format(L("SUMMARY"), ADDON, #ready, #loaded, #pruned, #dormant))
    end
    if #pruned > 0 then if not preview then haiku(2) end print("  " .. L("PRUNED") .. table.concat(pruned, ", ")) end
    if #dormant > 0 then if not preview then haiku(3) end print("  " .. L("DORMANT") .. table.concat(dormant, ", ")) end
    if #collided > 0 then haiku(4); print("  " .. L("COLLIDED") .. table.concat(collided, ", ")) end
    if #full > 0 then haiku(7); M.msRefused_(3, #full); print("  " .. L("FULL") .. table.concat(full, ", ")) end
    if #short > 0 then haiku(9); print("  " .. L("TOOLONG") .. table.concat(short, ", ")) end
end

local f = CreateFrame("Frame")
f:RegisterEvent("ADDON_LOADED")
f:SetScript("OnEvent", function(_, _, name)
    if name == ADDON then
        _G[VAR] = M.msInit(_G[VAR] or {})
        db = _G[VAR]
    end
end)

SLASH_MACROSYNC1 = "/ascend"
SlashCmdList["MACROSYNC"] = function(msg)
    msg = (msg or ""):match("^%s*(.-)%s*$"):lower()
    if msg == "preview" then ascend(true)
    elseif msg == "" then ascend(false)
    else print(L("HELP")) end
end
