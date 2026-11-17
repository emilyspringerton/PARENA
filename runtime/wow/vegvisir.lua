-- runtime/wow/vegvisir.lua -- Vegvísir, the old map: host glue (spec addon 43, "our own maps"). The decisions are PARENA
-- (stdlib/collection/vegvisir.prn: the grid, charted fields, milestones, first arrivals); this file reads the client,
-- draws the parchment and opens the doors.
--
-- The founder, 2026-11-15: "all the interface affordances should come off the old map: shows where we have been in the
-- whole world on all characters, where all characters are, and global inventory" -- "glue all the plugins together
-- interface wise; it should be fun and connected and emergent".
--
-- /oldmap                         open the old map on its last page; again closes it
-- /oldmap world|here|hoard|folk   open it on that page
-- Every 3 s, and on every zone change, the map notes the field you stand in (a 32 x 32 grid of your map) and where
-- you are. Account-wide: every character draws on the same sheet.
--
-- Pages. THE WORLD: every land any of us has walked, how much of each is charted, who is there now; a row opens that
-- land. THIS LAND: the land you are in (or picked): the client's own map art when it offers it, tinted like old
-- vellum, fog over the fields none of us has crossed; pins for each of us (class-coloured), Andvari's buried Xs,
-- Rune Riddle's stops and Sleipnir's course, read straight from those addons' own saves. THE HOARD: Fafnir's Bed --
-- every bag, bank and mailbox, searched by name, who holds what. THE FOLK: every character we know of (this map,
-- Fafnir's Bed, the Character Board): class, level, where, when, gold; a row opens their land.
-- Under every page, THE DOORS: one stamp per addon in this package (registry.lua, written by the packager from each
-- addon's own .toc and code), which runs its command. A stamp on the minimap opens the map; drag it round the ring.
-- Without frames (a client with no CreateFrame), every page is printed in chat instead.
--
-- Client calls (feature-tested; C_Map.GetBestMapForUnit + GetPlayerMapPosition confirmed on Forever): C_Map.GetMapInfo
-- (a land's name), C_Map.GetMapArtLayers + GetMapArtLayerTextures (the map art; without them, plain parchment),
-- GetZoneText, UnitClass, RAID_CLASS_COLORS, CreateFrame/CreateTexture/CreateFontString, SetColorTexture or
-- SetTexture(r, g, b, a), FontString:SetFont (Fonts\MORPHEUS.TTF, the quest-title face), Minimap + GetCursorPosition
-- (the stamp), GameTooltip, UISpecialFrames (Escape closes), UIPanelCloseButton / UIPanelButtonTemplate /
-- InputBoxTemplate (each pcall'd, with a plain widget when the template is missing).
local ADDON, VAR = "@ADDON@", "@VAR@"
local _, ns = ...
local K = ns.kit
local V = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/vegvisir"]
if not V then print(string.format((ns.catalogue and ns.catalogue.enUS.MISSING) or "%s: module missing", ADDON)) return end
K.saved(VAR, V.vvInit)

local INK = "|cff8c6b27"
local PATH = "Interface\\AddOns\\" .. ADDON .. "\\parchment"
local MAPW, MAPH = 600, 400
local GRID = V.vvGrid()
local PAGES = { "world", "here", "hoard", "folk" }

-- ---- small helpers ----------------------------------------------------------------------------------------
local function colour(tex, r, g, b, a)
    if tex.SetColorTexture then tex:SetColorTexture(r, g, b, a) else tex:SetTexture(r, g, b, a) end
end
local function font(fs, size) if fs.SetFont then pcall(fs.SetFont, fs, "Fonts\\MORPHEUS.TTF", size) end end
local function count(t) local n = 0; for _ in pairs(t or {}) do n = n + 1 end; return n end
local function pct(permille) return string.format("%d%%", math.floor(permille / 10)) end
local function ago(at)
    local d = K.now() - (at or 0)
    if d < 60 then return K.L("AGO_NOW")
    elseif d < 3600 then return K.f("AGO_MIN", math.floor(d / 60))
    elseif d < 86400 then return K.f("AGO_HOUR", math.floor(d / 3600))
    else return K.f("AGO_DAY", math.floor(d / 86400)) end
end
local function gold(copper)
    copper = copper or 0
    return K.f("GOLD_FMT", math.floor(copper / 10000), math.floor(copper / 100) % 100, copper % 100)
end
local function class_colour(class)
    local c = RAID_CLASS_COLORS and class and class ~= "" and RAID_CLASS_COLORS[class]
    if c then return c.r, c.g, c.b end
    return 0.55, 0.12, 0.08
end
-- Another addon's saved table, found by its title in the package registry (so renamed folders still work), else by
-- its default global name.
local function dbof(title, default)
    for _, a in ipairs(ns.addons or {}) do
        if a.title == title and a.sv ~= "" then return _G[a.sv] end
    end
    return _G[default]
end
local function land_name(map)
    if C_Map and C_Map.GetMapInfo then
        local ok, info = pcall(C_Map.GetMapInfo, map)
        if ok and info and info.name and info.name ~= "" then return info.name end
    end
    local l = K.db and K.db.lands[map]
    if l and l.name then return l.name end
    if GetZoneText and K.pos("player") == map then
        local z = GetZoneText()
        if z and z ~= "" then return z end
    end
    return K.L("LAND_UNKNOWN")
end
local function haiku()
    local text = K.L("H_OPEN_" .. (K.now() % 3 + 1))
    for line in (text .. "/"):gmatch("%s*([^/]-)%s*/") do print(INK .. line .. "|r") end
end

-- ---- the recorder --------------------------------------------------------------------------------------------
local redraw   -- set once the window code is defined
local function tick()
    if not K.db then return end
    local map, x, y = K.pos("player")
    if not map then return end
    local name = land_name(map)
    if name ~= K.L("LAND_UNKNOWN") then V.vvLandName_(K.db, map, name) end
    local m = V.vvStep_(K.db, map, x, y, K.now())
    if m > 0 then
        local word = m == 250 and "QUARTER" or m == 500 and "HALF" or m == 750 and "THREEQ" or "WHOLE"
        K.notice(K.f("CHARTED", K.L("NAME"), K.L(word), name))
    end
    local class = nil
    if UnitClass then local _, c = UnitClass("player"); class = c end
    V.vvHere_(K.db, K.me() or "?", map, x, y, K.now(), class or "", name)
    if redraw then redraw() end
end

-- ---- what each page says (shared by the window and the chat fallback) ----------------------------------------
local show_land
local function who_on(map)
    local t = {}
    for who, w in pairs(K.db.where) do if w.map == map then t[#t + 1] = K.short(who) end end
    table.sort(t)
    return t
end
local function lands_sorted()
    local out = {}
    for map, l in pairs(K.db.lands) do
        out[#out + 1] = { map = map, name = l.name or K.L("LAND_UNKNOWN"), n = l.n or 0, cov = V.vvCoverage(K.db, map) }
    end
    table.sort(out, function(a, b) if a.cov ~= b.cov then return a.cov > b.cov end return a.name < b.name end)
    return out
end
local function world_items()
    local items, fields, walked = {}, 0, {}
    for who, went in pairs(K.db.went) do if next(went) then walked[who] = true end end
    local lands = lands_sorted()
    for _, l in ipairs(lands) do
        fields = fields + l.n
        local who = who_on(l.map)
        local map = l.map
        items[#items + 1] = { text = K.f("LAND_ROW", l.name, pct(l.cov), #who > 0 and K.f("LAND_WHO", table.concat(who, ", ")) or ""),
                              bar = l.cov, click = function() show_land(map) end }
    end
    return K.f("WORLD_HEAD", #lands, count(walked), fields), items
end

local function pins_on(map)
    local pins, me = {}, K.me()
    for who, w in pairs(K.db.where) do
        if w.map == map then
            pins[#pins + 1] = { x = w.x, y = w.y, kind = "folk", who = who, class = w.class,
                                label = who == me and K.L("PIN_ME") or K.short(who), tip = K.f("PIN_SEEN", K.short(who), ago(w.at)) }
        end
    end
    local andv = dbof("Andvari's Map", "AndvarisMapDB")
    for i, m in ipairs(andv and andv.marks or {}) do
        if m.map == map then
            pins[#pins + 1] = { x = m.x, y = m.y, kind = "x", mark = "X", faded = m.found ~= nil,
                                tip = K.f("PIN_X", i, m.found and K.L("PIN_X_FOUND") or "") }
        end
    end
    local rr = dbof("Rune Riddle", "RuneRiddleDB")
    for i, s in ipairs(rr and rr.stops or {}) do
        if s.map == map then pins[#pins + 1] = { x = s.x, y = s.y, kind = "stop", mark = "R" .. i, tip = K.f("PIN_STOP", i) } end
    end
    local sl = dbof("Sleipnir Sprint", "SleipnirSprintDB")
    local c = sl and sl.course
    if c and c.map == map then
        pins[#pins + 1] = { x = c.sx, y = c.sy, kind = "start", mark = "S", tip = K.L("PIN_START") }
        pins[#pins + 1] = { x = c.fx, y = c.fy, kind = "finish", mark = "F", tip = K.L("PIN_FINISH") }
    end
    return pins
end
local function here_lines(map)
    if not map then return K.L("HERE_NONE"), {} end
    local l = K.db.lands[map]
    local n = l and l.n or 0
    local head = K.f("HERE_HEAD", land_name(map), n, GRID * GRID, pct(V.vvCoverage(K.db, map)))
    local items = {}
    if n == 0 then items[#items + 1] = { text = K.L("HERE_FIRST") } end
    for _, p in ipairs(pins_on(map)) do items[#items + 1] = { text = p.tip, class = p.class } end
    return head, items
end

local function hoard_rows(q)
    local f = dbof("Fafnir's Bed", "@FAFNIR_VAR@")
    if not (f and f.chars) then return nil end
    local items, copper = {}, 0
    for who, ch in pairs(f.chars) do
        copper = copper + (ch.gold or 0)
        for where, t in pairs(ch.loc or {}) do
            for id, n in pairs(t) do
                if type(n) == "number" and n > 0 then
                    local it = items[id]
                    if not it then
                        it = { id = id, name = (f.names and f.names[id]) or ("#" .. tostring(id)), total = 0, holders = {} }
                        items[id] = it
                    end
                    it.total = it.total + n
                    it.holders[#it.holders + 1] = { who = K.short(who), where = where, n = n }
                end
            end
        end
    end
    for _, box in pairs(f.post or {}) do
        for _, parcel in pairs(box) do if type(parcel) == "table" then copper = copper + (parcel.copper or 0) end end
    end
    q = (q or ""):lower()
    local list, kinds, total = {}, 0, 0
    for _, it in pairs(items) do
        kinds, total = kinds + 1, total + it.total
        if q == "" or it.name:lower():find(q, 1, true) then list[#list + 1] = it end
    end
    table.sort(list, function(a, b) if a.total ~= b.total then return a.total > b.total end return a.name < b.name end)
    return list, kinds, total, copper
end
local WHERE_KEY = { bags = "IN_BAGS", bank = "IN_BANK", mail = "IN_MAIL" }
local function hoard_items(q)
    local list, kinds, total, copper = hoard_rows(q)
    if not list then return K.L("HOARD_NONE"), {} end
    local items = {}
    for i, it in ipairs(list) do
        if i > 200 then break end
        table.sort(it.holders, function(a, b) return a.n > b.n end)
        local parts = {}
        for j, h in ipairs(it.holders) do
            if j > 4 then parts[#parts + 1] = "..."; break end
            parts[#parts + 1] = K.f("HOARD_HOLDER", h.who, h.n, K.L(WHERE_KEY[h.where] or "IN_BAGS"))
        end
        items[#items + 1] = { text = K.f("HOARD_ROW", it.name, it.total, table.concat(parts, ", ")) }
    end
    if #items == 0 then items[1] = { text = K.L("HOARD_EMPTY") } end
    return K.f("HOARD_HEAD", kinds, total, gold(copper)), items
end

local function folk()
    local names, out = {}, {}
    for c in pairs(K.db.where) do names[c] = true end
    local f, b = dbof("Fafnir's Bed", "@FAFNIR_VAR@"), dbof("Character Board", "CharacterBoardDB")
    if f and f.chars then for c in pairs(f.chars) do names[c] = true end end
    if b and b.chars then for c in pairs(b.chars) do names[c] = true end end
    for c in pairs(names) do
        local w = K.db.where[c]
        local fc, bc = f and f.chars and f.chars[c], b and b.chars and b.chars[c]
        out[#out + 1] = { name = c, class = (w and w.class ~= "" and w.class) or (bc and bc.class) or "",
                          level = bc and bc.level, zone = (w and w.zone) or (bc and bc.zone), map = (w and w.map) or (bc and bc.map),
                          at = (w and w.at) or (bc and bc.t) or (fc and fc.seen) or 0, gold = fc and fc.gold }
    end
    table.sort(out, function(a, b) if a.at ~= b.at then return a.at > b.at end return a.name < b.name end)
    return out
end
local function folk_items()
    local items, lands = {}, {}
    local list = folk()
    for _, p in ipairs(list) do
        if p.map then lands[p.map] = true end
        local who = K.short(p.name) .. (p.level and (" (" .. p.level .. ")") or "")
        local where = p.zone or K.L("FOLK_NOWHERE")
        local map = p.map
        items[#items + 1] = { text = K.f("FOLK_ROW", who, where, p.at > 0 and (", " .. ago(p.at)) or "", gold(p.gold)),
                              click = map and function() show_land(map) end or nil, class = p.class }
    end
    return K.f("FOLK_HEAD", #list, count(lands)), items
end

local function lines_for(page, map)
    local head, items
    if page == "here" then head, items = here_lines(map or K.pos("player"))
    elseif page == "hoard" then head, items = hoard_items("")
    elseif page == "folk" then head, items = folk_items()
    else head, items = world_items() end
    local out = { INK .. K.L("TITLE") .. "|r  " .. head }
    for i, it in ipairs(items) do if i > 30 then break end out[#out + 1] = "  " .. it.text end
    return out
end

-- ---- the window --------------------------------------------------------------------------------------------
local window, pages, tabs, stamp
local toggle

-- A list of rows drawn from a pool; the mouse wheel scrolls it. Rows may carry an ink bar (permille) and a click.
local ROW = 20
local function make_list(parent, top, left, width, rows_shown)
    local L = { rows = {}, offset = 0, items = {} }
    local function row(i)
        local r = L.rows[i]
        if r then return r end
        local b = CreateFrame("Button", nil, parent)
        b:SetSize(width, ROW)
        b:SetPoint("TOPLEFT", parent, "TOPLEFT", left, -(top + (i - 1) * ROW))
        b.bar = b:CreateTexture(nil, "BACKGROUND")
        colour(b.bar, 0.30, 0.42, 0.18, 0.30)
        b.bar:SetPoint("LEFT", b, "LEFT", 0, 0)
        b.bar:SetHeight(ROW - 4)
        b.words = b:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
        b.words:SetPoint("LEFT", b, "LEFT", 4, 0)
        if b.words.SetJustifyH then b.words:SetJustifyH("LEFT") end
        if b.words.SetWidth then b.words:SetWidth(width - 8) end
        b:SetScript("OnClick", function(self) if self.item and self.item.click then self.item.click() end end)
        L.rows[i] = b
        return b
    end
    function L.draw()
        for i = 1, rows_shown do
            local it = L.items[L.offset + i]
            local r = row(i)
            r.item = it
            if it then
                r.words:SetText(it.text)
                if r.words.SetTextColor then
                    if it.class and it.class ~= "" then r.words:SetTextColor(class_colour(it.class))
                    else r.words:SetTextColor(0.22, 0.15, 0.06) end
                end
                if it.bar then r.bar:SetWidth(math.max(1, width * it.bar / 1000)); r.bar:Show() else r.bar:Hide() end
                r:Show()
            else
                r.words:SetText("")
                r:Hide()
            end
        end
    end
    function L.set(items) L.items = items; L.offset = 0; L.draw() end
    if parent.EnableMouseWheel then
        parent:EnableMouseWheel(true)
        parent:SetScript("OnMouseWheel", function(_, delta)
            local max = math.max(0, #L.items - rows_shown)
            L.offset = math.min(max, math.max(0, L.offset - delta * 3))
            L.draw()
        end)
    end
    return L
end

local function button(parent, text, w, h)
    local ok, b = pcall(CreateFrame, "Button", nil, parent, "UIPanelButtonTemplate")
    if not ok then b = CreateFrame("Button", nil, parent) end
    b:SetSize(w, h)
    b:SetText(text)
    return b
end

-- The land's own art, when the client offers it: its tiles under the fog, tinted like old vellum. False without it.
local function draw_art(h, map)
    for _, t in ipairs(h.art) do t:Hide() end
    if not (C_Map and C_Map.GetMapArtLayers and C_Map.GetMapArtLayerTextures) then return false end
    local ok, layers = pcall(C_Map.GetMapArtLayers, map)
    local l = ok and layers and layers[1]
    if not (l and l.layerWidth and l.layerWidth > 0 and l.tileWidth and l.tileWidth > 0 and l.layerHeight and l.layerHeight > 0) then return false end
    local ok2, tiles = pcall(C_Map.GetMapArtLayerTextures, map, 1)
    if not (ok2 and tiles and #tiles > 0) then return false end
    local cols = math.ceil(l.layerWidth / l.tileWidth)
    local sx, sy = MAPW / l.layerWidth, MAPH / l.layerHeight
    for i, file in ipairs(tiles) do
        local t = h.art[i]
        if not t then t = h.board:CreateTexture(nil, "BACKGROUND"); h.art[i] = t end
        local col, row = (i - 1) % cols, math.floor((i - 1) / cols)
        t:SetSize(l.tileWidth * sx, l.tileHeight * sy)
        t:SetPoint("TOPLEFT", h.board, "TOPLEFT", col * l.tileWidth * sx, -row * l.tileHeight * sy)
        t:SetTexture(file)
        if t.SetDesaturated then pcall(t.SetDesaturated, t, true) end
        if t.SetVertexColor then t:SetVertexColor(0.95, 0.82, 0.58, 0.9) end
        t:Show()
    end
    return true
end

local function pin_widget(h, i)
    local b = h.pins[i]
    if b then return b end
    b = CreateFrame("Button", nil, h.board)
    b:SetSize(16, 16)
    b.dot = b:CreateTexture(nil, "OVERLAY")
    b.dot:SetSize(10, 10)
    b.dot:SetPoint("CENTER", b, "CENTER", 0, 0)
    b.mark = b:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    b.mark:SetPoint("CENTER", b, "CENTER", 0, 0)
    font(b.mark, 18)
    b.label = b:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    b.label:SetPoint("LEFT", b, "RIGHT", 2, 0)
    b:SetScript("OnEnter", function(self)
        if GameTooltip and self.tip then GameTooltip:SetOwner(self, "ANCHOR_RIGHT"); GameTooltip:SetText(self.tip); GameTooltip:Show() end
    end)
    b:SetScript("OnLeave", function() if GameTooltip then GameTooltip:Hide() end end)
    h.pins[i] = b
    return b
end

local function draw_here()
    local h = pages.here
    local map = h.map or K.pos("player")
    local head, side = here_lines(map)
    h.head:SetText(head)
    h.list.set(side)
    if not map then
        for _, t in ipairs(h.fog) do t:Hide() end
        for _, t in ipairs(h.art) do t:Hide() end
        for _, b in ipairs(h.pins) do b:Hide() end
        return
    end
    local seen = K.db.seen[map] or {}
    local art = draw_art(h, map)
    local cw, ch = MAPW / GRID, MAPH / GRID
    for cell = 1, GRID * GRID do
        local t = h.fog[cell]
        if not t then
            t = h.board:CreateTexture(nil, "ARTWORK")
            t:SetSize(cw, ch)
            t:SetPoint("TOPLEFT", h.board, "TOPLEFT", (V.vvCellCol(cell) - 1) * cw, -(V.vvCellRow(cell) - 1) * ch)
            h.fog[cell] = t
        end
        if seen[cell] then
            if art then t:Hide() else colour(t, 0.30, 0.42, 0.18, 0.28); t:Show() end
        else
            colour(t, 0.10, 0.07, 0.04, art and 0.72 or 0.45)
            t:Show()
        end
    end
    local pins = pins_on(map)
    for i, p in ipairs(pins) do
        local b = pin_widget(h, i)
        b:SetPoint("CENTER", h.board, "TOPLEFT", (p.x or 0) / 1000 * MAPW, -(p.y or 0) / 1000 * MAPH)
        b.tip, b.pin = p.tip, p.kind
        if p.kind == "folk" then
            local r, g, bl = class_colour(p.class)
            colour(b.dot, r, g, bl, 1)
            b.dot:Show(); b.mark:Hide()
            b.label:SetText(p.label); b.label:Show()
        else
            b.dot:Hide(); b.label:Hide()
            b.mark:SetText(p.mark)
            if b.mark.SetTextColor then
                if p.kind == "x" then b.mark:SetTextColor(0.62, 0.06, 0.06) else b.mark:SetTextColor(0.22, 0.15, 0.06) end
            end
            if b.mark.SetAlpha then b.mark:SetAlpha(p.faded and 0.45 or 1) end
            b.mark:Show()
        end
        b:Show()
    end
    for i = #pins + 1, #h.pins do h.pins[i]:Hide() end
end

local function select_page(name)
    for _, n in ipairs(PAGES) do
        if pages[n] then if n == name then pages[n]:Show() else pages[n]:Hide() end end
        local t = tabs and tabs[n]
        if t then t.label:SetText((n == name and "|cff5a3a10" or "|cff8c6b27") .. K.L("TAB_" .. n:upper()) .. "|r") end
    end
    K.db.page = name
    redraw()
end
show_land = function(map)
    if not pages then return end
    pages.here.map = map
    select_page("here")
end

redraw = function()
    if not (window and window:IsShown() and pages) then return end
    local page = K.db.page or "world"
    if page == "world" then
        local head, items = world_items()
        pages.world.head:SetText(head); pages.world.list.set(items)
    elseif page == "hoard" then
        local head, items = hoard_items(pages.hoard.box:GetText())
        pages.hoard.head:SetText(head); pages.hoard.list.set(items)
    elseif page == "folk" then
        local head, items = folk_items()
        pages.folk.head:SetText(head); pages.folk.list.set(items)
    else draw_here() end
end

local function make_doors(f)
    local list = {}
    for _, a in ipairs(ns.addons or {}) do if a.folder ~= ADDON then list[#list + 1] = a end end
    table.sort(list, function(x, y) return x.title < y.title end)
    local label = f:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    label:SetPoint("BOTTOMLEFT", f, "BOTTOMLEFT", 14, 74)
    label:SetText(INK .. K.L("DOORS") .. "|r")
    local per, bw, bh = 9, 84, 20
    for i, a in ipairs(list) do
        local b = button(f, a.title, bw, bh)
        local col, row = (i - 1) % per, math.floor((i - 1) / per)
        b:SetPoint("BOTTOMLEFT", f, "BOTTOMLEFT", 14 + col * (bw + 4), 50 - row * (bh + 2))
        b.door = a
        b:SetScript("OnClick", function()
            local fn = a.key ~= "" and SlashCmdList and SlashCmdList[a.key]
            if fn then fn("") else print(INK .. a.title .. "|r: " .. K.L("NOCMD")) end
        end)
        b:SetScript("OnEnter", function(self)
            if not GameTooltip then return end
            GameTooltip:SetOwner(self, "ANCHOR_TOP")
            GameTooltip:SetText(K.f("DOOR_TIP", a.title, a.notes or "", #a.cmds > 0 and table.concat(a.cmds, "  ") or K.L("NOCMD")))
            GameTooltip:Show()
        end)
        b:SetScript("OnLeave", function() if GameTooltip then GameTooltip:Hide() end end)
    end
    return #list
end

local function make_pages(f)
    pages = {}
    for _, name in ipairs(PAGES) do
        local p = CreateFrame("Frame", nil, f)
        p:SetPoint("TOPLEFT", f, "TOPLEFT", 0, -66)
        p:SetPoint("BOTTOMRIGHT", f, "BOTTOMRIGHT", 0, 96)
        p.head = p:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        p.head:SetPoint("TOPLEFT", p, "TOPLEFT", 14, -6)
        if p.head.SetWidth then p.head:SetWidth(780) end
        if p.head.SetJustifyH then p.head:SetJustifyH("LEFT") end
        if p.head.SetTextColor then p.head:SetTextColor(0.35, 0.22, 0.08) end
        p:Hide()
        pages[name] = p
    end
    pages.world.list = make_list(pages.world, 30, 14, 780, 19)
    pages.folk.list = make_list(pages.folk, 30, 14, 780, 19)
    local ok, box = pcall(CreateFrame, "EditBox", ADDON .. "HoardSearch", pages.hoard, "InputBoxTemplate")
    if not ok then box = CreateFrame("EditBox", ADDON .. "HoardSearch", pages.hoard) end
    box:SetSize(300, 20)
    box:SetPoint("TOPLEFT", pages.hoard, "TOPLEFT", 22, -28)
    if box.SetAutoFocus then box:SetAutoFocus(false) end
    box:SetScript("OnTextChanged", function() redraw() end)
    box:SetScript("OnEscapePressed", function(self) if self.ClearFocus then self:ClearFocus() end end)
    pages.hoard.box = box
    local hint = pages.hoard:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    hint:SetPoint("LEFT", box, "RIGHT", 8, 0)
    hint:SetText(K.L("HOARD_SEARCH"))
    pages.hoard.list = make_list(pages.hoard, 54, 14, 780, 18)
    local h = pages.here
    h.board = CreateFrame("Frame", nil, h)
    h.board:SetSize(MAPW, MAPH)
    h.board:SetPoint("TOPLEFT", h, "TOPLEFT", 14, -30)
    local edge = h.board:CreateTexture(nil, "BORDER")
    edge:SetPoint("TOPLEFT", h.board, "TOPLEFT", -2, 2)
    edge:SetPoint("BOTTOMRIGHT", h.board, "BOTTOMRIGHT", 2, -2)
    colour(edge, 0.25, 0.17, 0.08, 0.9)
    local sheet = h.board:CreateTexture(nil, "BACKGROUND")
    sheet:SetAllPoints(h.board)
    colour(sheet, 0.86, 0.76, 0.56, 1)
    h.compass = h.board:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    h.compass:SetPoint("TOP", h.board, "TOP", 0, -4)
    h.compass:SetText("N")
    font(h.compass, 16)
    h.art, h.fog, h.pins = {}, {}, {}
    h.list = make_list(h, 30, 630, 176, 19)
end

local function make_window()
    if window or not (CreateFrame and UIParent) then return window end
    local f = CreateFrame("Frame", "VegvisirFrame", UIParent)
    f:SetSize(820, 600)
    f:SetPoint("CENTER", UIParent, "CENTER", 0, 20)
    if f.SetFrameStrata then f:SetFrameStrata("HIGH") end
    local base = f:CreateTexture(nil, "BACKGROUND")
    base:SetAllPoints(f)
    colour(base, 0.80, 0.70, 0.50, 0.97)
    local sheet = f:CreateTexture(nil, "BORDER")
    sheet:SetAllPoints(f)
    sheet:SetTexture(PATH)
    f:EnableMouse(true)
    f:SetMovable(true)
    f:RegisterForDrag("LeftButton")
    f:SetScript("OnDragStart", function(self) self:StartMoving() end)
    f:SetScript("OnDragStop", function(self) self:StopMovingOrSizing() end)
    local title = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    title:SetPoint("TOPLEFT", f, "TOPLEFT", 18, -12)
    title:SetText(K.L("TITLE"))
    font(title, 22)
    if title.SetTextColor then title:SetTextColor(0.35, 0.22, 0.08) end
    local ok, close = pcall(CreateFrame, "Button", nil, f, "UIPanelCloseButton")
    if not ok then
        close = CreateFrame("Button", nil, f)
        close:SetSize(60, 20)
        close:SetText("x")
    end
    close:SetPoint("TOPRIGHT", f, "TOPRIGHT", -4, -4)
    close:SetScript("OnClick", function() f:Hide() end)
    tabs = {}
    for i, name in ipairs(PAGES) do
        local t = CreateFrame("Button", nil, f)
        t:SetSize(118, 22)
        t:SetPoint("TOPLEFT", f, "TOPLEFT", 14 + (i - 1) * 124, -40)
        t.label = t:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        t.label:SetPoint("LEFT", t, "LEFT", 4, 0)
        font(t.label, 15)
        local line = t:CreateTexture(nil, "ARTWORK")
        line:SetPoint("BOTTOMLEFT", t, "BOTTOMLEFT", 0, 0)
        line:SetPoint("BOTTOMRIGHT", t, "BOTTOMRIGHT", 0, 0)
        line:SetHeight(1)
        colour(line, 0.35, 0.22, 0.08, 0.6)
        t:SetScript("OnClick", function() select_page(name) end)
        tabs[name] = t
    end
    make_pages(f)
    f.doors = make_doors(f)
    f:SetScript("OnShow", function() redraw() end)
    if UISpecialFrames then table.insert(UISpecialFrames, "VegvisirFrame") end
    f:Hide()
    window = f
    return f
end

local function show(page)
    local f = make_window()
    if page then K.db.page = page elseif not K.db.page then K.db.page = "world" end
    if page ~= "here" and pages then pages.here.map = nil end
    if not f then
        for _, line in ipairs(lines_for(K.db.page)) do print(line) end
        return
    end
    local was = f:IsShown()
    f:Show()
    select_page(K.db.page)
    if not was then haiku() end
end
toggle = function()
    local f = make_window()
    if f and f:IsShown() then f:Hide() else show() end
end

-- ---- the stamp on the minimap ----------------------------------------------------------------------------------
local function make_stamp()
    if stamp or not (Minimap and CreateFrame) then return end
    local b = CreateFrame("Button", "VegvisirMinimapStamp", Minimap)
    b:SetSize(31, 31)
    if b.SetFrameStrata then b:SetFrameStrata("MEDIUM") end
    if b.SetFrameLevel then b:SetFrameLevel(8) end
    local icon = b:CreateTexture(nil, "BACKGROUND")
    icon:SetSize(20, 20)
    icon:SetPoint("CENTER", b, "CENTER", 0, 1)
    icon:SetTexture("Interface\\Icons\\INV_Misc_Map_01")
    if icon.SetTexCoord then icon:SetTexCoord(0.08, 0.92, 0.08, 0.92) end
    local ring = b:CreateTexture(nil, "OVERLAY")
    ring:SetSize(53, 53)
    ring:SetPoint("TOPLEFT", b, "TOPLEFT", 0, 0)
    ring:SetTexture("Interface\\Minimap\\MiniMap-TrackingBorder")
    local function place()
        local a = math.rad(K.db.ring or 200)
        b:SetPoint("CENTER", Minimap, "CENTER", math.cos(a) * 80, math.sin(a) * 80)
    end
    place()
    if b.RegisterForClicks then b:RegisterForClicks("AnyUp") end
    if b.RegisterForDrag then b:RegisterForDrag("LeftButton") end
    b:SetScript("OnClick", function() toggle() end)
    b:SetScript("OnDragStart", function(self) self.dragging = true end)
    b:SetScript("OnDragStop", function(self) self.dragging = false end)
    b:SetScript("OnUpdate", function(self)
        if not (self.dragging and GetCursorPosition and Minimap.GetCenter and Minimap.GetEffectiveScale) then return end
        local mx, my = Minimap:GetCenter()
        local cx, cy = GetCursorPosition()
        local s = Minimap:GetEffectiveScale()
        if not (mx and my and cx and cy and s and s > 0) then return end
        K.db.ring = math.deg(math.atan2(cy / s - my, cx / s - mx))
        place()
    end)
    b:SetScript("OnEnter", function(self)
        if GameTooltip then GameTooltip:SetOwner(self, "ANCHOR_LEFT"); GameTooltip:SetText(K.L("MINIMAP_TIP")); GameTooltip:Show() end
    end)
    b:SetScript("OnLeave", function() if GameTooltip then GameTooltip:Hide() end end)
    stamp = b
end

-- ---- wiring -------------------------------------------------------------------------------------------------
K.on("PLAYER_LOGIN", function()
    tick()
    K.every(3, tick)
    make_stamp()
end)
for _, e in ipairs({ "ZONE_CHANGED_NEW_AREA", "ZONE_CHANGED", "ZONE_CHANGED_INDOORS", "PLAYER_LOGOUT" }) do
    K.on(e, function() pcall(tick) end)
end

SLASH_VEGVISIR1 = "/oldmap"
SLASH_VEGVISIR2 = "/vegvisir"
SlashCmdList["VEGVISIR"] = function(msg)
    msg = (msg or ""):match("^%s*(.-)%s*$"):lower()
    if msg == "world" or msg == "here" or msg == "hoard" or msg == "folk" then show(msg)
    elseif msg == "" then toggle()
    else print(K.L("HELP")) end
end
