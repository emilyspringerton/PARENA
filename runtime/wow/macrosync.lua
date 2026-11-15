-- runtime/wow/macrosync.lua -- MacroSync's host glue. The decisions are PARENA (stdlib/collection/macrosync.prn) and
-- the macro sets are PARENA (stdlib/collection/macros.prn); this file makes the client calls and speaks in haiku.
--
-- /ascend          load this character's kit: create or update managed macros, prune removed ones
-- /ascend preview  what /ascend would do, changing nothing
-- /ascend bars off  leave the action bars alone from now on
-- /ascend bars on   put the macros on empty action slots again (the default)
-- /ascend firstaid off  stop FirstAid following your bags, skill and level
-- /ascend firstaid on   let it follow them again (the default)
-- /firstaid        what the universal FirstAid macro holds for this character, and why
--
-- FirstAid (stdlib/collection/firstaid.prn): one ACCOUNT macro for every class, rewritten for whoever is logged in
-- whenever their bags, First Aid skill or level change -- the best bandage they carry and can use (on a mouseover or
-- friendly target, else themselves), shift for the best healing potion they can drink, else open First Aid. Never in
-- combat (the client forbids it): a change waits for the fight to end. Item calls: GetItemCount (C_Item.GetItemCount),
-- UnitLevel, GetNumSkillLines/GetSkillLineInfo (the First Aid rank), MAX_ACCOUNT_MACROS, and Recently Bandaged through
-- C_UnitAuras.GetAuraDataByIndex or UnitDebuff (+ GetTime): while it lasts, a plain press bandages a friend or drinks a
-- potion on you, instead of a bandage the game would refuse.
--
-- The kit is the class set, changed by the plan made for this character in the plugin forge (runtime/wow/forge/,
-- baked into kits.lua at build time; stdlib/collection/kits.prn applies it). No plan: the class set as shipped.
--
-- Client calls (retail-lineage; none probed on Forever yet, each feature-tested):
--   InCombatLockdown (macros cannot change in combat), GetNumMacros, GetMacroIndexByName, GetMacroBody,
--   CreateMacro(name, icon, body, perCharacter), EditMacro(index, name, icon, body), DeleteMacro(index),
--   MAX_CHARACTER_MACROS (the slot limit, read from the client -- never assumed), UnitClass,
--   C_Spell.GetSpellInfo or GetSpellInfo (which of a {A|B} choice this character knows).
--   Action bars: HasAction, GetActionInfo, GetMacroInfo, PickupMacro, PickupAction, PlaceAction, ClearCursor,
--   MultiBarBottomLeft/MultiBarBottomRight (only to say when the bars it filled are switched off).
-- Every macro is created per character with the question-mark icon, so #showtooltip shows the live spell.
-- After writing, each macro that is on no action bar is put on one (stdlib/collection/macrosync.prn ms-bar-plan
-- decides where: a slot planned in the forge, else the first empty slot of the bottom bars, then the right bars, then
-- the main bar; never over a spell, item or macro the player placed).
-- A body the client shortens on save (its length limit is not confirmed) is caught by reading it back.

local ADDON = "@ADDON@"
local VAR = "@VAR@"
local _, ns = ...

local M = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/macrosync"]
local SETS = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/macros"]
local KITS = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/kits"]
local FA = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/firstaid"]
MacroSyncKits = ns and ns.kits or {}      -- the planned kits, for the Collection's character screen
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

-- The plan the forge made for this character (Name-Realm, as the Character Board and the forge key it), if any.
local function this_plan()
    local kits = ns and ns.kits
    if type(kits) ~= "table" then return nil end
    local name = UnitName and UnitName("player")
    local realm = GetRealmName and GetRealmName()
    if not name then return nil end
    return kits[(realm and realm ~= "") and (name .. "-" .. realm) or name]
end

-- kitApply fills a plan's missing parts in; never let it touch the baked table itself.
local function copy(t)
    if type(t) ~= "table" then return t end
    local c = {}
    for k, v in pairs(t) do c[k] = copy(v) end
    return c
end

-- ---- FirstAid: the universal macro ---------------------------------------------------------------------------------
-- Is the macro called `name` MacroSync's FirstAid (named so, and carrying the marker line)?
local function fa_is_ours(name)
    if not (FA and name == FA.faMacroName() and GetMacroBody) then return false end
    local idx = GetMacroIndexByName and GetMacroIndexByName(name) or 0
    return idx > 0 and FA.faOurs(GetMacroBody(idx)) == 1
