-- runtime/wow/hrungnir_talk.lua -- talking to the rock. Hrungnir is the account's front desk: ask it about every
-- character, tell it what someone needs, and at a mailbox it packs the shipment (founder, 2026-11-14: "can i like
-- talk to the rock and get him to use regex to que up mail suggestions ... a character can say a rumor and then when
-- you are at the mail the rock packs an amazon shipment" / "i want the rock to be the main affordance i should be
-- able to query it for character data including bag free space and coin data").
--
-- /rock gold                 every character's coin, and the account's (or name one: /rock how much gold has ash)
-- /rock bags                 every character's free bag space, and empty bag slots (or name one)
-- /rock levels               every character's level, class and where it was
-- /rock who has <item>       who holds it, and where (also: where is <item>, find <item>)
-- /rock i need <n> <item>    tell the account (also: i need a bag, <name> needs <n> <item>, <name> wants a bag)
-- /rock mail                 what this character can send to whom (also: ship, post, deliver, amazon)
--
-- What the words mean is PARENA (stdlib/collection/rocktalk.prn, RefluxCore.modules["collection/rocktalk"]: the
-- wordings as Lua patterns, which requests this character can fill, which stacks to attach). The data is the account's
-- own: Fafnir's Bed (coin, bags, bank, mail, bag space, requests, item facts) and the Character Board (level, class,
-- zone), both account-wide, so every character sees every other one as of its last logout or /reload -- and itself live.
-- A request is the same record /need writes (FafnirsBedDB.requests): the rock passes it on as a rumour at the next
-- login of a character that can fill it, and at a mailbox shows a Pack button for each. Pack fills in the recipient
-- and subject and attaches the stacks; you press Send. A sent shipment takes its count off the request.
--
-- Client calls: the mail frame's own (MailFrameTab2, SendMailNameEditBox, SendMailSubjectEditBox, GetSendMailItem,
-- ClickSendMailItemButton, ATTACHMENTS_MAX_SEND), C_Container.PickupContainerItem or PickupContainerItem, the bag
-- calls Fafnir's Bed uses, InCombatLockdown, C_Timer.After; events MAIL_SHOW, MAIL_CLOSED, MAIL_SEND_SUCCESS,
-- MAIL_FAILED, PLAYER_LOGIN. Feature-tested; none probed on Forever yet. English only for now (not in the catalogue).

local ADDON = "@ADDON@"
local FAFNIR = "@FAFNIR_VAR@"
local BOARD = "@BOARD_VAR@"

local T = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/rocktalk"]
if not T then return end

local function say(fmt, ...) print("|cffc8a878Hrungnir:|r " .. string.format(fmt, ...)) end
local function line(fmt, ...) print("  " .. string.format(fmt, ...)) end

local function me()
    local name = UnitName and UnitName("player")
    local realm = GetRealmName and GetRealmName()
    if not name then return nil end
    return (realm and realm ~= "") and (name .. "-" .. realm) or name
end
local function fdb() return _G[FAFNIR] end
local function bdb() return _G[BOARD] end
local function inv() return RefluxCore.modules["collection/inventory"] end

local function money(c)
    c = math.floor(tonumber(c) or 0)
    return string.format("%dg %ds %dc", math.floor(c / 10000), math.floor(c / 100) % 100, c % 100)
end

local function item_name(id)
    local f = fdb()
    return (f and f.names and f.names[id]) or ("item " .. tostring(id))
end

