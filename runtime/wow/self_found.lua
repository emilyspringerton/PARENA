-- runtime/wow/self_found.lua -- Self Found's host glue. The doctrine and every record are PARENA
-- (stdlib/collection/selffound.prn, RefluxCore.modules["collection/selffound"]); this file only notices what the
-- client did and hands it over. /selffound (/sf) shows it.
--
-- How each transaction is seen. Every client call is feature-tested; one that is missing is skipped and named in
-- `/sf why`, never guessed:
--   vendor   MERCHANT_SHOW/CLOSED + UnitGUID("npc"); the item list from C_MerchantFrame.GetItemInfo or the older
--            GetMerchantItemInfo; buys hook BuyMerchantItem, buybacks BuybackItem, repairs RepairAllItems; sells are
--            items leaving the bags while the money goes up with the merchant window open (selling has no call to hook).
--   AH       scans: QueryAuctionItems / C_AuctionHouse.SendBrowseQuery / SendSearchQuery / ReplicateItems (allowed).
--            buys: PlaceAuctionBid / C_AuctionHouse.PlaceBid / ConfirmCommoditiesPurchase. posts: PostAuction /
--            StartAuction / C_AuctionHouse.PostItem / PostCommodity. The CALL is what counts: an attempt to buy or
--            sell on the AH breaks self-found even if the server then refuses it.
--   trade    TRADE_ACCEPT_UPDATE with both sides accepted, then UI_INFO_MESSAGE ERR_TRADE_COMPLETE. An empty trade
--            (nothing on either side) is not a transaction.
--   mail     TakeInboxItem / TakeInboxMoney / AutoLootMailItem. GetInboxHeaderInfo: a mail you can reply to that
--            was not returned is from a player (alts included) -> breaks it; AH, NPC and returned mail do not.
--            SendMail is recorded.
--   gbank    GUILDBANKFRAME_OPENED/CLOSED: items arriving in the bags while it is open are withdrawals (clicked or
--            dragged), items leaving are deposits; WithdrawGuildBankMoney / DepositGuildBankMoney for gold.
--   loot     CHAT_MSG_LOOT lines matching the client's own LOOT_ITEM_SELF(_MULTIPLE) format. quests: QUEST_TURNED_IN.

local ADDON = "@ADDON@"
local VAR = "@VAR@"

local S = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/selffound"]
if not S then
    print("|cffff5555" .. ADDON .. ": RefluxCore's collection/selffound module is missing; Self Found disabled.|r")
    return
end

local db = nil
local missing = {}            -- what this client lacks -> why that transaction kind is not seen
local INT_MAX = 2147483647

local function clamp(n) n = tonumber(n) or 0; if n > INT_MAX then return INT_MAX end; if n < 0 then return 0 end; return math.floor(n) end

local function who()
    local name = UnitName and UnitName("player")
    local realm = GetRealmName and GetRealmName()
    if not name or name == "" then return nil end
    if realm and realm ~= "" then return name .. "-" .. realm end
    return name
end

local function level() return (UnitLevel and UnitLevel("player")) or 0 end

local function where()
    if not (C_Map and C_Map.GetBestMapForUnit) then return nil, nil, nil end
    local map = C_Map.GetBestMapForUnit("player")
    if not map then return nil, nil, nil end
    local pos = C_Map.GetPlayerMapPosition and C_Map.GetPlayerMapPosition(map, "player")
    if not pos then return map, nil, nil end
    local x, y = pos:GetXY()
    return map, math.floor(x * 10000 + 0.5) / 10000, math.floor(y * 10000 + 0.5) / 10000
end

local function record(kind, a, b, c)
    local w = who()
    if not (db and w) then return end
    local zone, x, y = where()
    local before = S.sfStatus(db, w)
    local after = S.sfRecord(db, w, level(), kind, clamp(a), clamp(b), clamp(c), time(), zone, x, y)
    if before ~= 0 and after == 0 then
        print("|cffff8800Self Found: " .. w .. " is no longer self-found (" .. (db.lastWhy or "doctrine") .. ").|r")
    end
end

local function item_of(link)
    if not link then return 0 end
    return tonumber(link:match("item:(%d+)")) or 0
end

local function hook(owner, name, fn, what)
    local t = owner or _G
    if type(t[name]) == "function" then hooksecurefunc(t, name, fn); return true end
    return false
end