end

local function item_count(id)
    if C_Item and C_Item.GetItemCount then return C_Item.GetItemCount(id) or 0 end
    if GetItemCount then return GetItemCount(id) or 0 end
    return 0
end

-- The First Aid rank: the skill line named First Aid in any client language; 0 when the character has not learned it;
-- -1 when the client cannot say (then every bandage carried counts as usable).
local function first_aid_rank()
    if not (GetNumSkillLines and GetSkillLineInfo) then return -1 end
    local names = (ns and ns.first_aid_names) or { "First Aid" }
    for i = 1, GetNumSkillLines() do
        local name, header, _, rank = GetSkillLineInfo(i)
        if name and not header then
            for _, n in ipairs(names) do if name == n then return rank or 0 end end
        end
    end
    return 0
end

-- Recently Bandaged: seconds left (0 = not), found by spell id, else by name in any client language.
local function recently_bandaged()
    local names = (ns and ns.recently_bandaged_names) or { "Recently Bandaged" }
    local function check(name, expires, id)
        if not name then return nil end
        local hit = id == FA.faRecentSpell()
        if not hit then for _, n in ipairs(names) do if name == n then hit = true; break end end end
        if hit then
            local left = (expires and expires > 0 and GetTime) and (expires - GetTime()) or 60
            return left > 0 and left or 0
        end
    end
    if C_UnitAuras and C_UnitAuras.GetAuraDataByIndex then
        for i = 1, 40 do
            local a = C_UnitAuras.GetAuraDataByIndex("player", i, "HARMFUL")
            if not a then break end
            local left = check(a.name, a.expirationTime, a.spellId)
            if left then return left end
        end
        return 0
    end
    if UnitDebuff then
        for i = 1, 40 do
            local name, _, _, _, _, expires, _, _, _, id = UnitDebuff("player", i)
            if not name then break end
            local left = check(name, expires, id)
            if left then return left end
        end
    end
    return 0
end

-- What FirstAid should hold now: the body and the choice, {body, bandage row, potion row, skill, level, counts}.
local function fa_plan()
    local bc, pc = {}, {}
    for i = 0, FA.faBandageCount() - 1 do bc[i + 1] = item_count(FA.faBandageId(i)) end
    for i = 0, FA.faPotionCount() - 1 do pc[i + 1] = item_count(FA.faPotionId(i)) end
    local skill = first_aid_rank()
    local level = UnitLevel and UnitLevel("player") or 1
    local b = FA.faBestBandage(bc, skill)
    local p = FA.faBestPotion(pc, level)
    local spell = (ns and ns.first_aid_names and ns.first_aid_names[1]) or "First Aid"
    for _, n in ipairs((ns and ns.first_aid_names) or {}) do if spell_known(n) then spell = n; break end end
    local recent = recently_bandaged()
    return { body = FA.faBody(b, p, spell, recent > 0 and 1 or 0), b = b, p = p, skill = skill, level = level,
             bc = bc, pc = pc, recent = recent }
end

local fa_pending, fa_collided = false, false

-- Write FirstAid if it should change. quiet: the automatic refresh (bags, skill, level) says nothing unless it could not.
-- Returns "written", "same", "combat", "collided", "full" or "none" (no macro API / module).
local function fa_write(quiet)
    if not (FA and CreateMacro and EditMacro and GetMacroIndexByName and GetNumMacros and GetMacroBody) then return "none" end
    if InCombatLockdown and InCombatLockdown() then fa_pending = true; return "combat" end
    fa_pending = false
    local plan = fa_plan()
    local name = FA.faMacroName()
    local idx = GetMacroIndexByName(name) or 0
    if idx > 0 then
        local body = GetMacroBody(idx)
        if FA.faOurs(body) == 0 then
            if not fa_collided and not quiet then print("  " .. L("FA_COLLIDED")) end
            fa_collided = true
            return "collided"
        end
        if body == plan.body then return "same" end
        EditMacro(idx, name, ICON, plan.body)
        return "written"
    end
    local account = GetNumMacros()
    local max = type(MAX_ACCOUNT_MACROS) == "number" and MAX_ACCOUNT_MACROS or -1
    if M.msFits(account or 0, max, 1) == 0 then
        if not quiet then print("  " .. L("FA_FULL")) end
        return "full"
    end
    CreateMacro(name, ICON, plan.body, false)
    return "written"
