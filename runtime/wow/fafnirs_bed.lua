-- runtime/wow/fafnirs_bed.lua -- Fafnir's Bed's host glue (spec addon 2, Unified Inventory).
--
-- /fafnir                    net inventory across every character on the account
-- /fafnir <name or item id>  who has it, and the net count
--
-- The inventory model is PARENA (stdlib/collection/inventory.prn, RefluxCore.modules["collection/inventory"]).
-- This file reads the client into plain {[itemID] = count} tables, hands them over on the events that mean
-- "this location may have changed", and gives the searchable view as a slash command.
--
-- Which client calls, and how far each is trusted (docs/COLLECTION_OF_VALUE_STEP1_AND_DISPUTES.md section 1):
--   CreateFrame/RegisterEvent/SetScript, SlashCmdList, UnitName, GetRealmName, GetMoney, time, print -- every client
--   Bags: C_Container.GetContainerNumSlots/GetContainerItemInfo (retail 10.0+) OR the older globals
--     GetContainerNumSlots/GetContainerItemInfo. Feature-tested on every read; whichever exists is used.
--   Bag/bank ids: NUM_BAG_SLOTS, NUM_REAGENTBAG_SLOTS, BANK_CONTAINER, NUM_BANKBAGSLOTS, REAGENTBANK_CONTAINER --
--     read from the client's own globals, never hard-coded. A location whose globals are missing is not read
--     (and /fafnir says so) rather than read with guessed ids.
--   Gear: GetInventoryItemID/GetInventoryItemLink("player", slot) over INVSLOT_FIRST_EQUIPPED..INVSLOT_LAST_EQUIPPED
--     (1..19 when the client does not name them), on login and PLAYER_EQUIPMENT_CHANGED.
--   Mail: GetInboxNumItems/GetInboxItem, ATTACHMENTS_MAX_RECEIVE -- only while the mailbox is open.
--   Item facts, for the forge's gear optimizer (stdlib/collection/upgrades.prn): GetItemInfo (C_Item.GetItemInfo
--     where it exists) for item level, required level, slot, class and subclass ids, binding; GetItemStats
--     (C_Item.GetItemStats) for the stats; GET_ITEM_INFO_RECEIVED when the client had not cached an item yet.
--     Soulbound copies: C_Item.IsBound(ItemLocation:CreateFromBagAndSlot(...)) where both exist; without them the
--     forge goes by the binding type alone (bind on pickup = stays with its holder).
-- None of the bag/bank/mail forms above is confirmed on WoW Forever yet; that is what the in-game probe and
-- the first real run settle.

local ADDON = "@ADDON@"
local VAR = "@VAR@"

local I = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/inventory"]
if not I then
    print("|cffff5555" .. ADDON .. ": RefluxCore's collection/inventory module is missing; inventory disabled.|r")
    return
end

local db = nil
local unreadable = {}          -- location -> why it could not be read on this client

local function character()
    local name = UnitName and UnitName("player")
    local realm = GetRealmName and GetRealmName()
    if not name or name == "" then return nil end
    if realm and realm ~= "" then return name .. "-" .. realm end
    return name
end

local function num_slots(bag)
    if C_Container and C_Container.GetContainerNumSlots then return C_Container.GetContainerNumSlots(bag) or 0 end
    if GetContainerNumSlots then return GetContainerNumSlots(bag) or 0 end
    return nil
end

local function slot_item(bag, slot)
    if C_Container and C_Container.GetContainerItemInfo then
        local info = C_Container.GetContainerItemInfo(bag, slot)
        if info then return info.itemID, info.stackCount, info.hyperlink end
        return nil
    end
    if GetContainerItemInfo then
        local _, count, _, _, _, _, link, _, _, id = GetContainerItemInfo(bag, slot)
        return id, count, link
    end
    return nil
end

local function name_of(link)
    return type(link) == "string" and link:match("%[(.-)%]") or nil
end