-- Every character either addon has seen, sorted.
local function everyone()
    local seen, list = {}, {}
    for _, db in ipairs({ fdb(), bdb() }) do
        for who in pairs(db and db.chars or {}) do if not seen[who] then seen[who] = true; list[#list + 1] = who end end
    end
    table.sort(list)
    return list
end

-- A character named anywhere in the words ("how much gold does ash have"), by its name part, this realm first.
-- An exact name wins; failing that, a word of 4+ letters that starts exactly one name ("emi" is too short to trust).
local function named_in(text)
    local mine = T.rockRealmOf(me() or "")
    local function better(hit, who) return not hit or (T.rockRealmOf(who) == mine and T.rockRealmOf(hit) ~= mine) end
    local exact, prefix, prefixes = nil, nil, 0
    for _, who in ipairs(everyone()) do
        local n = T.rockNameOf(who):lower()
        for word in text:gmatch("%a+") do
            if word == n then if better(exact, who) then exact = who end
            elseif #word >= 4 and n:sub(1, #word) == word then
                if prefix ~= who then prefixes = prefixes + 1 end
                if better(prefix, who) then prefix = who end
            end
        end
    end
    return exact or (prefixes == 1 and prefix) or nil
end

local function chars_for(text)
    local one = named_in(text)
    if one then return { one } end
    return everyone()
end

-- ---- questions ---------------------------------------------------------------------------------------------------
local function gold(text)
    local f = fdb()
    if not f then say("I can't see anyone's coin without Fafnir's Bed."); return end
    local list, total = chars_for(text), 0
    if #list == 1 then
        local c = f.chars and f.chars[list[1]]
        say("%s has %s.", list[1], c and money(c.gold) or "no coin I know of")
        return
    end
    say("Coin on the account:")
    for _, who in ipairs(list) do
        local c = f.chars and f.chars[who]
        if c then total = total + (c.gold or 0); line("%-22s %s", who, money(c.gold)) end
    end
    line("%-22s %s", "all together", money(total))
end

local function bags(text)
    local f = fdb()
    if not f then say("I can't see anyone's bags without Fafnir's Bed."); return end
    local list = chars_for(text)
    say(#list == 1 and "Bag space:" or "Bag space on the account:")
    local any = false
    for _, who in ipairs(list) do
        local sp = f.space and f.space[who]
        if sp then
            any = true
            local warn = (sp.free == 0 and "  FULL") or (sp.free <= 4 and "  nearly full") or ""
            local empty = (sp.max or 0) - (sp.bags or 0)
            line("%-22s %d of %d free%s%s", who, sp.free or 0, sp.total or 0, warn,
                 empty > 0 and string.format("  (%d bag slot%s empty)", empty, empty == 1 and "" or "s") or "")
        elseif #list == 1 then
            line("%s: not seen since Fafnir's Bed learned to count bag space -- log in once.", who)
        end
    end
    if not any and #list > 1 then line("nobody's bag space is known yet -- log each character in once") end
end

local function levels(text)
    local b, list = bdb(), chars_for(text)
    say("The account:")
    for _, who in ipairs(list) do
        local c = b and b.chars and b.chars[who]
        if c then
            line("%-22s %-9s %2s  %s%s", who, c.class or "", tostring(c.level or "?"), c.zone or "",
                 (c.subzone and c.subzone ~= "") and (" - " .. c.subzone) or "")
        end
    end
end

local function trim_item(s)
    s = (s or ""):gsub("^the ", ""):gsub("^my ", ""):gsub("^some ", ""):gsub("[%?%.!]+$", "")
    return s
end

local function find(text, item)
    local f, I = fdb(), inv()
    if not (f and I) then say("I can't search without Fafnir's Bed."); return end
    item = trim_item(item)
    local ids = I.invFind(f, item)
    if #ids == 0 then say("Nobody has anything called '%s' that I've seen.", item); return end
    say("Who has '%s':", item)
    for k = 1, math.min(#ids, 6) do
        local who = I.invWho(f, ids[k])
        local parts = {}
        for name, n in pairs(who) do parts[#parts + 1] = name .. " " .. n end
        table.sort(parts)
        line("%s: %s", item_name(ids[k]), #parts > 0 and table.concat(parts, ", ") or "nobody now")
    end
    if #ids > 6 then line("...and %d more", #ids - 6) end
end

-- ---- requests: the rumour ------------------------------------------------------------------------------------------
local function shipments_for(who)
    local f = fdb()
    if not (f and who and f.chars and f.chars[who]) then return {} end
    local loc = f.chars[who].loc or {}
    local list = T.rockShipments(f.requests, who, loc.bags or {}, f.items, f.bound and f.bound[who])
    table.sort(list, function(a, b)
        if a.to ~= b.to then return a.to < b.to end
        return a.index < b.index
    end)
    return list
end

local function request(it)
    local f, I = fdb(), inv()
    if not f then say("Without Fafnir's Bed I have nowhere to write it down."); return end
    local who = me()
    if it.who and it.who ~= "i" then
        who = named_in(it.who)
        if not who then say("I don't know a character called %s.", it.who); return end
    end
    local item = trim_item(it.item)
    local count = it.count or 1
    local r
    if item == "bag" or item == "bags" or item:match(" bags?$") then
        r = { what = "bag", name = "a bag", count = count, t = time() }
    else
        local id = tonumber(item) or tonumber(item:match("item:(%d+)"))
        if not id and I then
            local ids = I.invFind(f, item)
            if #ids == 1 then id = ids[1]
            elseif #ids > 1 then
                say("More than one thing is called like '%s'; which one?", item)
                for k = 1, math.min(#ids, 5) do line("%s  (%s)", item_name(ids[k]), ids[k]) end
                line("say it again with the id: /rock i need %d %s", count, ids[1])
                return
            end
        end
        r = { what = id or item, name = id and item_name(id) or item, count = count, t = time() }
    end
    f.requests = f.requests or {}
    f.requests[who] = f.requests[who] or {}
    table.insert(f.requests[who], r)
    say("I'll spread the word: %s needs %s x%d.", who, r.name, r.count)
    if who ~= me() then
        for _, s in ipairs(shipments_for(me())) do
            if s.to == who and s.index == #f.requests[who] then
                line("You could send %d yourself -- open a mailbox and I'll pack it.", s.count)
            end
        end
    else
        line("Whoever can fill it hears about it at their next login; at a mailbox I pack it for them.")
    end
end

local function describe(s)
    local what = s.bag == 1 and (item_name(s.what) .. " (for a bag)") or item_name(s.what)
    return string.format("%s needs %s x%d -- you have %d", s.to, what, s.want, s.have)
end

local function list_shipments()
    local list = shipments_for(me())
    if #list == 0 then say("Nothing anyone asked for is in your bags. (/rock i need ... to ask)"); return end
    say("You could send:")
    for _, s in ipairs(list) do line("%s", describe(s)) end
    line("Open a mailbox and I'll pack them.")
end

-- ---- the mailbox ---------------------------------------------------------------------------------------------------
local panel, rows, pending = nil, {}, nil

local function bag_stacks(id)
    local out = {}
    local last = type(NUM_BAG_SLOTS) == "number" and NUM_BAG_SLOTS or 4
    for bag = 0, last do
        local n = (C_Container and C_Container.GetContainerNumSlots and C_Container.GetContainerNumSlots(bag))
            or (GetContainerNumSlots and GetContainerNumSlots(bag)) or 0
        for slot = 1, n do
            local sid, count, locked
            if C_Container and C_Container.GetContainerItemInfo then
                local info = C_Container.GetContainerItemInfo(bag, slot)
                if info then sid, count, locked = info.itemID, info.stackCount, info.isLocked end
            elseif GetContainerItemInfo then
                local _
                _, count, locked, _, _, _, _, _, _, sid = GetContainerItemInfo(bag, slot)
            end
            if sid == id and count and not locked then out[#out + 1] = { bag, slot, count } end
        end
    end
    return out
end

local function pickup(bag, slot)
    if C_Container and C_Container.PickupContainerItem then C_Container.PickupContainerItem(bag, slot)
    elseif PickupContainerItem then PickupContainerItem(bag, slot) end
end

local refresh

local function pack(s)
    if InCombatLockdown and InCombatLockdown() then say("Not in a fight."); return end
    if not (SendMailNameEditBox and ClickSendMailItemButton) then say("I can't reach the send-mail form on this client."); return end
    if MailFrameTab2 and MailFrameTab2.Click then MailFrameTab2:Click() end
    local max = type(ATTACHMENTS_MAX_SEND) == "number" and ATTACHMENTS_MAX_SEND or 12
    if GetSendMailItem then
        for i = 1, max do
            if GetSendMailItem(i) then say("Clear the attachments first -- I only pack one shipment at a time."); return end
        end
    end
    local stacks = bag_stacks(s.what)
    if #stacks == 0 then say("I can't find %s in your bags any more (or it's locked).", item_name(s.what)); return end
    local p = T.rockPickStacks(stacks, s.count, max)
    SendMailNameEditBox:SetText(T.rockNameOf(s.to))
    if SendMailSubjectEditBox then
        SendMailSubjectEditBox:SetText(string.format("From the rock: %s x%d", item_name(s.what), p.total))
    end
    for k, idx in ipairs(p.picks) do
        pickup(stacks[idx][1], stacks[idx][2])
        ClickSendMailItemButton(k)
    end
    pending = { to = s.to, index = s.index, what = s.what, bag = s.bag, sent = p.total }
    say("Packed %s x%d for %s. Press Send.", item_name(s.what), p.total, s.to)
    if p.total > s.count then line("(%d asked for -- a mail can't split a stack; split it first for the exact count)", s.count) end
    if p.total < s.count then line("(%d asked for -- that is all that fits in one mail; I'll pack the rest next)", s.count) end
end

-- A sent shipment comes off the request it filled.
local function settle()
    local f = fdb()
    if not (pending and f and f.requests and f.requests[pending.to]) then pending = nil; return end
    local list = f.requests[pending.to]
    local r = list[pending.index]
    if r and (r.what == pending.what or (pending.bag == 1 and r.what == "bag") or tonumber(r.what) == pending.what) then
        r.count = (r.count or 1) - (pending.bag == 1 and 1 or pending.sent)
        if r.count <= 0 then
            table.remove(list, pending.index)
            say("Shipped. %s's request is filled.", pending.to)
        else
            say("Shipped. %s still needs %d.", pending.to, r.count)
        end
    end
    pending = nil
end

local function build_panel()
    if panel or not CreateFrame then return end
    panel = CreateFrame("Frame", ADDON .. "Shipments", MailFrame or UIParent)
    if panel.SetSize then panel:SetSize(300, 40) end
    if panel.SetPoint then panel:SetPoint("TOPLEFT", MailFrame or UIParent, "TOPRIGHT", 4, 0) end
    if panel.CreateFontString then
        panel.title = panel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        panel.title:SetPoint("TOPLEFT", panel, "TOPLEFT", 8, -8)
        panel.title:SetText("Hrungnir packs:")
    end
    for i = 1, 6 do
        local b = CreateFrame("Button", ADDON .. "Ship" .. i, panel, "UIPanelButtonTemplate")
        if b.SetSize then b:SetSize(284, 22) end
        if b.SetPoint then b:SetPoint("TOPLEFT", panel, "TOPLEFT", 8, -8 - 24 * i) end
        b:SetScript("OnClick", function(self) if self.ship then pack(self.ship) end end)
        rows[i] = b
    end
end

refresh = function()
    local list = shipments_for(me())
    if #list == 0 then if panel and panel.Hide then panel:Hide() end return end
    build_panel()
    if not panel then return end
    for i, b in ipairs(rows) do
        local s = list[i]
        b.ship = s
        if s then
            if b.SetText then b:SetText(string.format("Pack %s x%d for %s", item_name(s.what), s.count, T.rockNameOf(s.to))) end
            if b.Show then b:Show() end
        elseif b.Hide then b:Hide() end
    end
    if panel.SetSize then panel:SetSize(300, 16 + 24 * (math.min(#list, 6) + 1)) end
    if panel.Show then panel:Show() end
end

-- ---- events and the slash command ----------------------------------------------------------------------------------
local events = CreateFrame("Frame")
for _, e in ipairs({ "PLAYER_LOGIN", "MAIL_SHOW", "MAIL_CLOSED", "MAIL_SEND_SUCCESS", "MAIL_FAILED", "BAG_UPDATE_DELAYED" }) do
    events:RegisterEvent(e)
end
local at_mail = false
local function rumours()
    local list = shipments_for(me())
    for _, s in ipairs(list) do say("Rumour has it %s. I'll pack it at a mailbox.", describe(s)) end
end
events:SetScript("OnEvent", function(_, event)
    if event == "PLAYER_LOGIN" then
        -- the bags are often not loaded at login; Fafnir's Bed looks again after 3 s, so the rumours wait 4
        if C_Timer and C_Timer.After then C_Timer.After(4, rumours) else rumours() end
    elseif event == "MAIL_SHOW" then
        at_mail = true; refresh()
    elseif event == "MAIL_CLOSED" then
        at_mail = false; pending = nil
        if panel and panel.Hide then panel:Hide() end
    elseif event == "MAIL_SEND_SUCCESS" then
        settle(); if at_mail then refresh() end
    elseif event == "MAIL_FAILED" then
        pending = nil
    elseif event == "BAG_UPDATE_DELAYED" then
        if at_mail and not pending then refresh() end
    end
end)

local base = SlashCmdList and SlashCmdList["HRUNGNIR"]
if base then
    SlashCmdList["HRUNGNIR"] = function(msg)
        local text = ((msg or ""):match("^%s*(.-)%s*$") or ""):lower()
        local it = T.rockIntent(text)
        if text == "" or it.intent == 0 or it.intent >= 7 then
            base(msg)
            if text == "" or it.intent == 0 then
                line("Ask me: gold | bags | levels | who has <item> | i need <n> <item> | emily needs a bag | mail")
            end
            return
        end
        if it.intent == 1 then gold(text)
        elseif it.intent == 2 then bags(text)
        elseif it.intent == 3 then find(text, it.item)
        elseif it.intent == 4 then levels(text)
        elseif it.intent == 5 then request(it)
        elseif it.intent == 6 then list_shipments() end
    end
end

HrungnirTalk = { shipments = shipments_for, pack = pack, refresh = refresh }