end

-- /firstaid: the choice, in words
local function fa_explain()
    if not FA then return end
    local plan = fa_plan()
    local function item(id) return (GetItemInfo and GetItemInfo(id)) or ("item " .. id) end
    local skill = plan.skill < 0 and L("FA_SKILL_UNKNOWN") or tostring(plan.skill)
    if plan.b >= 0 then
        print(string.format(L("FA_BANDAGE"), item(FA.faBandageId(plan.b)), plan.bc[plan.b + 1], FA.faBandageHeal(plan.b), skill))
    else
        local carried, best = 0, nil
        for i = 0, FA.faBandageCount() - 1 do if plan.bc[i + 1] > 0 then carried = carried + plan.bc[i + 1]; best = i end end
        if carried > 0 then
            print(string.format(L("FA_BANDAGE_LOCKED"), item(FA.faBandageId(best)), FA.faBandageSkill(best), skill))
        else
            print(L("FA_NO_BANDAGE"))
        end
    end
    -- a carried bandage the skill cannot use yet is worth saying even when a lesser one was chosen
    for i = FA.faBandageCount() - 1, (plan.b >= 0 and plan.b + 1 or FA.faBandageCount()), -1 do
        if plan.bc[i + 1] > 0 and plan.skill >= 0 and plan.skill < FA.faBandageSkill(i) then
            print("  " .. string.format(L("FA_SKILL_UP"), item(FA.faBandageId(i)), FA.faBandageSkill(i))); break
        end
    end
    if plan.p >= 0 then
        print(string.format(L(plan.b >= 0 and "FA_POTION_SHIFT" or "FA_POTION"), item(FA.faPotionId(plan.p)), plan.pc[plan.p + 1]))
    end
    if plan.b < 0 and plan.p < 0 then print(L("FA_OPENS")) end
    if plan.recent > 0 and plan.b >= 0 then
        print(string.format(L(plan.p >= 0 and "FA_RECENT" or "FA_RECENT_NOPOT"), math.ceil(plan.recent)))
    end
    local r = fa_write(false)
    if r == "written" then print("  " .. L("FA_WRITTEN"))
    elseif r == "combat" then print("  " .. L("FA_COMBAT")) end
end