-- ---- item facts for the gear optimizer ---------------------------------------------------------------------------
-- GetItemStats' keys, shortened to the optimizer's (stdlib/collection/upgrades.prn upg-stat-names). A key not here is
-- kept as-is under `other`, so the forge can say which stats it did not score instead of silently ignoring them.
local STAT_KEYS = {
    ITEM_MOD_STRENGTH_SHORT = "str", ITEM_MOD_AGILITY_SHORT = "agi", ITEM_MOD_STAMINA_SHORT = "sta",
    ITEM_MOD_INTELLECT_SHORT = "int", ITEM_MOD_SPIRIT_SHORT = "spi", RESISTANCE0_NAME = "armor",
    ITEM_MOD_ATTACK_POWER_SHORT = "ap", ITEM_MOD_RANGED_ATTACK_POWER_SHORT = "rap",
    ITEM_MOD_SPELL_POWER_SHORT = "sp", ITEM_MOD_SPELL_DAMAGE_DONE_SHORT = "sp", ITEM_MOD_SPELL_HEALING_DONE_SHORT = "heal",
    ITEM_MOD_CRIT_RATING_SHORT = "crit", ITEM_MOD_CRIT_MELEE_RATING_SHORT = "crit", ITEM_MOD_CRIT_RANGED_RATING_SHORT = "crit",
    ITEM_MOD_HIT_RATING_SHORT = "hit", ITEM_MOD_HIT_MELEE_RATING_SHORT = "hit", ITEM_MOD_HIT_RANGED_RATING_SHORT = "hit",
    ITEM_MOD_CRIT_SPELL_RATING_SHORT = "spellcrit", ITEM_MOD_HIT_SPELL_RATING_SHORT = "spellhit",
    ITEM_MOD_MANA_REGENERATION_SHORT = "mp5", ITEM_MOD_POWER_REGEN0_SHORT = "mp5",
    ITEM_MOD_DEFENSE_SKILL_RATING_SHORT = "def", ITEM_MOD_DAMAGE_PER_SECOND_SHORT = "dps",
}

local function item_info(id)
    if C_Item and C_Item.GetItemInfo then return C_Item.GetItemInfo(id) end
    if GetItemInfo then return GetItemInfo(id) end
end

local function item_stats(link)
    if C_Item and C_Item.GetItemStats then return C_Item.GetItemStats(link) end
    if GetItemStats then return GetItemStats(link) end
end

-- Record what the optimizer needs to know about item `id`, once. Not gear (no slot) is skipped: the SavedVariables
-- stay small. Returns false when the client has not loaded the item yet (GET_ITEM_INFO_RECEIVED will bring it).
local function remember_facts(id)
    if not (db and id) then return true end
    db.items = db.items or {}
    if db.items[id] then return true end
    local name, link, quality, ilvl, req, _, _, _, loc, _, _, cls, sub, bind = item_info(id)
    if not name then return false end
    if not loc or loc == "" or loc == "INVTYPE_NON_EQUIP_IGNORE" or loc == "INVTYPE_BAG" or loc == "INVTYPE_AMMO"
        or loc == "INVTYPE_QUIVER" then return true end
    local f = { q = quality, ilvl = ilvl, req = req, loc = loc, cls = cls, sub = sub, bind = bind, s = {} }
    local raw = link and item_stats(link)
    for k, v in pairs(raw or {}) do
        if STAT_KEYS[k] then f.s[STAT_KEYS[k]] = (f.s[STAT_KEYS[k]] or 0) + v
        else f.other = f.other or {}; f.other[k] = v end
    end
    db.items[id] = f
    I.invRememberName(db, id, name)
    return true
end

-- Is the copy in this bag slot soulbound? nil when the client cannot say.
local function slot_bound(bag, slot)
    if not (C_Item and C_Item.IsBound and ItemLocation and ItemLocation.CreateFromBagAndSlot) then return nil end
    local ok, loc = pcall(ItemLocation.CreateFromBagAndSlot, ItemLocation, bag, slot)
    if not ok or not loc then return nil end
    local ok2, b = pcall(C_Item.IsBound, loc)
    if ok2 then return b and true or false end
    return nil
end

-- Read the given bag ids into one {[itemID] = count}; nil when the client has no bag API at all.
local function read_bags(ids)
    local snap, bound = {}, {}
    for _, bag in ipairs(ids) do
        local n = num_slots(bag)
        if n == nil then return nil end
        -- The backpack is never 0 slots: 0 means the client has not loaded the bags yet (early login) or has
        -- already torn them down (logout). Saving that would wipe the real snapshot -- the founder's first
        -- live account came back with every character empty -- so it is "not ready", not "empty".
        if bag == 0 and n == 0 then return nil, "not ready" end
        for slot = 1, n do
            local id, count, link = slot_item(bag, slot)
            if id and count then
                snap[id] = (snap[id] or 0) + count
                I.invRememberName(db, id, name_of(link))
                remember_facts(id)
                if slot_bound(bag, slot) then bound[id] = 1 end
            end
        end
    end
    return snap, nil, bound
end

