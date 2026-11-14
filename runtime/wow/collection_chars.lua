-- runtime/wow/collection_chars.lua -- the Collection's character screen: every character on the account in one window,
-- with its level, gold, gear and inventory (founder, 2026-11-14: "i want to see the inv and levels and equip of the
-- chars, including monies -- ideally all from one in game screen"). Part of the CollectionHub addon; what it decides
-- (gold, xp, who is listed, in what order) is PARENA (stdlib/collection/hub.prn).
--
-- /chars             open or close the character screen (also the Characters button in /collection)
-- /chars <name>      open it on that character
-- /chars find <item> who has it, and where (also the Find box at the top of the window)
--
-- Reads two account-wide SavedVariables, so every character shows, not only the one logged in: the Character Board's
-- latest snapshot of each character (level, xp, rested, gold, zone, item level, played, professions) and Fafnir's
-- Bed's holdings (gold, what is worn by slot, bags, bank, mail). A character the Board has not seen since it was
-- installed still shows what Fafnir knows, and the other way round. MacroSync's planned kit for the character (baked
-- in by the plugin forge, published as MacroSyncKits) is listed too when there is one.
-- What is shown is as of each character's last login: the bank is read when it was last opened, mail when the
-- mailbox was last opened.
--
-- Client calls (feature-tested; none probed on Forever yet): CreateFrame, UIParent, GetItemInfo (names and quality
-- of items the client has cached; otherwise the name Fafnir last saw in a link), ITEM_QUALITY_COLORS, date.

local _, ns = ...
local H = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/hub"]
if not (H and ns.build) then return end

local function L(key)
    local c = ns.catalogue
    if not c then return key end
    local t = c[GetLocale and GetLocale() or "enUS"]
    return (t and t[key]) or (c.enUS and c.enUS[key]) or key
end

local GOLD, GREY, WHITE = "|cffd8b25e", "|cff9d9d9d", "|cffffffff"

local progress           -- defined below, used by sheet

local function db_of(var) local t = var and _G[var]; return type(t) == "table" and t or nil end
local function board() return db_of(ns.build.board) end
local function fafnir() return db_of(ns.build.fafnir) end

local function money(copper)
    local p = H.hubGoldParts(math.floor(tonumber(copper) or 0))
    return string.format("%s%dg|r %ds %dc", GOLD, p[1], p[2], p[3])
end

local function item(id, names)
    local name, quality
    if GetItemInfo then local n, _, q = GetItemInfo(id); name, quality = n, q end
    name = name or (names and names[id]) or ("item " .. tostring(id))
    local c = quality and ITEM_QUALITY_COLORS and ITEM_QUALITY_COLORS[quality]
    if c and c.hex then return c.hex .. name .. "|r" end
    return WHITE .. name .. "|r"
end

-- One character's facts, from whichever table has them.
local function facts(who)
    local b, f = board(), fafnir()
    local snap = b and b.chars and b.chars[who] or nil
    local hold = f and f.chars and f.chars[who] or nil
    return snap, hold, f and f.names or nil
end