-- ---- the action bars ---------------------------------------------------------------------------------------------
-- What every slot holds now: state {[slot] = 1 (not ours) or our macro's name}, where {[name] = its first slot}.
local function bar_state()
    if not (HasAction and GetActionInfo) then return nil end
    local state, where = {}, {}
    for slot = 1, 120 do
        if HasAction(slot) then
            local kind, id = GetActionInfo(slot)
            local name = kind == "macro" and id and GetMacroInfo and GetMacroInfo(id)
            if name and (M.msManaged(db, name) == 1 or fa_is_ours(name)) then
                state[slot] = name
                where[name] = where[name] or slot
            else
                state[slot] = 1
            end
        end
    end
    return state, where
end

local function place_on_bars(names, plan, preview)
    if db.bars == false then return end
    if not (PickupMacro and PlaceAction) then return end
    local state, where = bar_state()
    if not state then return end
    local steps = M.msBarPlan(names, where, plan and plan.bars or {}, state)
    local placed, taken, full, bottom = {}, {}, {}, false
    for _, st in ipairs(steps) do
        local name, slot, kind, from = st[1], st[2], st[3], st[4]
        if kind == 1 or kind == 2 or kind == 5 then
            if not preview then
                if kind == 5 then PickupAction(from) else PickupMacro(name) end
                PlaceAction(slot)
                if ClearCursor then ClearCursor() end
            end
            placed[#placed + 1] = name .. " " .. slot
            if slot >= 49 and slot <= 72 then bottom = true end
        elseif kind == 3 then taken[#taken + 1] = name .. " (slot " .. slot .. ")"
        else full[#full + 1] = name end
    end
    if #placed > 0 then print("  " .. string.format(L(preview and "BARS_WOULD" or "BARS"), #placed) .. table.concat(placed, ", ")) end
    if #taken > 0 then print("  " .. L("BARS_TAKEN") .. table.concat(taken, ", ")) end
    if #full > 0 then print("  " .. L("BARS_FULL") .. table.concat(full, ", ")) end
    if bottom and not preview then
        local bl = MultiBarBottomLeft and MultiBarBottomLeft.IsShown and not MultiBarBottomLeft:IsShown()
        local br = MultiBarBottomRight and MultiBarBottomRight.IsShown and not MultiBarBottomRight:IsShown()
        if bl or br then print("  " .. L("BARS_HIDDEN")) end
    end
end

local function ascend(preview)
    if not db then return end
    if InCombatLockdown and InCombatLockdown() then haiku(5); M.msRefused_(1, 0); return end
    if not (CreateMacro and EditMacro and DeleteMacro and GetMacroIndexByName and GetNumMacros) then
        haiku(6); M.msRefused_(2, 0); return
    end
    local _, class = UnitClass("player")
    local set = SETS.macroSet(class or "")
    local plan = this_plan()
    if plan and KITS then
        set = KITS.kitApply(set, copy(plan))
        print(string.format(L("KIT"), ADDON, KITS.kitChanges(copy(plan))))
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
            local _, perchar = GetNumMacros()
            if M.msFits(perchar or 0, max, 1) == 0 then
                full[#full + 1] = name
            else
                if not preview then
                    idx = CreateMacro(name, ICON, body, true)
                    if idx and idx > 0 then M.msAdopt_(db, name) end
                end
                loaded[#loaded + 1] = name
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
    -- FirstAid, the universal one (an account macro, so it never takes one of the class set's slots)
    if FA and not preview then
        local r = fa_write(false)
        if r == "written" then print("  " .. L("FA_WRITTEN")) end
    end
    -- then the action bars: every macro of the kit that exists now (or would, in a preview), and FirstAid
    local names = {}
    for _, r in ipairs(ready) do
        local exists = GetMacroIndexByName(r[1]) or 0
        if exists > 0 or preview then names[#names + 1] = r[1] end
    end
    if FA and (fa_is_ours(FA.faMacroName()) or preview) then names[#names + 1] = FA.faMacroName() end
    place_on_bars(names, plan, preview)
end

-- FirstAid stays current by itself: bags settle in bursts, so a change is written half a second after the last one.
local FA_EVENTS = { "PLAYER_ENTERING_WORLD", "BAG_UPDATE", "BAG_UPDATE_DELAYED", "SKILL_LINES_CHANGED", "PLAYER_LEVEL_UP",
                    "PLAYER_REGEN_ENABLED", "UNIT_AURA" }
local fa_due = nil
local f = CreateFrame("Frame")
f:RegisterEvent("ADDON_LOADED")
for _, e in ipairs(FA_EVENTS) do f:RegisterEvent(e) end
f:SetScript("OnEvent", function(_, event, name)
    if event == "ADDON_LOADED" then
        if name == ADDON then
            _G[VAR] = M.msInit(_G[VAR] or {})
            db = _G[VAR]
        end
    elseif db and FA and db.firstaid ~= false then
        if event == "PLAYER_REGEN_ENABLED" and not fa_pending then return end
        if event == "UNIT_AURA" and name ~= "player" then return end
        fa_due = 0.5
    end
end)
local fa_timer = CreateFrame("Frame")   -- its own frame: the timer never shares a frame with the event handler
fa_timer:SetScript("OnUpdate", function(_, dt)
    if not fa_due then return end
    fa_due = fa_due - (dt or 0)
    if fa_due <= 0 then fa_due = nil; fa_write(true) end
end)

SLASH_MACROSYNC1 = "/ascend"
SlashCmdList["MACROSYNC"] = function(msg)
    msg = (msg or ""):match("^%s*(.-)%s*$"):lower()
    if msg == "bars off" then db.bars = false; print(L("BARS_OFF"))
    elseif msg == "bars on" then db.bars = nil; print(L("BARS_ON"))
    elseif msg == "preview" then ascend(true)
    elseif msg == "firstaid off" then db.firstaid = false; print(L("FA_OFF"))
    elseif msg == "firstaid on" then db.firstaid = nil; print(L("FA_ON")); fa_write(false)
    elseif msg == "" then ascend(false)
    else print(L("HELP")) end
end

SLASH_MACROSYNCFIRSTAID1 = "/firstaid"
SlashCmdList["MACROSYNCFIRSTAID"] = function() fa_explain() end
