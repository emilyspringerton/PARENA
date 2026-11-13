-- runtime/wow/norns_draw.lua -- Norns' Draw's host glue (spec addon 66, gnome bingo). Rules: stdlib/collection/norns.prn.
-- /bingo new    deal a fresh card from the 30 events below
-- /bingo        show the card ([x] marked; the centre is free)
-- Each player runs their own card; a BINGO is announced to the group.
--
-- The events, by number (catalogue E<n> / S<n> name them), and what detects them:
--   1 you jump (JumpOrAscendStart)       2-6 anyone /dance, /wave, /cheer, /roar, /laugh (text emotes, kit patterns)
--   7-13 a rogue, mage, warlock, paladin, hunter, druid, priest player is seen (nameplate, target or mouseover)
--   14-20 a gnome, dwarf, tauren, undead, troll, night elf, orc player is seen (the race's file name, not localised)
--   21 you loot a grey item   22 you loot a green item (CHAT_MSG_LOOT on your own line, item quality)
--   23 quest turned in   24 you die   25 you level   26 new mail   27 you enter combat
--   28 you target a critter (the catalogue's CRITTER_TYPE)   29 someone says "lol"   30 your gold goes up
local ADDON, VAR = "@ADDON@", "@VAR@"
local _, ns = ...
local K = ns.kit
local G = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/norns"]
if not G then print(ADDON .. ": RefluxCore's collection/norns module is missing.") return end
K.saved(VAR, G.nornsInit)

local POOL = 30
local CLASSES = { ROGUE = 7, MAGE = 8, WARLOCK = 9, PALADIN = 10, HUNTER = 11, DRUID = 12, PRIEST = 13 }
local RACES = { Gnome = 14, Dwarf = 15, Tauren = 16, Scourge = 17, Troll = 18, NightElf = 19, Orc = 20 }
local EMOTE_EVENT = { DANCE = 2, WAVE = 3, CHEER = 4, ROAR = 5, LAUGH = 6 }

local function happened(ev)
    if not (K.db and K.db.card) then return end
    if G.nornsMark_(K.db, ev, K.now()) == 2 then
        K.notice(K.f("BINGO", K.L("NAME")))
        K.say(K.f("BINGO_SAY", K.L("NAME"), UnitName("player"), K.L("E" .. ev)))
    end
end

local function saw(unit)
    if not (UnitExists and UnitExists(unit)) then return end
    if UnitIsPlayer and UnitIsPlayer(unit) then
        local _, class = UnitClass(unit)
        if class and CLASSES[class] then happened(CLASSES[class]) end
        local _, race = UnitRace(unit)
        if race and RACES[race] then happened(RACES[race]) end
    end
end

local money = nil
local hooked = false
K.on("PLAYER_LOGIN", function()
    money = GetMoney and GetMoney() or 0
    if not hooked and hooksecurefunc and JumpOrAscendStart then
        hooksecurefunc("JumpOrAscendStart", function() happened(1) end); hooked = true
    end
end)
K.on_emote(function(tok) if EMOTE_EVENT[tok] then happened(EMOTE_EVENT[tok]) end end)
K.on("NAME_PLATE_UNIT_ADDED", function(unit) saw(unit) end)
K.on("UPDATE_MOUSEOVER_UNIT", function() saw("mouseover") end)
K.on("PLAYER_TARGET_CHANGED", function()
    saw("target")
    if UnitCreatureType and UnitCreatureType("target") == K.L("CRITTER_TYPE") then happened(28) end
end)
K.on("CHAT_MSG_LOOT", function(text)
    if type(text) ~= "string" or not GetItemInfo then return end
    local mine = LOOT_ITEM_SELF and text:find((LOOT_ITEM_SELF:gsub("%%s.*", "")), 1, true) == 1
    if not mine then return end
    local link = text:match("|H(item:[^|]+)|h")
    local q = link and select(3, GetItemInfo(link))
    if q == 0 then happened(21) elseif q == 2 then happened(22) end
end)
K.on("QUEST_TURNED_IN", function() happened(23) end)
K.on("PLAYER_DEAD", function() happened(24) end)
K.on("PLAYER_LEVEL_UP", function() happened(25) end)
K.on("UPDATE_PENDING_MAIL", function() if HasNewMail and HasNewMail() then happened(26) end end)
K.on("PLAYER_REGEN_DISABLED", function() happened(27) end)
K.on_chat(function(text) if text:lower():find("%f[%a]lol%f[%A]") then happened(29) end end)
K.on("PLAYER_MONEY", function()
    local now = GetMoney and GetMoney() or 0
    if money and now > money then happened(30) end
    money = now
end)

local function show()
    for r = 0, 4 do
        local cells = {}
        for c = 1, 5 do
            local sq = r * 5 + c
            local ev = G.nornsSquare(K.db, sq)
            local label = ev == 0 and K.L("FREE") or K.L("S" .. ev)
            cells[#cells + 1] = (G.nornsMarked(K.db, sq) == 1 and "[x]" or "[ ]") .. label
        end
        print(table.concat(cells, " "))
    end
    print(K.f("LINES", G.nornsLines(K.db)))
end

SLASH_NORNSDRAW1 = "/bingo"
SlashCmdList["NORNSDRAW"] = function(msg)
    if not K.db then return end
    local cmd = (msg or ""):match("^%s*(%S*)")
    if cmd == "new" or not K.db.card then
        G.nornsDeal_(K.db, POOL, K.now(), K.now())
        print(K.f("DEALT", K.L("NAME")))
    end
    if cmd == "help" then print(K.L("HELP")) return end
    show()
end