-- Every character, highest level first.
local function roster()
    local list = {}
    for who in pairs(H.hubRoster(board(), fafnir())) do
        local snap = facts(who)
        list[#list + 1] = { who = who, level = snap and tonumber(snap.level) or 0 }
    end
    table.sort(list, function(a, b) return H.hubCharBefore(a.level, a.who, b.level, b.who) == 1 end)
    return list
end

local function gold_of(who)
    local snap, hold = facts(who)
    -- Fafnir follows every PLAYER_MONEY; the Board's snapshot is as of its last snapshot
    if hold and hold.gold then return hold.gold end
    return snap and snap.gold or 0
end

local function sorted_items(t, names)
    local rows = {}
    for id, n in pairs(t or {}) do rows[#rows + 1] = { id = id, n = n, name = (GetItemInfo and GetItemInfo(id)) or (names and names[id]) or "" } end
    table.sort(rows, function(a, b) if a.name ~= b.name then return a.name < b.name end return a.id < b.id end)
    return rows
end

local function played(sec)
    sec = tonumber(sec) or 0
    if sec <= 0 then return nil end
    return string.format("%dd %dh", math.floor(sec / 86400), math.floor(sec / 3600) % 24)
end

-- The Character Board's snapshots of one character, the last one of each day, oldest first; at most `max` days.
progress = function(who, max)
    local b = board()
    local rows = {}
    for _, r in ipairs(b and b.rows or {}) do if r.character == who and r.t then rows[#rows + 1] = r end end
    table.sort(rows, function(x, y) return x.t < y.t end)
    local days = {}
    for _, r in ipairs(rows) do
        local d = date and date("%Y-%m-%d", r.t) or tostring(r.t)
        local entry = { date = d, level = r.level, gold = r.gold or 0 }
        if days[#days] and days[#days].date == d then days[#days] = entry else days[#days + 1] = entry end
    end
    while #days > max do table.remove(days, 1) end
    return days
end

-- Who has what: every item whose name holds `q` (or whose id it is), the account total, and who holds it where.
local LOCS = { "bags", "bank", "mail", "equipped" }
local function search(q)
    local f = fafnir()
    local lines = { GOLD .. string.format(L("C_FOUND"), q) .. "|r" }
    if not f then lines[2] = GREY .. L("C_NOFAFNIR") .. "|r"; return lines end
    local lq, id_q = q:lower(), tonumber(q)
    local hits = {}
    for id, name in pairs(f.names or {}) do
        if id == id_q or tostring(name):lower():find(lq, 1, true) then
            local holders, total = {}, 0
            for who, ch in pairs(f.chars or {}) do
                local n, where = 0, {}
                for _, l in ipairs(LOCS) do
                    local k = ch.loc and ch.loc[l] and ch.loc[l][id] or 0
                    if k > 0 then n = n + k; where[#where + 1] = L("C_" .. l:upper()):lower() .. " " .. k end
                end
                if n > 0 then holders[#holders + 1] = { who = who, n = n, where = table.concat(where, ", ") }; total = total + n end
            end
            table.sort(holders, function(a, b) if a.n ~= b.n then return a.n > b.n end return a.who < b.who end)
            hits[#hits + 1] = { id = id, total = total, holders = holders, name = tostring(name) }
        end
    end
    table.sort(hits, function(a, b) if a.total ~= b.total then return a.total > b.total end return a.name < b.name end)
    for i, h in ipairs(hits) do
        if i > 30 then lines[#lines + 1] = GREY .. string.format(L("C_MORE"), #hits - 30) .. "|r"; break end
        lines[#lines + 1] = ""
        lines[#lines + 1] = item(h.id, f.names) .. "  " .. GREY .. string.format(L("C_TOTAL"), h.total) .. "|r"
        for _, hd in ipairs(h.holders) do lines[#lines + 1] = "  " .. hd.who .. "  " .. hd.n .. "  " .. GREY .. "(" .. hd.where .. ")|r" end
    end
    if #hits == 0 then lines[#lines + 1] = GREY .. L("C_NOTHING") .. "|r" end
    return lines
end

-- The whole sheet of one character, as lines of text.
local function sheet(who)
    local snap, hold, names = facts(who)
    local lines = {}
    local function add(s) lines[#lines + 1] = s end
    local head = GOLD .. who .. "|r"
    if snap then
        head = head .. "   " .. string.format(L("C_LEVEL"), tonumber(snap.level) or 0, tostring(snap.race or ""), tostring(snap.class or ""))
    end
    add(head)
    if snap then
        local pct = H.hubXpPct(tonumber(snap.xp) or 0, tonumber(snap.xpmax) or 0)
        local rested = H.hubXpPct(tonumber(snap.rested) or 0, tonumber(snap.xpmax) or 0)
        add(string.format(L("C_XP"), pct, rested) .. "   " .. string.format(L("C_ILVL"), tonumber(snap.ilvl) or 0) ..
            (played(snap.played) and ("   " .. string.format(L("C_PLAYED"), played(snap.played))) or ""))
        add(string.format(L("C_ZONE"), tostring(snap.zone or "?"), (date and snap.t) and date("%Y-%m-%d %H:%M", snap.t) or "?"))
        if snap.profs and snap.profs ~= "" then add(string.format(L("C_PROFS"), snap.profs)) end
    else
        add(GREY .. L("C_NOBOARD") .. "|r")
    end
    add(string.format(L("C_GOLD"), money(gold_of(who))))
    add("")

    add(GOLD .. L("C_GEAR") .. "|r")
    local gear = hold and hold.gear
    if gear and next(gear) then
        for slot = 1, 19 do
            if gear[slot] then add("  " .. GREY .. L("SLOT_" .. slot) .. "|r  " .. item(gear[slot], names)) end
        end
    else
        add("  " .. GREY .. L("C_NOGEAR") .. "|r")
    end

    for _, loc in ipairs({ "bags", "bank", "mail" }) do
        add("")
        local t = hold and hold.loc and hold.loc[loc]
        local rows = sorted_items(t, names)
        local total = 0
        for _, r in ipairs(rows) do total = total + r.n end
        if t then
            add(GOLD .. L("C_" .. loc:upper()) .. "|r  " .. GREY .. string.format(L("C_COUNT"), #rows, total) .. "|r")
            for _, r in ipairs(rows) do add("  " .. item(r.id, names) .. (r.n > 1 and ("  x" .. r.n) or "")) end
        else
            add(GOLD .. L("C_" .. loc:upper()) .. "|r  " .. GREY .. L("C_NOTREAD_" .. loc:upper()) .. "|r")
        end
    end

    -- the last week of the Character Board's snapshots: level and gold at the end of each day
    local days = progress(who, 7)
    if #days > 1 then
        add("")
        add(GOLD .. L("C_PROGRESS") .. "|r")
        for i = #days, 1, -1 do
            local d, prev = days[i], days[i - 1]
            local delta = prev and ((tonumber(d.gold) or 0) - (tonumber(prev.gold) or 0)) or nil
            add(string.format("  %s%s|r  " .. L("C_DAYLEVEL") .. "  %s%s", GREY, d.date, tonumber(d.level) or 0, money(d.gold),
                delta and (GREY .. "  (" .. (delta >= 0 and "+" or "-") .. string.format("%dg", math.floor(math.abs(delta) / 10000)) .. ")|r") or ""))
        end
    end

    local kits = type(MacroSyncKits) == "table" and MacroSyncKits or nil
    local plan = kits and kits[who]
    if plan then
        add("")
        add(GOLD .. L("C_KIT") .. "|r")
        local parts = {}
        for name in pairs(plan.drop or {}) do parts[#parts + 1] = "-" .. name end
        for name in pairs(plan.edit or {}) do parts[#parts + 1] = "~" .. name end
        for _, m in ipairs(plan.add or {}) do parts[#parts + 1] = "+" .. m[1] end
        table.sort(parts)
        add("  " .. table.concat(parts, "  "))
    end
    return lines
end

local function list_in_chat(who)
    for _, line in ipairs(sheet(who)) do print(line) end
end

local query = nil      -- when set, the right side shows who has what instead of a character

-- The window: the account across the top, a list of characters on the left, the chosen one's sheet on the right.
local window, selected
local LIST_W, ROW_H, LINE_H = 230, 22, 14

local function make_window()
    if window or not (CreateFrame and UIParent) then return window end
    local f = CreateFrame("Frame", "CollectionCharsFrame", UIParent)
    f:SetSize(860, 580)
    f:SetPoint("CENTER", UIParent, "CENTER", 0, 30)
    if f.SetFrameStrata then f:SetFrameStrata("DIALOG") end
    local bg = f:CreateTexture(nil, "BACKGROUND")
    bg:SetAllPoints(f)
    if bg.SetColorTexture then bg:SetColorTexture(0.06, 0.05, 0.04, 0.94) else bg:SetTexture(0.06, 0.05, 0.04, 0.94) end
    f:EnableMouse(true)
    f:SetMovable(true)
    f:RegisterForDrag("LeftButton")
    f:SetScript("OnDragStart", function(self) self:StartMoving() end)
    f:SetScript("OnDragStop", function(self) self:StopMovingOrSizing() end)
    local ok, close = pcall(CreateFrame, "Button", nil, f, "UIPanelCloseButton")
    if not ok then close = CreateFrame("Button", nil, f); close:SetSize(60, 20); close:SetText(L("CLOSE")) end
    close:SetPoint("TOPRIGHT", f, "TOPRIGHT", -4, -4)
    close:SetScript("OnClick", function() f:Hide() end)

    local title = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    title:SetPoint("TOPLEFT", f, "TOPLEFT", 16, -14)
    title:SetText(L("C_TITLE"))
    local account = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    account:SetPoint("TOPLEFT", f, "TOPLEFT", 16, -38)

    -- who has what: type part of an item's name (or its id) and press Enter
    local label = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    label:SetPoint("TOPLEFT", f, "TOPLEFT", LIST_W + 20, -38)
    label:SetText(L("C_FIND"))
    local okb, box = pcall(CreateFrame, "EditBox", nil, f, "InputBoxTemplate")
    if not okb then box = CreateFrame("EditBox", nil, f) end
    box:SetSize(220, 20)
    box:SetPoint("TOPLEFT", f, "TOPLEFT", LIST_W + 110, -34)
    if box.SetAutoFocus then box:SetAutoFocus(false) end
    f.findbox = box

    -- left: one button per character
    local list = CreateFrame("ScrollFrame", nil, f)
    list:SetPoint("TOPLEFT", f, "TOPLEFT", 8, -60)
    list:SetPoint("BOTTOMLEFT", f, "BOTTOMLEFT", 8, 8)
    if list.SetWidth then list:SetWidth(LIST_W) end
    local listbody = CreateFrame("Frame", nil, list)
    list:SetScrollChild(listbody)

    -- right: the sheet, one long text that scrolls
    local scroll = CreateFrame("ScrollFrame", nil, f)
    scroll:SetPoint("TOPLEFT", f, "TOPLEFT", LIST_W + 20, -60)
    scroll:SetPoint("BOTTOMRIGHT", f, "BOTTOMRIGHT", -12, 8)
    local body = CreateFrame("Frame", nil, scroll)
    scroll:SetScrollChild(body)
    local text = body:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    text:SetPoint("TOPLEFT", body, "TOPLEFT", 4, -4)
    local sheet_w = 860 - LIST_W - 40
    if text.SetWidth then text:SetWidth(sheet_w - 8) end
    if text.SetJustifyH then text:SetJustifyH("LEFT") end
    if text.SetJustifyV then text:SetJustifyV("TOP") end
    local sheet_h = 0
    for _, s in ipairs({ list, scroll }) do
        if s.EnableMouseWheel then
            s:EnableMouseWheel(true)
            s:SetScript("OnMouseWheel", function(self, delta)
                local child_h = self == scroll and sheet_h or (#roster() * ROW_H + 8)
                local max = math.max(0, child_h - (self.GetHeight and self:GetHeight() or 0))
                self:SetVerticalScroll(math.min(max, math.max(0, (self:GetVerticalScroll() or 0) - delta * 3 * LINE_H)))
            end)
        end
    end

    local buttons = {}
    local function show_sheet()
        if not query and not selected then text:SetText(GREY .. L("C_PICK") .. "|r"); return end
        local lines = query and search(query) or sheet(selected)
        text:SetText(table.concat(lines, "\n"))
        sheet_h = #lines * LINE_H + 16
        if text.SetHeight then text:SetHeight(sheet_h) end
        body:SetSize(sheet_w, sheet_h)
        if scroll.SetVerticalScroll then scroll:SetVerticalScroll(0) end
    end

    local function refresh()
        local people = roster()
        local total = 0
        for _, p in ipairs(people) do total = total + (tonumber(gold_of(p.who)) or 0) end
        account:SetText(string.format(L("C_ACCOUNT"), #people, money(total)))
        if not selected and people[1] then selected = people[1].who end
        listbody:SetSize(LIST_W, #people * ROW_H + 8)
        for i, p in ipairs(people) do
            local b = buttons[i]
            if not b then
                local okb, nb = pcall(CreateFrame, "Button", nil, listbody, "UIPanelButtonTemplate")
                if not okb then nb = CreateFrame("Button", nil, listbody) end
                nb:SetSize(LIST_W - 8, ROW_H - 2)
                nb:SetPoint("TOPLEFT", listbody, "TOPLEFT", 4, -((i - 1) * ROW_H + 4))
                nb:SetScript("OnClick", function(self) selected = self.who; query = nil; show_sheet() end)
                buttons[i] = nb
                b = nb
            end
            b.who = p.who
            local short = p.who:match("^([^-]+)") or p.who
            b:SetText(string.format("%s  %s  %s", short, p.level > 0 and tostring(p.level) or "?", money(gold_of(p.who))))
            b:Show()
        end
        for i = #people + 1, #buttons do buttons[i]:Hide() end
        if #people == 0 then text:SetText(GREY .. L("C_NONE") .. "|r") else show_sheet() end
    end

    box:SetScript("OnEnterPressed", function(self)
        local q = (self:GetText() or ""):match("^%s*(.-)%s*$")
        query = q ~= "" and q or nil
        if self.ClearFocus then self:ClearFocus() end
        show_sheet()
    end)
    box:SetScript("OnEscapePressed", function(self) if self.ClearFocus then self:ClearFocus() end end)
    f.show_sheet = show_sheet

    f:SetScript("OnShow", refresh)
    if UISpecialFrames then table.insert(UISpecialFrames, "CollectionCharsFrame") end
    f:Hide()
    f.refresh = refresh
    window = f
    return f
end

local function open(who)
    local w = make_window()
    if who then selected = who end
    if not w then
        if not selected then local r = roster(); selected = r[1] and r[1].who end
        if selected then list_in_chat(selected) else print(L("C_NONE")) end
        return
    end
    if w:IsShown() and not who then w:Hide() return end
    if w:IsShown() then w.refresh() else w:Show() end
end

-- "emily" / "Emily-Forever" / "em" -> the character, if exactly one matches.
local function find_char(q)
    q = q:lower()
    local hits = {}
    for _, p in ipairs(roster()) do
        local name = (p.who:match("^([^-]+)") or p.who):lower()
        if p.who:lower() == q then return p.who end
        if name == q or name:sub(1, #q) == q then hits[#hits + 1] = p.who end
    end
    if #hits == 1 then return hits[1] end
    return nil, #hits
end

SLASH_COLLECTIONCHARS1 = "/chars"
SlashCmdList["COLLECTIONCHARS"] = function(msg)
    msg = (msg or ""):match("^%s*(.-)%s*$")
    if msg == "" then return open(nil) end
    local q = msg:match("^[Ff][Ii][Nn][Dd]%s+(.+)$")
    if q then
        query = q
        local w = make_window()
        if not w then for _, line in ipairs(search(q)) do print(line) end return end
        if w.findbox and w.findbox.SetText then w.findbox:SetText(q) end
        if w:IsShown() then w.show_sheet() else w:Show() end
        return
    end
    query = nil
    local who, n = find_char(msg)
    if not who then print(string.format(n == 0 and L("C_NOSUCH") or L("C_MANY"), msg)) return end
    open(who)
end

ns.open_chars, ns.char_sheet, ns.char_search = open, sheet, search
