-- tests/reflux_wow/macrosync_test.lua -- MacroSync in the packaged client, under real Lua 5.1, against a stubbed
-- macro list and spellbook. usage: lua5.1 macrosync_test.lua <client dir>
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
    for line in io.lines(dir .. "/" .. name .. ".toc") do
        if line ~= "" and not line:match("^#") then files[#files + 1] = line end
    end
    return files
end

-- the world: a macro list (account macros first, then character macros, like the client) and a spellbook
local function world(class, spells)
    local w = { out = {}, class = class, known = {}, combat = false, char = {}, account = {}, max = 18, cut = 255,
                saved = {}, now = 1763000000 }
    for _, s in ipairs(spells) do w.known[s] = true end
    return w
end

local function session(w)
    local env = { print = function(...)
        local t = {}
        for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
        w.out[#w.out + 1] = table.concat(t, " ")
    end }
    for _, k in ipairs({ "pairs", "ipairs", "type", "tostring", "tonumber", "select", "string", "table", "math",
                         "next", "error", "assert", "pcall", "unpack", "setmetatable", "getmetatable" }) do env[k] = _G[k] end
    env._G = env
    env.time = function() return w.now end
    env.GetLocale = function() return "enUS" end
    env.UnitName = function() return "Emily" end
    env.GetRealmName = function() return "Forever" end
    env.UnitClass = function() return w.class:sub(1, 1) .. w.class:sub(2):lower(), w.class end
    env.InCombatLockdown = function() return w.combat end
    env.MAX_CHARACTER_MACROS = w.max
    env.C_Spell = { GetSpellInfo = function(name) if w.known[name] then return { name = name } end end }
    local function all() local t = {}; for _, m in ipairs(w.account) do t[#t + 1] = m end; for _, m in ipairs(w.char) do t[#t + 1] = m end; return t end
    env.GetNumMacros = function() return #w.account, #w.char end
    env.GetMacroIndexByName = function(name) for i, m in ipairs(all()) do if m.name == name then return i end end return 0 end
    env.GetMacroBody = function(i) local m = all()[i]; return m and m.body end
    env.CreateMacro = function(name, icon, body, per)
        assert(per, "MacroSync makes character macros")
        assert(#w.char < w.max, "CreateMacro past the slot limit")
        w.char[#w.char + 1] = { name = name, icon = icon, body = body:sub(1, w.cut) }
        return #w.account + #w.char
    end
    env.EditMacro = function(i, name, icon, body) local m = all()[i]; m.name, m.icon, m.body = name, icon, body:sub(1, w.cut); return i end
    env.DeleteMacro = function(i)
        if i <= #w.account then table.remove(w.account, i) else table.remove(w.char, i - #w.account) end
    end
    local frames = {}
    env.CreateFrame = function()
        local f = { events = {}, scripts = {} }
        function f:RegisterEvent(e) self.events[e] = true end
        function f:SetScript(k, fn) self.scripts[k] = fn end
        frames[#frames + 1] = f
        return f
    end
    env.SlashCmdList = {}
    for k, v in pairs(w.saved) do env[k] = v end
    local s = { env = env }
    function s.load(addon)
        local dir = client .. "/AddOns/" .. addon
        local ns = {}
        for _, file in ipairs(toc_files(dir, addon)) do
            local chunk = assert(loadfile(dir .. "/" .. file)); setfenv(chunk, env); chunk(addon, ns)
        end
    end
    function s.fire(event, ...) for _, f in ipairs(frames) do if f.events[event] and f.scripts.OnEvent then f.scripts.OnEvent(f, event, ...) end end end
    function s.tick(dt) for _, f in ipairs(frames) do if f.scripts.OnUpdate then f.scripts.OnUpdate(f, dt) end end end
    function s.ascend(msg) w.out = {}; env.SlashCmdList.MACROSYNC(msg or ""); return table.concat(w.out, "\n") end
    for _, a in ipairs({ "RefluxCore", "CollectionJournal", "MacroSync" }) do s.load(a) end
    for _, a in ipairs({ "RefluxCore", "CollectionJournal", "MacroSync" }) do s.fire("ADDON_LOADED", a) end
    return s
end

local function names(list) local t = {}; for _, m in ipairs(list) do t[#t + 1] = m.name end; return table.concat(t, ",") end
local function find(list, name) for _, m in ipairs(list) do if m.name == name then return m end end end

-- ===== a level-1 paladin on a classic-named client: EVERY macro is written, learned or not =================
local w = world("PALADIN", { "Holy Light", "Seal of Righteousness", "Devotion Aura", "Judgement" })
w.account = { { name = "MyMount", body = "/cast Swift Brown Steed" } }
w.char = { { name = "PalStun", body = "/say my own stun macro" } }
local s = session(w)
local out = s.ascend("preview")
has(out, "would write 15, prune 0;", "preview counts what /ascend would do (16 in the set, one name is the player's)")
eq(#w.char, 1, "and changes nothing")
out = s.ascend()
eq(#w.char, 16, "all fifteen written beside the player's own macro, with nothing learned past level 1")
eq(names(w.char), "PalStun,PalJudge,PalSeal,PalHeal,PalBigHeal,PalBubble,PalBubHearth,PalLoH,PalBless,PalBoP,PalFreedom,PalCleanse,PalAura,PalTaunt,PalExo,PalMount",
   "in the set's order")
eq(find(w.char, "PalJudge").body, "#showtooltip\n/startattack\n/cast Judgement", "the known spelling is chosen")
eq(find(w.char, "PalHeal").body:find("Holy Light", 1, true) ~= nil, true, "Flash of Light unknown, Holy Light known: Holy Light")
eq(find(w.char, "PalLoH").body, "#showtooltip\n/cast [@mouseover,help,nodead][help,nodead][@player] Lay on Hands",
   "an unlearned spell is written by its first name, ready for the day it is learned")
eq(find(w.char, "PalSeal").body, "#showtooltip\n/cast [mod:shift] Seal of the Crusader; Seal of Righteousness",
   "one choice unlearned, the other known: both resolve")
eq(find(w.char, "PalStun").body, "/say my own stun macro", "the player's own macro is untouched")
eq(names(w.account), "MyMount", "account macros are untouched")
has(out, "15 written, 0 pruned;", "summary line")
has(out, "not learned yet (already on your list, they work once learned): ", "unlearned spells are listed")
has(out, "PalLoH (Lay on Hands)", "by macro and spell")
local lines = 0
for _, l in ipairs(w.out) do if l:find("|cff8c6b27", 1, true) then lines = lines + 1 end end
eq(lines >= 6, true, "the ascend and the not-learned haiku print, three lines each")
eq(s.env.MacroSyncDB.managed.PalJudge, 1, "MacroSync remembers what it manages")
eq(s.env.MacroSyncDB.managed.PalStun, nil, "but not the player's own")

-- ===== later: spells learned -- the choices upgrade; a macro dropped from the set is pruned =================
w.saved.MacroSyncDB = s.env.MacroSyncDB
for _, sp in ipairs({ "Hammer of Justice", "Seal of the Crusader", "Retribution Aura", "Flash of Light", "Lay on Hands" }) do w.known[sp] = true end
w.char[#w.char + 1] = { name = "PalOldOne", body = "/cast Old Spell" }
w.saved.MacroSyncDB.managed.PalOldOne = 1          -- a macro an older set had
s = session(w)
out = s.ascend()
eq(find(w.char, "PalOldOne"), nil, "a managed macro no longer in the set is pruned")
has(out, "pruned: PalOldOne", "and the prune is reported")
eq(find(w.char, "PalHeal").body:find("Flash of Light", 1, true) ~= nil, true, "PalHeal upgraded to Flash of Light")
eq(find(w.char, "PalStun").body, "/say my own stun macro", "a name collision leaves the player's macro alone")
has(out, "left alone (your own macro has the name): PalStun", "and says so")
local before = names(w.char)
out = s.ascend()
has(out, "0 written, 0 pruned", "a second /ascend has nothing to do")
eq(names(w.char), before, "and changes nothing")

-- ===== a retail-named client chooses the other spelling ==================================================
local r = world("PALADIN", { "Judgment", "Holy Light" })
s = session(r)
s.ascend()
eq(find(r.char, "PalJudge").body, "#showtooltip\n/startattack\n/cast Judgment", "the retail spelling was chosen")

-- ===== combat, full slots, a short body limit, another class ===========================================
local c = world("PALADIN", { "Judgement", "Holy Light" })
c.combat = true
s = session(c)
out = s.ascend()
eq(#c.char, 0, "nothing changes in combat")
eq(out:find("no macro may change in war", 1, true) ~= nil or out:find("will not move in fights", 1, true) ~= nil, true, "and a combat haiku says so")

local f = world("PALADIN", { "Judgement", "Holy Light", "Devotion Aura" })
for i = 1, 17 do f.char[i] = { name = "Mine" .. i, body = "/wave" } end
s = session(f)
out = s.ascend()
eq(#f.char, 18, "only the one free slot is used")
has(out, "no slot free for: ", "the rest are reported, never forced")

local t = world("PALADIN", { "Judgement" })
t.cut = 20
s = session(t)
out = s.ascend()
has(out, "the client shortened: PalJudge", "a body the client cut is caught by reading it back")

local k = world("WARLOCK", { "Shadow Bolt" })
s = session(k)
out = s.ascend()
has(out, "the Flame holds no words for you", "a class with no set yet is told so")
eq(#k.char, 0, "and nothing is written")

-- ===== the shaman's panic button: War Stomp then heals on a Tauren, heals all the way on anyone else ==========
local tau = world("SHAMAN", { "War Stomp", "Lesser Healing Wave", "Healing Wave" })
s = session(tau)
s.ascend()
eq(find(tau.char, "ShaPanic").body, "#showtooltip\n/castsequence [@player] reset=combat/15 War Stomp, Lesser Healing Wave, Lesser Healing Wave",
   "a Tauren shaman stomps first, then heals twice")
local orc = world("SHAMAN", { "Healing Wave" })
s = session(orc)
s.ascend()
eq(find(orc.char, "ShaPanic").body, "#showtooltip\n/castsequence [@player] reset=combat/15 Healing Wave, Healing Wave, Healing Wave",
   "a shaman with no War Stomp (and no Lesser Healing Wave yet) just heals")

-- ===== every heal, in every class's set, can be cast on whoever is under the mouse =========================
-- (founder, 2026-11-14: "all the classes with healing need mouseover macros for their heals"). The panic button is
-- the one heal on yourself by design; totems are not cast on anyone.
do
    local heals = { "Heal", "Holy Light", "Flash of Light", "Lay on Hands", "Renew", "Healing Wave", "Healing Surge",
                    "Chain Heal", "Healing Touch", "Regrowth", "Rejuvenation", "Power Word: Shield", "Resurrection",
                    "Redemption", "Ancestral Spirit", "Rebirth" }
    local ns = {}
    local chunk = assert(loadfile(client .. "/AddOns/MacroSync/macros.lua"))
    local fake = { RefluxCore = { modules = {} } }
    setfenv(chunk, setmetatable(fake, { __index = _G }))
    chunk("MacroSync", ns)
    local S = fake.RefluxCore.modules["collection/macros"]
    local checked = 0
    for _, class in ipairs(S.macroClasses()) do
        for _, m in ipairs(S.macroSet(class)) do
            local name, body = m[1], m[2]
            local heals_someone = false
            for _, h in ipairs(heals) do if body:find(h, 1, true) then heals_someone = true end end
            if heals_someone and name ~= "ShaPanic" and not name:find("Totem", 1, true) then
                checked = checked + 1
                eq(body:find("[@mouseover,help", 1, true) ~= nil, true, class .. " " .. name .. " casts on the mouseover")
            end
        end
    end
    eq(checked >= 14, true, "every healing class's heals were checked (" .. checked .. ")")
end
do
    local pal = world("PALADIN", { "Holy Light", "Redemption" })
    local ps = session(pal)
    ps.ascend()
    eq(find(pal.char, "PalBigHeal").body, "#showtooltip\n/cast [@mouseover,help,dead][help,dead] Redemption; [@mouseover,help,nodead][help,nodead][@player] Holy Light",
       "the paladin's big heal raises a dead mouseover, and heals a living one")
end

-- ===== a level-1 priest ===============================================================================
local pr = world("PRIEST", { "Smite", "Lesser Heal", "Power Word: Fortitude", "Shoot" })
s = session(pr)
out = s.ascend()
eq(#pr.char, 16, "a level-1 priest gets all sixteen, learned or not")
eq(find(pr.char, "PriHeal").body:find("Lesser Heal", 1, true) ~= nil, true, "the quick heal falls back to Lesser Heal")
eq(find(pr.char, "PriBigHeal").body:find("Lesser Heal", 1, true) ~= nil, true, "so does the big heal, until a bigger one is known")
eq(find(pr.char, "PriWand").body, "#showtooltip\n/cast !Shoot", "the wand keeps its no-toggle !")
has(out, "16 written, 0 pruned; 11 use spells you have not learned yet", "and is told which wait on learning")
eq(find(pr.char, "PriShield").body:find("Power Word: Shield", 1, true) ~= nil, true, "the shield macro is there before the shield is learned")
pr.known["Greater Heal"], pr.known["Power Word: Shield"] = true, true
pr.saved.MacroSyncDB = s.env.MacroSyncDB
s = session(pr)
s.ascend()
eq(find(pr.char, "PriBigHeal").body:find("Greater Heal", 1, true) ~= nil, true, "learning Greater Heal upgrades the big heal")
eq(#pr.char, 16, "and nothing is duplicated")

-- ===== mage, shaman, druid: every set is complete on a fresh character, and choices pick the best known ======
for _, case in ipairs({
    { "MAGE", { "Fireball", "Frost Armor" }, "MagArmor", "Frost Armor" },
    { "SHAMAN", { "Lightning Bolt", "Rockbiter Weapon", "Healing Wave" }, "ShaWeapon", "Rockbiter Weapon" },
    { "DRUID", { "Wrath", "Healing Touch", "Mark of the Wild" }, "DruMark", "Mark of the Wild" },
    { "WARRIOR", { "Heroic Strike", "Battle Stance", "Battle Shout", "Charge" }, "WarStrike", "Heroic Strike" },
    { "ROGUE", { "Sinister Strike", "Stealth", "Eviscerate" }, "RogSinister", "Sinister Strike" },
    { "HUNTER", { "Auto Shot", "Raptor Strike", "Aspect of the Monkey", "Track Beasts" }, "HunAspect", "Aspect of the Monkey" },
}) do
    local cw = world(case[1], case[2])
    s = session(cw)
    out = s.ascend()
    local want = case[1] == "SHAMAN" and 17 or 16       -- the shaman set also has the panic button
    eq(#cw.char, want, case[1] .. ": the whole set written on a fresh character")
    eq(find(cw.char, case[3]).body:find(case[4], 1, true) ~= nil, true, case[1] .. ": " .. case[3] .. " uses the known " .. case[4])
    local named = true
    for _, m in ipairs(cw.char) do if #m.name > 16 then named = false end end
    eq(named, true, case[1] .. ": every name fits the macro frame's 16 characters")
end
local dr = world("DRUID", {})
s = session(dr)
s.ascend()
eq(find(dr.char, "DruCaster").body, "/cancelform", "a macro with no spell choice is written as is")

-- ===== warrior, rogue, hunter: the class-specific conditionals survive resolution ==========================
local wa = world("WARRIOR", { "Bloodthirst", "Heroic Strike", "Charge", "Battle Stance" })
s = session(wa); s.ascend()
eq(find(wa.char, "WarStrike").body, "#showtooltip\n/startattack\n/cast Bloodthirst", "a fury warrior's strike is Bloodthirst, not Heroic Strike")
eq(find(wa.char, "WarCharge").body, "#showtooltip\n/startattack\n/cast [stance:3] Intercept; Charge", "Charge, Intercept in Berserker stance")
local ro = world("ROGUE", { "Sinister Strike", "Stealth" })
s = session(ro); s.ascend()
eq(find(ro.char, "RogStealth").body, "#showtooltip\n/cast [nostealth] Stealth", "Stealth never toggles you out")
eq(find(ro.char, "RogOpener").body, "#showtooltip\n/cast [stealth,mod:shift] Ambush; [stealth] Cheap Shot; Sinister Strike", "the stealth openers")
local hu = world("HUNTER", { "Auto Shot", "Call Pet" })
s = session(hu); s.ascend()
eq(find(hu.char, "HunAuto").body, "#showtooltip\n/cast !Auto Shot", "Auto Shot keeps its no-toggle !")
eq(find(hu.char, "HunPet").body, "#showtooltip\n/cast [@pet,dead] Revive Pet; [nopet] Call Pet; Mend Pet", "one pet button: revive, call, mend")
eq(find(hu.char, "HunPetAttack").body, "/petattack [@mouseover,harm,nodead][]", "pet attack has no spell choice")

-- ===== every class's set is built in, and fits the client ==================================================
local SETS = s.env.RefluxCore.modules["collection/macros"]
local classes = SETS.macroClasses()
eq(table.concat(classes, ","), "PALADIN,PRIEST,MAGE,SHAMAN,DRUID,WARRIOR,ROGUE,HUNTER", "eight classes have a set")
for _, class in ipairs(classes) do
    local set, names_ok, tag_ok, len_ok, uniq = SETS.macroSet(class), true, true, true, {}
    local tag = set[1] and set[1][1]:sub(1, 3)
    for _, m in ipairs(set) do
        if #m[1] > 16 or uniq[m[1]] then names_ok = false end
        uniq[m[1]] = true
        if m[1]:sub(1, 3) ~= tag then tag_ok = false end
        -- the longest body this template can resolve to: every choice its longest alternative
        local longest = m[2]:gsub("{([^}]*)}", function(alts)
            local best = ""
            for alt in alts:gmatch("[^|]+") do if #alt > #best then best = alt end end
            return best
        end)
        if #longest > 255 then len_ok = false end
    end
    eq(#set, class == "SHAMAN" and 17 or 16, class .. ": the set's size (shaman: sixteen and the panic button)")
    eq(names_ok, true, class .. ": names unique and at most 16 characters")
    eq(tag_ok, true, class .. ": every name carries the class tag " .. tostring(tag))
    eq(len_ok, true, class .. ": every body fits 255 characters")
end

-- ===== REFLUX: ascends reach the journal ================================================================
local j = world("PALADIN", { "Judgement" })
s = session(j)
s.ascend()
s.tick(10)
local jdb = s.env.CollectionJournalDB
local seen = 0
for seq = jdb.firstSeq, jdb.nextSeq - 1 do if jdb.rows[seq].type == 3000 then seen = seen + 1 end end
eq(seen, 1, "one 3000 ascended row in the journal")

-- ===== every haiku is 5-7-5, by Bragi's Verse's own syllable counter ===================================
local benv = { string = string, table = table, math = math, next = next, tostring = tostring, tonumber = tonumber,
               type = type, pairs = pairs, ipairs = ipairs, RefluxCore = { modules = {} } }
local chunk = assert(loadfile(client .. "/AddOns/BragisVerse/bragi_rules.lua")); setfenv(chunk, benv); chunk()
local B = benv.RefluxCore.modules["collection/bragi"]
local ns = {}; assert(loadfile(client .. "/AddOns/MacroSync/locale.lua"))("MacroSync", ns)
local count = 0
for key, text in pairs(ns.catalogue.enUS) do
    if key:match("^H_") then
        local c = {}
        for line in (text .. "/"):gmatch("%s*([^/]-)%s*/") do c[#c + 1] = B.bragiSyllables(line) end
        eq(table.concat(c, "-"), "5-7-5", key .. " is a haiku")
        count = count + 1
    end
end
eq(count, 14, "fourteen haiku")

print(string.format("macrosync: %d passed, %d failed", passed, failed))
os.exit(failed == 0 and 0 or 1)