-- --- bags: counts by item id, for the sell / guild-bank diffs ---------------------------------------------------
local function bag_counts()
    local nbags = NUM_BAG_SLOTS
    if not nbags then return nil end
    local C = C_Container
    local nslots = (C and C.GetContainerNumSlots) or GetContainerNumSlots
    if not nslots then return nil end
    local counts = {}
    for bag = 0, nbags do
        for slot = 1, (nslots(bag) or 0) do
            local id, n
            if C and C.GetContainerItemInfo then
                local info = C.GetContainerItemInfo(bag, slot)
                if info then id, n = info.itemID, info.stackCount end
            elseif GetContainerItemInfo then
                local _, cnt, _, _, _, _, link = GetContainerItemInfo(bag, slot)
                id, n = item_of(link), cnt
            end
            if id and id > 0 then counts[id] = (counts[id] or 0) + (n or 1) end
        end
    end
    return counts
end

local snap, snap_money = nil, 0
local function diff_bags(on_gain, on_loss)
    local now = bag_counts()
    if not (now and snap) then snap = now; return end
    for id, n in pairs(now) do local d = n - (snap[id] or 0); if d > 0 and on_gain then on_gain(id, d) end end
    for id, n in pairs(snap) do local d = n - (now[id] or 0); if d > 0 and on_loss then on_loss(id, d) end end
    snap = now
end

-- --- vendors ----------------------------------------------------------------------------------------------------
local merchant_npc = nil       -- npc id of the open merchant, or 0 when the window has no creature (e.g. an item)

local function npc_id()
    local guid = UnitGUID and UnitGUID("npc")
    if not guid then return 0 end
    local kind, _, _, _, _, id = strsplit("-", guid)
    if kind == "Creature" or kind == "Vehicle" then return tonumber(id) or 0 end
    return 0
end

local function merchant_item(i)
    local id = (GetMerchantItemID and GetMerchantItemID(i)) or item_of(GetMerchantItemLink and GetMerchantItemLink(i))
    local price
    if C_MerchantFrame and C_MerchantFrame.GetItemInfo then
        local info = C_MerchantFrame.GetItemInfo(i)
        price = info and info.price
    elseif GetMerchantItemInfo then
        local _, _, p = GetMerchantItemInfo(i)
        price = p
    end
    return id or 0, price or 0
end

local function on_merchant_show()
    merchant_npc = npc_id()
    snap, snap_money = bag_counts(), (GetMoney and GetMoney()) or 0
    if merchant_npc == 0 or not db then return end
    local w = who()
    local zone, x, y = where()
    local sf = (w and S.sfStatus(db, w) > 0) and 1 or 0
    S.sfVendor(db, merchant_npc, (UnitName and UnitName("npc")) or "?", time(), zone, x, y, sf)
    for i = 1, ((GetMerchantNumItems and GetMerchantNumItems()) or 0) do
        local id, price = merchant_item(i)
        if id > 0 then S.sfVendorItem(db, merchant_npc, id, clamp(price)) end
    end
end

-- --- trade ------------------------------------------------------------------------------------------------------
local trade_seen = nil
local function trade_snapshot()
    local n = 0
    local slots = MAX_TRADABLE_ITEMS or 6
    for i = 1, slots do
        if GetTradePlayerItemLink and GetTradePlayerItemLink(i) then n = n + 1 end
        if GetTradeTargetItemLink and GetTradeTargetItemLink(i) then n = n + 1 end
    end
    local got = (GetTargetTradeMoney and GetTargetTradeMoney()) or 0
    local gave = (GetPlayerTradeMoney and GetPlayerTradeMoney()) or 0
    return { items = n, got = got, gave = gave }
end

-- --- mail -------------------------------------------------------------------------------------------------------
local function mail_kind(i)
    if not GetInboxHeaderInfo then return nil end
    local _, _, _, _, _, _, _, _, _, wasReturned, _, canReply = GetInboxHeaderInfo(i)
    if canReply and not wasReturned then return S.sfKMailPlayer() end
    return S.sfKMailOther()
end

local function on_take_item(i, att)
    local k = mail_kind(i)
    if not k then return end
    local link = GetInboxItemLink and GetInboxItemLink(i, att or 1)
    local _, _, _, count = (GetInboxItem or function() end)(i, att or 1)
    db.lastWhy = "took mail from a player"
    record(k, item_of(link), count or 1, 0)
end

local function on_take_money(i)
    local k = mail_kind(i)
    if not k then return end
    local money = select(5, GetInboxHeaderInfo(i))
    db.lastWhy = "took money from a player's mail"
    record(k, 0, 0, money or 0)
end

-- --- wiring -----------------------------------------------------------------------------------------------------
local gbank_open, ah_scanned = false, false
local loot_pat1, loot_pat2

local function fmt_to_pattern(fmt)
    if not fmt then return nil end
    return "^" .. fmt:gsub("([%(%)%.%%%+%-%*%?%[%]%^%$])", "%%%1"):gsub("%%%%s", "(.+)"):gsub("%%%%d", "(%%d+)") .. "$"
end

