-- tests/reflux_wow/selffound_test.lua -- Self Found in the packaged client, under real Lua 5.1, against a stubbed
-- client: vendors, the auction house, trades, mail, guild bank, loot and quests, and the doctrine
-- (stdlib/collection/selffound.prn). usage: lua5.1 selffound_test.lua <client dir>
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

-- The world: one character, its bags and money, an open merchant / mailbox / guild bank / trade, and the AH API.
local function world(name, level)
    return { out = {}, name = name, level = level, now = 1763400000, money = 100000, bags = { [2589] = 4 },
             npc = nil, merchant = {}, inbox = {}, trade = { player = {}, target = {}, got = 0, gave = 0 },
             saved = {} }
end

local function session(w, saved)
    local env = { print = function(...)
        local t = {}
        for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
        w.out[#w.out + 1] = table.concat(t, " ")
    end }
    for _, k in ipairs({ "pairs", "ipairs", "type", "tostring", "tonumber", "select", "string", "table", "math",
                         "next", "error", "assert", "pcall", "unpack", "setmetatable", "getmetatable", "date" }) do
        env[k] = _G[k] or os[k]
    end
    env.date = os.date
    env._G = env
    env.time = function() return w.now end
    env.strsplit = function(sep, s)
        local t = {}
        for part in (s .. sep):gmatch("(.-)" .. sep:gsub("%-", "%%-")) do t[#t + 1] = part end
        return unpack(t)
    end
    env.hooksecurefunc = function(t, name, fn)
        if type(t) == "string" then t, name, fn = env, t, name end
        local orig = t[name]
        t[name] = function(...) local r = { orig(...) }; fn(...); return unpack(r) end
    end
    env.UnitName = function(u) if u == "npc" then return w.npc and w.npc.name end; return w.name end
    env.GetRealmName = function() return "Forever" end
    env.UnitLevel = function() return w.level end
    env.UnitGUID = function(u) if u == "npc" and w.npc then return "Creature-0-1-2-3-" .. w.npc.id .. "-0000ABCD" end end
    env.GetMoney = function() return w.money end
    env.C_Map = { GetBestMapForUnit = function() return 1420 end,
                  GetPlayerMapPosition = function() return { GetXY = function() return 0.51234, 0.25 end } end }
    env.NUM_BAG_SLOTS = 0
    env.C_Container = {
        GetContainerNumSlots = function() return 16 end,
        GetContainerItemInfo = function(_, slot)
            local i = 0
            for id, n in pairs(w.bags) do i = i + 1; if i == slot then return { itemID = id, stackCount = n } end end
        end }
    -- merchant
    env.GetMerchantNumItems = function() return #w.merchant end
    env.GetMerchantItemID = function(i) return w.merchant[i] and w.merchant[i].id end
    env.GetMerchantItemInfo = function(i) local m = w.merchant[i]; return "x", 0, m.price end
    env.BuyMerchantItem = function(i, qty) local m = w.merchant[i]; w.bags[m.id] = (w.bags[m.id] or 0) + (qty or 1) end
    env.BuybackItem = function() end
    env.GetBuybackItemLink = function() return "|Hitem:159:0|h[Water]|h" end
    env.RepairAllItems = function() end
    -- auction house (both shapes: the old globals and C_AuctionHouse)
    env.QueryAuctionItems = function() end
    env.PlaceAuctionBid = function() end
    env.PostAuction = function() end
    env.C_AuctionHouse = { SendBrowseQuery = function() end, PlaceBid = function() end,
                           ConfirmCommoditiesPurchase = function() end, PostItem = function() end,
                           PostCommodity = function() end }
    -- mail: header = sender, money, hasItem, wasReturned, canReply
    env.GetInboxHeaderInfo = function(i)
        local m = w.inbox[i]
        return nil, nil, m.sender, "subj", m.money or 0, 0, 30, m.item and 1 or nil, true, m.returned, true, m.reply
    end
    env.GetInboxItemLink = function(i) local m = w.inbox[i]; return m.item and ("|Hitem:" .. m.item .. ":0|h[x]|h") end
    env.GetInboxItem = function(i) local m = w.inbox[i]; return "x", m.item, nil, m.count or 1 end
    env.TakeInboxItem = function() end
    env.TakeInboxMoney = function() end
    env.AutoLootMailItem = function() end
    env.SendMail = function() end
    -- guild bank
    env.WithdrawGuildBankMoney = function() end
    env.DepositGuildBankMoney = function() end
    -- trade
    env.MAX_TRADABLE_ITEMS = 6
    env.GetTradePlayerItemLink = function(i) return w.trade.player[i] end
    env.GetTradeTargetItemLink = function(i) return w.trade.target[i] end
    env.GetTargetTradeMoney = function() return w.trade.got end
    env.GetPlayerTradeMoney = function() return w.trade.gave end
    env.ERR_TRADE_COMPLETE = "Trade complete."
    env.LOOT_ITEM_SELF = "You receive loot: %s."
    env.LOOT_ITEM_SELF_MULTIPLE = "You receive loot: %sx%d."

    local frames = {}
    env.CreateFrame = function()
        local f = { events = {}, scripts = {} }
        function f:RegisterEvent(e) self.events[e] = true end
        function f:SetScript(k, fn) self.scripts[k] = fn end
        frames[#frames + 1] = f
        return f
    end
    env.SlashCmdList = {}
    env.SelfFoundDB = saved
    local s = { env = env }
    function s.load(addon)
        local dir = client .. "/AddOns/" .. addon
        local ns = {}
        for _, file in ipairs(toc_files(dir, addon)) do
            local chunk = assert(loadfile(dir .. "/" .. file)); setfenv(chunk, env); chunk(addon, ns)
        end
    end
    function s.fire(event, ...) for _, f in ipairs(frames) do if f.events[event] and f.scripts.OnEvent then f.scripts.OnEvent(f, event, ...) end end end
    function s.sf(msg) w.out = {}; env.SlashCmdList.SELFFOUND(msg or ""); return table.concat(w.out, "\n") end
    for _, a in ipairs({ "RefluxCore", "SelfFound" }) do s.load(a) end
    for _, a in ipairs({ "RefluxCore", "SelfFound" }) do s.fire("ADDON_LOADED", a) end
    s.fire("PLAYER_LOGIN")
    return s
end

local function status(s, who) return s.env.SelfFoundDB.chars[who].status end
local function last(s) local db = s.env.SelfFoundDB; return db.tx[db.nextSeq - 1] end

-- ===== a fresh level-1 character: vendors, loot, quests and AH scans keep it self-found =======================
local w = world("Ingrid", 1)
local saved = {}
local s = session(w, saved)
local A = "Ingrid-Forever"
eq(status(s, A), 2, "first seen at level 1: self-found since level 1")

w.npc = { id = 3881, name = "Grimtak" }
w.merchant = { { id = 159, price = 25 }, { id = 4540, price = 25 } }
s.fire("MERCHANT_SHOW")
local v = s.env.SelfFoundDB.vendors[3881]
eq(v and v.name, "Grimtak", "a merchant window records the vendor")
eq(v and v.zone, 1420, "with its zone")
eq(v and v.x, 0.5123, "and position, to four places")
eq(v and v.items[4540], 25, "and every item on its list with its price")
eq(v and v.sfVisits, 1, "visited by a self-found character")

s.env.BuyMerchantItem(2, 5)
eq(last(s).kind, 1, "a buy is recorded as a vendor buy")
eq(last(s).a, 4540, "of that item")
eq(last(s).b, 5, "that many")
eq(last(s).c, 3881, "from that vendor")

-- sell: the item leaves the bags and the money goes up while the merchant is open
w.bags[2589] = nil; w.money = w.money + 40
s.fire("PLAYER_MONEY"); s.fire("BAG_UPDATE_DELAYED")
eq(last(s).kind, 2, "items leaving the bags with money going up are a vendor sell")
eq(last(s).a, 2589, "of the item that left")
s.env.RepairAllItems()
eq(last(s).kind, 4, "repairs are recorded")
s.fire("MERCHANT_CLOSED")

s.fire("CHAT_MSG_LOOT", "You receive loot: |cffffffff|Hitem:2589:0|h[Linen Cloth]|h|rx3.")
eq(last(s).kind, 14, "loot is recorded")
eq(last(s).b, 3, "with its count")
s.fire("QUEST_TURNED_IN", 7, 450, 120)
eq(last(s).kind, 15, "a quest turn-in is recorded")
eq(last(s).c, 120, "with its money")

s.fire("AUCTION_HOUSE_SHOW")
s.env.C_AuctionHouse.SendBrowseQuery({})
s.env.QueryAuctionItems("x")
eq(last(s).kind, 5, "an AH scan is recorded")
local n = s.env.SelfFoundDB.chars[A].n
eq(n, 6, "buy, sell, repair, loot, quest and ONE scan: two scan calls in one AH visit record once")
eq(status(s, A), 2, "vendors, loot, quests and AH scans keep it self-found")

-- mail that does not break it: the AH's (no reply), and its own returned mail
w.inbox = { { sender = "Auction House", money = 500, reply = false }, { sender = "Ingrid", item = 2589, reply = true, returned = true } }
s.env.TakeInboxMoney(1)
eq(last(s).kind, 10, "AH mail money is other mail")
s.env.TakeInboxItem(2, 1)
eq(last(s).kind, 10, "returned mail is other mail")
s.env.SendMail("Bjorn", "hi", "")
eq(last(s).kind, 11, "sending mail is recorded")
eq(status(s, A), 2, "none of that breaks it")

-- the first AH buy breaks it, for good
w.now = w.now + 60
s.env.C_AuctionHouse.PlaceBid(12345, 100)
eq(last(s).kind, 6, "an AH bid is an AH buy")
eq(status(s, A), 0, "and breaks self-found")
eq(s.env.SelfFoundDB.chars[A].brokenKind, 6, "the reason is kept")
eq(s.env.SelfFoundDB.chars[A].brokenAt, w.now, "and when")
has(table.concat(w.out, "\n"), "Ingrid-Forever is no longer self-found (bought on the auction house)", "the player is told why")
s.env.QueryAuctionItems("y")
eq(status(s, A), 0, "it never comes back")

local out = s.sf("")
has(out, "Ingrid-Forever: |cffff5555broken|r", "/sf shows the broken status")
has(out, "broken by AH buy", "and why")
has(out, "vendors found: 1 (1 by self-found characters)", "and the vendors found")
has(s.sf("vendors"), "Grimtak (npc 3881), zone 1420 at 0.5123,0.25: 2 items, 1 visits", "/sf vendors lists them")
has(s.sf("log"), "AH buy", "/sf log shows the latest transactions")
has(s.sf("why"), "every transaction kind is watched", "/sf why on a client with every call")

-- ===== each other way to break it, on fresh characters sharing the account's saved table ===================
local function fresh(name, lvl)
    local w2 = world(name, lvl)
    return w2, session(w2, saved), name .. "-Forever"
end

local w2, s2, B = fresh("Bjorn", 12)
eq(status(s2, B), 1, "first seen above level 1: self-found since tracking, never shown as verified")
eq(status(s2, A), 0, "and the account still remembers Ingrid")
s2.env.PostAuction(1, 2, 3)
eq(status(s2, B), 0, "posting an auction (old API) breaks it")

local w3, s3, C = fresh("Cora", 1)
s3.env.C_AuctionHouse.PostCommodity({}, 1, 1)
eq(status(s3, C), 0, "posting a commodity breaks it")

local w4, s4, D = fresh("Dagny", 1)
s4.fire("TRADE_ACCEPT_UPDATE", 1, 0)
s4.fire("UI_INFO_MESSAGE", 0, "Trade complete.")
eq(status(s4, D), 2, "a trade only one side accepted is not a trade")
s4.fire("TRADE_ACCEPT_UPDATE", 1, 1)
s4.fire("UI_INFO_MESSAGE", 0, "Trade complete.")
eq(status(s4, D), 2, "an empty completed trade moves nothing")
w4.trade.target[1] = "|Hitem:2589:0|h[Linen]|h"; w4.trade.got = 50
s4.fire("TRADE_ACCEPT_UPDATE", 1, 1)
s4.fire("TRADE_CLOSED")
eq(status(s4, D), 2, "a trade that closes without completing is not a trade")
s4.fire("TRADE_ACCEPT_UPDATE", 1, 1)
s4.fire("UI_INFO_MESSAGE", 0, "Trade complete.")
eq(status(s4, D), 0, "a completed trade that moved something breaks it")
eq(last(s4).kind, 8, "recorded as a trade")
eq(last(s4).a, 1, "with the item count")
eq(last(s4).b, 50, "and the money received")

local w5, s5, E = fresh("Eir", 1)
w5.inbox = { { sender = "Ingrid", item = 4540, count = 2, reply = true } }
s5.env.TakeInboxItem(1, 1)
eq(status(s5, E), 0, "taking an item mailed by an alt breaks it")
eq(last(s5).kind, 9, "recorded as player mail")
eq(last(s5).a, 4540, "with the item")

local w6, s6, F = fresh("Frode", 1)
w6.inbox = { { sender = "Stranger", money = 1000, reply = true } }
s6.env.AutoLootMailItem(1)
eq(status(s6, F), 0, "taking a player's money by auto-loot breaks it")
eq(last(s6).c, 1000, "with the money")

local w7, s7, G = fresh("Gudrun", 1)
s7.env.DepositGuildBankMoney(100)
eq(status(s7, G), 2, "depositing into a guild bank does not break it")
s7.fire("GUILDBANKFRAME_OPENED")
w7.bags[2589] = nil
s7.fire("BAG_UPDATE_DELAYED")
eq(last(s7).kind, 13, "items leaving the bags at the guild bank are a deposit")
eq(status(s7, G), 2, "still self-found")
w7.bags[6948] = 1
s7.fire("BAG_UPDATE_DELAYED")
eq(last(s7).kind, 12, "an item arriving in the bags at the guild bank is a withdrawal")
eq(last(s7).a, 6948, "of that item")
eq(status(s7, G), 0, "and breaks it")
s7.fire("GUILDBANKFRAME_CLOSED")

local w8, s8, H = fresh("Hakon", 1)
s8.env.WithdrawGuildBankMoney(500)
eq(status(s8, H), 0, "withdrawing guild-bank gold breaks it")

-- the rules on their own
local S = s.env.RefluxCore.modules["collection/selffound"]
local breaks = {}
for k = 1, S.sfKLast() do if S.sfViolates(k) == 1 then breaks[#breaks + 1] = k end end
eq(table.concat(breaks, ","), "6,7,8,9,12", "exactly AH buy, AH post, trade, player mail and guild-bank withdraw break it")
eq(S.sfRecord(saved, "X-Forever", 1, 0, 0, 0, 0, 1, nil, nil, nil), -1, "kind 0 is refused")
eq(S.sfRecord(saved, "X-Forever", 1, 16, 0, 0, 0, 1, nil, nil, nil), -1, "a kind past the last is refused")

-- the size cap drops the oldest rows
local small = S.sfInit({})
for i = 1, S.sfKeep() + 5 do S.sfRecord(small, "Y-Forever", 1, 14, i, 1, 0, i, nil, nil, nil) end
eq(small.nextSeq - small.firstSeq, S.sfKeep(), "at most sfKeep rows are held")
eq(small.tx[small.firstSeq - 1], nil, "the oldest are gone")
eq(small.tx[small.nextSeq - 1].a, S.sfKeep() + 5, "the newest is kept")

print(string.format("selffound: %d passed, %d failed", passed, failed))
os.exit(failed == 0 and 0 or 1)