local function bag_ids()
    if type(NUM_BAG_SLOTS) ~= "number" then return nil end
    local ids = {}
    for b = 0, NUM_BAG_SLOTS do ids[#ids + 1] = b end
    if type(NUM_REAGENTBAG_SLOTS) == "number" then
        for b = NUM_BAG_SLOTS + 1, NUM_BAG_SLOTS + NUM_REAGENTBAG_SLOTS do ids[#ids + 1] = b end
    end
    return ids
end

local function bank_ids()
    if type(BANK_CONTAINER) ~= "number" or type(NUM_BAG_SLOTS) ~= "number" or type(NUM_BANKBAGSLOTS) ~= "number" then
        return nil
    end
    local first = NUM_BAG_SLOTS + (type(NUM_REAGENTBAG_SLOTS) == "number" and NUM_REAGENTBAG_SLOTS or 0) + 1
    local ids = { BANK_CONTAINER }
    for b = first, first + NUM_BANKBAGSLOTS - 1 do ids[#ids + 1] = b end
    if type(REAGENTBANK_CONTAINER) == "number" then ids[#ids + 1] = REAGENTBANK_CONTAINER end
    return ids
end

local function apply(loc, snap, bound)
    local who = character()
    if not (db and who) then return end
    if not snap then return end
    unreadable[loc] = nil
    I.invApplySnapshot(db, who, loc, snap, time())
    -- soulbound item ids per character: a bound copy never unbinds, so ids only accumulate
    if bound and next(bound) then
        db.bound = db.bound or {}
        db.bound[who] = db.bound[who] or {}
        for id in pairs(bound) do db.bound[who][id] = 1 end
    end
end

local function scan_bags()
    local ids = bag_ids()
    if not ids then unreadable.bags = "NUM_BAG_SLOTS is missing"; return end
    local snap, why, bound = read_bags(ids)
    if why == "not ready" then return end
    if not snap then unreadable.bags = "no container API (C_Container or GetContainerItemInfo)"; return end
    apply("bags", snap, bound)
end

local function scan_bank()
    local ids = bank_ids()
    if not ids then unreadable.bank = "BANK_CONTAINER / NUM_BANKBAGSLOTS are missing"; return end
    local snap, why, bound = read_bags(ids)
    if why == "not ready" then return end
    if not snap then unreadable.bank = "no container API"; return end
    apply("bank", snap, bound)
end

local function scan_mail()
    if not (GetInboxNumItems and GetInboxItem) then unreadable.mail = "no inbox API"; return end
    local per = type(ATTACHMENTS_MAX_RECEIVE) == "number" and ATTACHMENTS_MAX_RECEIVE or nil
    if not per then unreadable.mail = "ATTACHMENTS_MAX_RECEIVE is missing"; return end
    local snap = {}
    for i = 1, (GetInboxNumItems() or 0) do
        for j = 1, per do
            local name, id, _, count = GetInboxItem(i, j)
            if id and count then
                snap[id] = (snap[id] or 0) + count
                I.invRememberName(db, id, name)
                remember_facts(id)
            end
        end
    end
    apply("mail", snap)
end

-- What the character wears: slots INVSLOT_FIRST_EQUIPPED..INVSLOT_LAST_EQUIPPED (1 head .. 19 tabard when the
-- client does not name them). Counted as the "equipped" location, and kept by slot for the character sheet.
local function scan_gear()
    local who = character()
    if not (db and who) then return end
    if not GetInventoryItemID then unreadable.equipped = "no GetInventoryItemID"; return end
    local first = type(INVSLOT_FIRST_EQUIPPED) == "number" and INVSLOT_FIRST_EQUIPPED or 1
    local last = type(INVSLOT_LAST_EQUIPPED) == "number" and INVSLOT_LAST_EQUIPPED or 19
    local gear, snap, worn = {}, {}, 0
    for slot = first, last do
        local id = GetInventoryItemID("player", slot)
        if id then
            gear[slot] = id
            snap[id] = (snap[id] or 0) + 1
            worn = worn + 1
            if GetInventoryItemLink then I.invRememberName(db, id, name_of(GetInventoryItemLink("player", slot))) end
            remember_facts(id)
        end
    end
    -- Nothing worn at all is what an unloaded character looks like (early login, logout), not a naked one
    -- worth recording over the real gear -- the same rule as the bags' 0-slot backpack.
    if worn == 0 then return end
    apply("equipped", snap)
    I.invSetGear_(db, who, gear, time())
end

local function scan_gold()
    local who = character()
    if db and who and GetMoney then I.invSetGold(db, who, GetMoney(), time()) end
end

local frame = CreateFrame("Frame")
frame:RegisterEvent("ADDON_LOADED")
frame:RegisterEvent("PLAYER_LOGIN")
frame:RegisterEvent("BAG_UPDATE_DELAYED")
frame:RegisterEvent("PLAYER_MONEY")
frame:RegisterEvent("PLAYER_EQUIPMENT_CHANGED")
frame:RegisterEvent("BANKFRAME_OPENED")
frame:RegisterEvent("BANKFRAME_CLOSED")          -- the bank is only readable while it is open
frame:RegisterEvent("PLAYERBANKSLOTS_CHANGED")
frame:RegisterEvent("MAIL_INBOX_UPDATE")
frame:RegisterEvent("PLAYER_LOGOUT")
frame:RegisterEvent("GET_ITEM_INFO_RECEIVED")    -- an item the client had not cached when it was first seen
local bank_open = false
frame:SetScript("OnEvent", function(_, event, arg1)
    if event == "ADDON_LOADED" then
        if arg1 == ADDON then
            _G[VAR] = I.invInit(_G[VAR] or {})
            db = _G[VAR]
        end
    elseif event == "PLAYER_LOGIN" then
        scan_bags(); scan_gold(); scan_gear()
        -- the bags are often not loaded at login yet; look again once the world is up
        if C_Timer and C_Timer.After then C_Timer.After(3, function() scan_bags(); scan_gold(); scan_gear() end) end
    elseif event == "BAG_UPDATE_DELAYED" then
        scan_bags(); if bank_open then scan_bank() end
    elseif event == "PLAYER_MONEY" then
        scan_gold()
    elseif event == "PLAYER_EQUIPMENT_CHANGED" then
        scan_gear()
    elseif event == "BANKFRAME_OPENED" then
        bank_open = true; scan_bank()
    elseif event == "BANKFRAME_CLOSED" then
        bank_open = false
    elseif event == "PLAYERBANKSLOTS_CHANGED" then
        if bank_open then scan_bank() end
    elseif event == "MAIL_INBOX_UPDATE" then
        scan_mail()
    elseif event == "GET_ITEM_INFO_RECEIVED" then
        if arg1 then remember_facts(arg1) end
    end
    -- No scan at PLAYER_LOGOUT: by then the client reports empty bags and 0 gold, and the last
    -- BAG_UPDATE_DELAYED / PLAYER_MONEY scan is already current.
end)

local function money(copper)
    copper = copper or 0
    return string.format("%dg %ds %dc", math.floor(copper / 10000), math.floor(copper / 100) % 100, copper % 100)
end

local function report_item(id)
    local who = I.invWho(db, id)
    local parts = {}
    for name, n in pairs(who) do parts[#parts + 1] = name .. " " .. n end
    table.sort(parts)
    print(string.format("%s: %s -- net %d  (%s)", ADDON, db.names[id] or ("item " .. id), I.invNet(db, id),
        #parts > 0 and table.concat(parts, ", ") or "nobody"))
end

SLASH_FAFNIRSBED1 = "/fafnir"
SLASH_FAFNIRSBED2 = "/hoard"
SlashCmdList["FAFNIRSBED"] = function(msg)
    if not db then print(ADDON .. ": not loaded yet."); return end
    msg = (msg or ""):match("^%s*(.-)%s*$")
    if msg == "" then
        scan_bags(); scan_gold(); scan_gear()
        local t = I.invTotals(db)
        print(string.format("%s: net inventory -- %d characters, %d distinct items, %d items, %s across the account.",
            ADDON, t[1], t[2], t[3], money(t[4])))
        for loc, why in pairs(unreadable) do print(string.format("  %s not read on this client: %s", loc, why)) end
        print("  /fafnir <name or item id> -- who has it, and the net count")
        return
    end
    local id = tonumber(msg) or tonumber((msg:match("item:(%d+)")))
    if id then report_item(id); return end
    local hits = I.invFind(db, msg)
    if #hits == 0 then print(ADDON .. ": nothing seen named like '" .. msg .. "'."); return end
    for k = 1, math.min(#hits, 10) do report_item(hits[k]) end
    if #hits > 10 then print(string.format("  ...and %d more", #hits - 10)) end
end

FafnirsBed = { remember_facts = remember_facts, scan_bags = scan_bags, scan_bank = scan_bank, scan_mail = scan_mail, scan_gold = scan_gold, scan_gear = scan_gear }