local function install_hooks()
    if not hook(nil, "BuyMerchantItem", function(i, qty)
        local id = merchant_item(i)
        record(S.sfKVendorBuy(), id, qty or 1, merchant_npc or 0)
    end) then missing.vendor_buy = "no BuyMerchantItem" end
    hook(nil, "BuybackItem", function(i)
        record(S.sfKBuyback(), item_of(GetBuybackItemLink and GetBuybackItemLink(i)), 1, merchant_npc or 0)
    end)
    hook(nil, "RepairAllItems", function() record(S.sfKRepair(), 0, 0, merchant_npc or 0) end)

    local function scan() if not ah_scanned then ah_scanned = true; record(S.sfKAhScan(), 0, 0, 0) end end
    local function ah_buy() db.lastWhy = "bought on the auction house"; record(S.sfKAhBuy(), 0, 0, 0) end
    local function ah_post() db.lastWhy = "posted to the auction house"; record(S.sfKAhPost(), 0, 0, 0) end
    local A = C_AuctionHouse
    local seen_ah = false
    seen_ah = hook(nil, "QueryAuctionItems", scan) or seen_ah
    seen_ah = hook(nil, "PlaceAuctionBid", ah_buy) or seen_ah
    seen_ah = hook(nil, "PostAuction", ah_post) or seen_ah
    seen_ah = hook(nil, "StartAuction", ah_post) or seen_ah
    if A then
        seen_ah = hook(A, "SendBrowseQuery", scan) or seen_ah
        seen_ah = hook(A, "SendSearchQuery", scan) or seen_ah
        seen_ah = hook(A, "ReplicateItems", scan) or seen_ah
        seen_ah = hook(A, "PlaceBid", ah_buy) or seen_ah
        seen_ah = hook(A, "ConfirmCommoditiesPurchase", ah_buy) or seen_ah
        seen_ah = hook(A, "PostItem", ah_post) or seen_ah
        seen_ah = hook(A, "PostCommodity", ah_post) or seen_ah
    end
    if not seen_ah then missing.auction = "no auction house calls to watch" end

    local m1 = hook(nil, "TakeInboxItem", on_take_item)
    local m2 = hook(nil, "TakeInboxMoney", on_take_money)
    hook(nil, "AutoLootMailItem", function(i)
        local _, _, _, _, money, _, _, hasItem = GetInboxHeaderInfo(i)
        if (money or 0) > 0 then on_take_money(i) end
        if hasItem then on_take_item(i, 1) end
    end)
    if not (m1 and m2) then missing.mail = "no TakeInboxItem/TakeInboxMoney" end
    hook(nil, "SendMail", function() record(S.sfKMailSend(), 0, 0, 0) end)

    hook(nil, "WithdrawGuildBankMoney", function(copper)
        db.lastWhy = "withdrew gold from a guild bank"; record(S.sfKGbankWithdraw(), 0, 0, copper)
    end)
    hook(nil, "DepositGuildBankMoney", function(copper) record(S.sfKGbankDeposit(), 0, 0, copper) end)

    loot_pat1 = fmt_to_pattern(LOOT_ITEM_SELF_MULTIPLE)
    loot_pat2 = fmt_to_pattern(LOOT_ITEM_SELF)
    if not (loot_pat1 or loot_pat2) then missing.loot = "no LOOT_ITEM_SELF format string" end
end

local frame = CreateFrame("Frame")
for _, e in ipairs({ "ADDON_LOADED", "PLAYER_LOGIN", "MERCHANT_SHOW", "MERCHANT_CLOSED", "BAG_UPDATE_DELAYED",
                     "PLAYER_MONEY", "AUCTION_HOUSE_SHOW", "AUCTION_HOUSE_CLOSED", "TRADE_ACCEPT_UPDATE",
                     "TRADE_CLOSED", "UI_INFO_MESSAGE", "GUILDBANKFRAME_OPENED", "GUILDBANKFRAME_CLOSED",
                     "CHAT_MSG_LOOT", "QUEST_TURNED_IN" }) do
    pcall(frame.RegisterEvent, frame, e)   -- an event this client does not have is simply never fired
end

local money_up = false
frame:SetScript("OnEvent", function(_, event, a1, a2, a3)
    if event == "ADDON_LOADED" then
        if a1 == ADDON then _G[VAR] = S.sfInit(_G[VAR] or {}); db = _G[VAR]; install_hooks() end
    elseif not db then
        return
    elseif event == "PLAYER_LOGIN" then
        local w = who(); if w then S.sfChar(db, w, level(), time()) end
    elseif event == "MERCHANT_SHOW" then
        money_up = false; on_merchant_show()
    elseif event == "MERCHANT_CLOSED" then
        merchant_npc = nil; snap = nil
    elseif event == "PLAYER_MONEY" then
        local m = (GetMoney and GetMoney()) or 0
        if m > snap_money then money_up = true end
        snap_money = m
    elseif event == "BAG_UPDATE_DELAYED" then
        if gbank_open then
            diff_bags(function(id, n) db.lastWhy = "took items from a guild bank"; record(S.sfKGbankWithdraw(), id, n, 0) end,
                      function(id, n) record(S.sfKGbankDeposit(), id, n, 0) end)
        elseif merchant_npc then
            if money_up then
                diff_bags(nil, function(id, n) record(S.sfKVendorSell(), id, n, merchant_npc) end)
                money_up = false
            else
                snap = bag_counts()
            end
        end
    elseif event == "AUCTION_HOUSE_SHOW" then
        ah_scanned = false
    elseif event == "GUILDBANKFRAME_OPENED" then
        gbank_open = true; snap = bag_counts()
    elseif event == "GUILDBANKFRAME_CLOSED" then
        gbank_open = false; snap = nil
    elseif event == "TRADE_ACCEPT_UPDATE" then
        if a1 == 1 and a2 == 1 then trade_seen = trade_snapshot() end
    elseif event == "TRADE_CLOSED" then
        -- completion is announced by UI_INFO_MESSAGE; a cancelled trade only closes
    elseif event == "UI_INFO_MESSAGE" then
        local text = (type(a2) == "string" and a2) or a1
        if trade_seen and ERR_TRADE_COMPLETE and text == ERR_TRADE_COMPLETE then
            local t = trade_seen; trade_seen = nil
            if t.items > 0 or t.got > 0 or t.gave > 0 then
                db.lastWhy = "traded with another player"
                record(S.sfKTrade(), t.items, t.got, t.gave)
            end
        end
    elseif event == "CHAT_MSG_LOOT" then
        local link, n
        if loot_pat1 then link, n = a1:match(loot_pat1) end
        if not link and loot_pat2 then link = a1:match(loot_pat2) end
        if link then record(S.sfKLoot(), item_of(link), tonumber(n) or 1, 0) end
    elseif event == "QUEST_TURNED_IN" then
        record(S.sfKQuest(), a1 or 0, 0, a3 or 0)
    end
end)

-- --- /selffound ----------------------------------------------------------------------------------------------------
local KIND = { "vendor buy", "vendor sell", "buyback", "repair", "AH scan", "AH buy", "AH post", "trade",
               "player mail", "AH/NPC mail", "sent mail", "guild bank withdraw", "guild bank deposit", "loot", "quest" }
local STATUS = { [0] = "|cffff5555broken|r", [1] = "|cffffd100self-found since tracking began|r",
                 [2] = "|cff55ff55self-found since level 1|r" }

local function show()
    print("Self Found -- every character this account has logged in:")
    for name, c in pairs(db.chars) do
        local line = "  " .. name .. ": " .. (STATUS[c.status] or "?") .. ", " .. (c.n or 0) .. " transactions"
        if c.status == 0 then line = line .. " (broken by " .. (KIND[c.brokenKind] or "?") .. " on " .. date("%Y-%m-%d", c.brokenAt) .. ")" end
        print(line)
    end
    local nv, nsf = 0, 0
    for _, v in pairs(db.vendors) do nv = nv + 1; if (v.sfVisits or 0) > 0 then nsf = nsf + 1 end end
    print("  vendors found: " .. nv .. " (" .. nsf .. " by self-found characters). /sf vendors, /sf log, /sf why")
end

SLASH_SELFFOUND1 = "/selffound"
SLASH_SELFFOUND2 = "/sf"
SlashCmdList.SELFFOUND = function(msg)
    if not db then print("Self Found is not loaded yet."); return end
    msg = (msg or ""):lower()
    if msg == "vendors" then
        for id, v in pairs(db.vendors) do
            local n = 0; for _ in pairs(v.items or {}) do n = n + 1 end
            print(string.format("  %s (npc %d), zone %s at %s,%s: %d items, %d visits", v.name or "?", id,
                tostring(v.zone), tostring(v.x), tostring(v.y), n, v.visits or 0))
        end
    elseif msg == "log" then
        for seq = math.max(db.firstSeq, db.nextSeq - 15), db.nextSeq - 1 do
            local t = db.tx[seq]
            if t then print(string.format("  #%d %s %s: %s %d x%d (%d)", seq, date("%m-%d %H:%M", t.at), t.who,
                KIND[t.kind] or "?", t.a or 0, t.b or 0, t.c or 0)) end
        end
    elseif msg == "why" then
        local any = false
        for k, why in pairs(missing) do any = true; print("  not seen on this client: " .. k .. " -- " .. why) end
        if not any then print("  every transaction kind is watched on this client.") end
    else
        show()
    end
end
