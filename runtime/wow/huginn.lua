-- runtime/wow/huginn.lua -- Huginn, quest arrows for orienteering: host glue. The decisions are PARENA
-- (stdlib/collection/huginn.prn: bearing, turn, distance, arrival, colour, the direction words); this file reads the
-- client and draws.
--
-- One small panel: a row per ACTIVE arrow (a colored arrow, the name, how far, which way), and under it a mini
-- quest log -- one line per quest with an on/off toggle for its arrow. Any number of arrows can be on at once (up
-- to 8), quests and waypoints mixed. With none on, the quest you track gets the arrow.
--
-- /huginn               show or hide the panel           /huginn log      show or hide the mini quest log
-- /huginn <n>           turn quest n's arrow on or off   /huginn list     the quest log in chat, numbered
-- /huginn way <x> <y> [note]   add a waypoint arrow on the map you are on (0-100); it clears when you arrive
-- /huginn clear         every arrow off
--
-- Optimized for a small cost per frame: quest locations are looked up only when the quest log or the zone changes
-- (and every 2 s as a backstop), never per frame; arrows are recomputed 10 times a second, only for active rows;
-- rows are made once and reused.
--
-- Where a quest's location comes from, best first (feature-tested; none probed on Forever yet -- a client with none
-- of them still has waypoints):
--   C_QuestLog.GetNextWaypoint(questID)        retail: the client's own next step, on any map
--   C_QuestLog.GetQuestsOnMap(mapID)           retail: the quest's marker on the map you are on
--   coordinates written in an objective's text ("(45.2, 60.1)"), via C_QuestLog.GetQuestObjectives or
--   GetQuestLogLeaderBoard -- taken as being on the map you are on
-- The quest log: C_QuestLog.GetNumQuestLogEntries + C_QuestLog.GetInfo (retail) or GetNumQuestLogEntries +
-- GetQuestLogTitle (classic). Tracked: C_SuperTrack.GetSuperTrackedQuestID. Watched: C_QuestLog.GetQuestWatchType
-- or IsQuestWatched. You: C_Map.GetBestMapForUnit + C_Map.GetPlayerMapPosition (confirmed on Forever),
-- GetPlayerFacing (radians counterclockwise from north; nil in instances on retail). Yards and targets on other
-- maps need C_Map.GetWorldPosFromMapPos + CreateVector2D; without them a target must be on your map and distances
-- are in map units. The arrow turns with Texture:SetRotation; without it the row shows only the words.

local ADDON = "@ADDON@"
local VAR = "@VAR@"
local _, ns = ...

local G = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/huginn"]

local function L(key)
    local c = ns and ns.catalogue
    if not c then return key end
    local t = c[GetLocale and GetLocale() or "enUS"]
    return (t and t[key]) or (c.enUS and c.enUS[key]) or key
end

if not G then
    print("|cffff5555" .. string.format(L("MISSING"), ADDON) .. "|r")
    return
end

local MAX_ARROWS = 8
local db = nil
-- active: ordered list of { kind = "quest", id } or { kind = "way", map, x, y, note }
local state = { active = {}, arrived = {}, log = {}, places = {}, stale = true, since = 0 }

-- ---------------------------------------------------------------------------------------------------------------
-- the client: you, maps, quests

local function you()
    if not (C_Map and C_Map.GetBestMapForUnit and C_Map.GetPlayerMapPosition) then return nil end
    local map = C_Map.GetBestMapForUnit("player")
    if not map then return nil end
    local p = C_Map.GetPlayerMapPosition(map, "player")
    if not (p and p.GetXY) then return nil end
    local x, y = p:GetXY()
    if not x or (x == 0 and y == 0) then return nil end
    return map, x, y
end

local function facing()
    if not GetPlayerFacing then return nil end
    local f = GetPlayerFacing()
    if not f then return nil end
    return math.floor(math.deg(f) + 0.5) % 360
end

local function world(map, x, y)
    if not (C_Map and C_Map.GetWorldPosFromMapPos and CreateVector2D) then return nil end
    local continent, pos = C_Map.GetWorldPosFromMapPos(map, CreateVector2D(x, y))
    if not (continent and pos and pos.GetXY) then return nil end
    local wx, wy = pos:GetXY()
    return continent, wx, wy
end

-- a map's frame in world space (corner + the two edge vectors), cached per map; false = the client cannot say
local frames = {}
local function frame_of(map)
    local f = frames[map]
    if f ~= nil then return f or nil end
    local c, ax, ay = world(map, 0, 0)
    local _, bx, by = world(map, 1, 0)
    local _, cx, cy = world(map, 0, 1)
    if not (c and bx and cx) then frames[map] = false; return nil end
    f = { continent = c, ox = ax, oy = ay, ex = bx - ax, ey = by - ay, sx = cx - ax, sy = cy - ay }
    f.width = math.sqrt(f.ex * f.ex + f.ey * f.ey)
    f.height = math.sqrt(f.sx * f.sx + f.sy * f.sy)
    if f.width == 0 or f.height == 0 then frames[map] = false; return nil end
    frames[map] = f
    return f
end

-- a target (map, x, y in 0..1) placed on the player's map (0..1, possibly outside); nil + a reason if it cannot be
local function onto(pmap, tmap, tx, ty)
    if tmap == pmap then return tx, ty end
    local f = frame_of(pmap)
    local c, wx, wy = world(tmap, tx, ty)
    if not (f and c) then return nil, "OTHER_MAP" end
    if c ~= f.continent then return nil, "OTHER_CONTINENT" end
    local dx, dy = wx - f.ox, wy - f.oy
    return (dx * f.ex + dy * f.ey) / (f.width * f.width), (dx * f.sx + dy * f.sy) / (f.height * f.height)
end

local function read_log()
    local list = {}
    if C_QuestLog and C_QuestLog.GetNumQuestLogEntries and C_QuestLog.GetInfo then
        for i = 1, C_QuestLog.GetNumQuestLogEntries() or 0 do
            local info = C_QuestLog.GetInfo(i)
            if info and not info.isHeader and not info.isHidden and info.questID then
                local watched = C_QuestLog.GetQuestWatchType and C_QuestLog.GetQuestWatchType(info.questID) ~= nil
                list[#list + 1] = { id = info.questID, title = info.title or ("#" .. info.questID), index = i, watched = watched }
            end
        end
    elseif GetNumQuestLogEntries and GetQuestLogTitle then
        for i = 1, GetNumQuestLogEntries() or 0 do
            local title, _, _, isHeader, _, _, _, questID = GetQuestLogTitle(i)
            if title and not isHeader then
                list[#list + 1] = { id = questID or -i, title = title, index = i,
                                    watched = IsQuestWatched and IsQuestWatched(i) and true or false }
            end
        end
    end
    return list
end

local function tracked_id()
    if C_SuperTrack and C_SuperTrack.GetSuperTrackedQuestID then
        local id = C_SuperTrack.GetSuperTrackedQuestID()
        if id and id > 0 then return id end
    end
    return nil
end

local function coords_in(text)
    if type(text) ~= "string" then return nil end
    local x, y = text:match("(%d+%.?%d*)%s*,%s*(%d+%.?%d*)")
    x, y = tonumber(x), tonumber(y)
    if x and y and x <= 100 and y <= 100 then return x / 100, y / 100 end
    return nil
end

local function quest_place(q, pmap)
    if C_QuestLog and C_QuestLog.GetNextWaypoint and q.id > 0 then
        local map, x, y = C_QuestLog.GetNextWaypoint(q.id)
        if map and x and y then return { map = map, x = x, y = y } end
    end
    if pmap and C_QuestLog and C_QuestLog.GetQuestsOnMap then
        for _, m in ipairs(C_QuestLog.GetQuestsOnMap(pmap) or {}) do
            if m.questID == q.id and m.x and m.y then return { map = pmap, x = m.x, y = m.y } end
        end
    end
    if pmap then
        if C_QuestLog and C_QuestLog.GetQuestObjectives and q.id > 0 then
            for _, o in ipairs(C_QuestLog.GetQuestObjectives(q.id) or {}) do
                local x, y = coords_in(o.text)
                if x then return { map = pmap, x = x, y = y } end
            end
        elseif GetNumQuestLeaderBoards and GetQuestLogLeaderBoard and q.index then
            for j = 1, GetNumQuestLeaderBoards(q.index) or 0 do
                local x, y = coords_in((GetQuestLogLeaderBoard(j, q.index)))
                if x then return { map = pmap, x = x, y = y } end
            end
        end
    end
    return false
end

-- the slow part, done only when something changed: the log and every quest's location
local function refresh_quests()
    local pmap = you()
    state.log = read_log()
    state.places = {}
    for _, q in ipairs(state.log) do state.places[q.id] = quest_place(q, pmap) end
    state.stale, state.since = false, 0
end

local function quest_by_id(id) for _, q in ipairs(state.log) do if q.id == id then return q end end end

-- the arrows to draw now: { label, quest, map, x, y, way, key }
local function targets()
    local out = {}
    for _, a in ipairs(state.active) do
        if a.kind == "way" then
            out[#out + 1] = { label = a.note ~= "" and a.note or L("WAYPOINT"), quest = 0, map = a.map, x = a.x, y = a.y,
                              way = a, key = "way:" .. a.map .. ":" .. a.x .. ":" .. a.y }
        else
            local q, p = quest_by_id(a.id), state.places[a.id]
            if q then out[#out + 1] = { label = q.title, quest = q.id, map = p and p.map, x = p and p.x, y = p and p.y,
                                        key = "quest:" .. q.id } end
        end
    end
    if #state.active == 0 then   -- nothing on: the tracked quest, else the first watched one with a location
        local t = tracked_id()
        local pick = t and quest_by_id(t)
        if not (pick and state.places[pick.id]) then
            pick = nil
            for _, q in ipairs(state.log) do if q.watched and state.places[q.id] then pick = q; break end end
        end
        if pick then
            local p = state.places[pick.id]
            out[1] = { label = pick.title, quest = pick.id, map = p.map, x = p.x, y = p.y, key = "quest:" .. pick.id, auto = true }
        end
    end
    return out
end

-- ---------------------------------------------------------------------------------------------------------------
-- active arrows

local function save()
    if not db then return end
    db.active = {}
    for i, a in ipairs(state.active) do
        db.active[i] = a.kind == "way" and { kind = "way", map = a.map, x = a.x, y = a.y, note = a.note } or { kind = "quest", id = a.id }
    end
end

local function is_on(id) for i, a in ipairs(state.active) do if a.kind == "quest" and a.id == id then return i end end end

local function toggle_quest(id)
    local i = is_on(id)
    if i then table.remove(state.active, i); save(); return false end
    if #state.active >= MAX_ARROWS then return nil end
    state.active[#state.active + 1] = { kind = "quest", id = id }
    save()
    return true
end

local function add_way(map, x, y, note)
    if #state.active >= MAX_ARROWS then return false end
    state.active[#state.active + 1] = { kind = "way", map = map, x = x, y = y, note = note or "" }
    save()
    return true
end

local function remove_way(w)
    for i, a in ipairs(state.active) do if a == w then table.remove(state.active, i); save(); return end end
end

-- ---------------------------------------------------------------------------------------------------------------
-- the panel

local COLOURS = { { 0.3, 0.95, 0.35 }, { 1, 0.85, 0.2 }, { 1, 0.3, 0.25 } }
local ROW, LOGROW, WIDTH = 30, 16, 260
local ui = nil

-- one arrow row: compute and draw it. Returns nothing; all text set in place.
local function draw_row(r, t, pmap, px, py, face)
    r.title:SetText(t.label)
    local function words(s) r.arrow:Hide(); r.info:SetText(s) end
    if not pmap then return words(string.format(L("NO_POS"), ADDON)) end
    if not t.map then return words(L("NONE")) end
    local tx, ty = onto(pmap, t.map, t.x, t.y)
    if not tx then return words(L(ty)) end
    local f = frame_of(pmap)
    local yards = f and 1 or 0
    local w = f and math.floor(f.width + 0.5) or G.hgStandInWidth()
    local h = f and math.floor(f.height + 0.5) or G.hgStandInHeight()
    local dx = G.hgDelta(math.floor(px * 10000 + 0.5), math.floor(tx * 10000 + 0.5), w)
    local dy = G.hgDelta(math.floor(py * 10000 + 0.5), math.floor(ty * 10000 + 0.5), h)
    local dist = G.hgDist(dx, dy)
    if G.hgArrived(dist, yards) == 1 then
        r.arrow:Hide()
        r.info:SetText("|cff4cf259" .. L("ARRIVED") .. "|r")
        if not state.arrived[t.key] then
            state.arrived[t.key] = true
            if db then G.hgArrived_(db, t.quest or 0, pmap, dist) end
            print(string.format(L("THERE"), ADDON, t.label))
            if t.way then remove_way(t.way) end
        end
        return
    end
    state.arrived[t.key] = nil
    local away = string.format(yards == 1 and L("YARDS") or L("UNITS"), dist)
    local bearing = G.hgBearing(dx, dy)
    if face and r.rotates then
        local turn = G.hgTurn(bearing, face)
        local c = COLOURS[G.hgHeat(turn)]
        r.arrow:Show()
        r.arrow:SetRotation(math.rad(turn))
        if r.arrow.SetVertexColor then r.arrow:SetVertexColor(c[1], c[2], c[3]) end
        r.info:SetText(away .. "  ·  " .. L("SIDE_" .. G.hgSide(turn)))
    elseif face then
        words(away .. "  ·  " .. L("SIDE_" .. G.hgSide(G.hgTurn(bearing, face))))
    else
        words(away .. "  ·  " .. L("COMPASS_" .. G.hgSide(bearing)))
    end
end

local function text(parent, template, w)
    local fs = parent:CreateFontString(nil, "OVERLAY", template)
    if fs.SetWidth then fs:SetWidth(w) end
    if fs.SetJustifyH then fs:SetJustifyH("LEFT") end
    return fs
end

local function make_ui()
    if ui or not (CreateFrame and UIParent) then return ui end
    local f = CreateFrame("Frame", "HuginnFrame", UIParent)
    f:SetSize(WIDTH, 40)
    f:SetPoint("TOPRIGHT", UIParent, "TOPRIGHT", -220, -200)
    f:EnableMouse(true)
    f:SetMovable(true)
    if f.SetUserPlaced then f:SetUserPlaced(true) end
    f:RegisterForDrag("LeftButton")
    f:SetScript("OnDragStart", function(self) self:StartMoving() end)
    f:SetScript("OnDragStop", function(self) self:StopMovingOrSizing() end)
    local bg = f:CreateTexture(nil, "BACKGROUND")
    bg:SetAllPoints(f)
    if bg.SetColorTexture then bg:SetColorTexture(0, 0, 0, 0.55) else bg:SetTexture(0, 0, 0, 0.55) end
    local head = CreateFrame("Button", nil, f)
    head:SetSize(WIDTH, 16)
    head:SetPoint("TOPLEFT", f, "TOPLEFT", 0, 0)
    local headText = text(head, "GameFontNormalSmall", WIDTH - 8)
    headText:SetPoint("TOPLEFT", head, "TOPLEFT", 6, -2)
    head:SetScript("OnClick", function() ns.toggle_log() end)
    ui = { frame = f, head = head, headText = headText, rows = {}, logRows = {}, logShown = true, elapsed = 0 }
    f:SetScript("OnUpdate", function(_, dt)
        ui.elapsed = ui.elapsed + (dt or 0)
        state.since = state.since + (dt or 0)
        if ui.elapsed < 0.1 then return end
        ui.elapsed = 0
        if state.stale or state.since >= 2 then refresh_quests(); ns.layout() end
        ns.update()
    end)
    return ui
end

local function arrow_row(i)
    local r = ui.rows[i]
    if r then return r end
    r = {}
    r.arrow = ui.frame:CreateTexture(nil, "ARTWORK")
    r.arrow:SetSize(24, 24)
    r.arrow:SetTexture("Interface\\Minimap\\MinimapArrow")
    r.rotates = r.arrow.SetRotation ~= nil
    r.title = text(ui.frame, "GameFontHighlightSmall", WIDTH - 40)
    r.info = text(ui.frame, "GameFontDisableSmall", WIDTH - 40)
    ui.rows[i] = r
    return r
end

local function log_row(i)
    local r = ui.logRows[i]
    if r then return r end
    r = { button = CreateFrame("Button", nil, ui.frame) }
    r.button:SetSize(WIDTH - 8, LOGROW)
    r.label = text(r.button, "GameFontHighlightSmall", WIDTH - 12)
    r.label:SetPoint("LEFT", r.button, "LEFT", 2, 0)
    r.button:SetScript("OnClick", function()
        if r.id then
            if toggle_quest(r.id) == nil then print(string.format(L("FULL"), ADDON, MAX_ARROWS)) end
            ns.layout(); ns.update()
        end
    end)
    ui.logRows[i] = r
    return r
end

-- place the rows: arrows on top, the mini log under them; hide what is not used
function ns.layout()
    if not ui then return end
    local list = targets()
    ui.current = list
    ui.headText:SetText(L("TITLE") .. "  " .. string.format(L("COUNT"), #state.active, MAX_ARROWS)
        .. "  |cff9d9d9d" .. (ui.logShown and L("HIDE_LOG") or L("SHOW_LOG")) .. "|r")
    local y = 18
    for i, _ in ipairs(list) do
        local r = arrow_row(i)
        r.arrow:ClearAllPoints(); r.arrow:SetPoint("TOPLEFT", ui.frame, "TOPLEFT", 6, -y - 2)
        r.title:ClearAllPoints(); r.title:SetPoint("TOPLEFT", ui.frame, "TOPLEFT", 36, -y)
        r.info:ClearAllPoints(); r.info:SetPoint("TOPLEFT", ui.frame, "TOPLEFT", 36, -y - 13)
        r.title:Show(); r.info:Show()
        y = y + ROW
    end
    for i = #list + 1, #ui.rows do local r = ui.rows[i]; r.arrow:Hide(); r.title:Hide(); r.info:Hide() end
    if #list == 0 then
        local r = arrow_row(1)
        r.arrow:Hide()
        r.title:ClearAllPoints(); r.title:SetPoint("TOPLEFT", ui.frame, "TOPLEFT", 36, -y)
        r.info:ClearAllPoints(); r.info:SetPoint("TOPLEFT", ui.frame, "TOPLEFT", 36, -y - 13)
        r.title:SetText(L("NO_TARGET")); r.info:SetText(L("NO_TARGET_HINT"))
        r.title:Show(); r.info:Show()
        y = y + ROW
    end
    local n = 0
    if ui.logShown then
        y = y + 2
        for i, q in ipairs(state.log) do
            n = i
            local r = log_row(i)
            r.id = q.id
            local on = is_on(q.id) ~= nil
            local has = state.places[q.id] and true or false
            r.label:SetText((on and "|cff4cf259[x]|r " or "[ ] ") .. (has and "" or "|cff7f7f7f") .. q.title
                .. (has and "" or "|r") .. (q.watched and " |cff9d9d9d+|r" or ""))
            r.button:ClearAllPoints(); r.button:SetPoint("TOPLEFT", ui.frame, "TOPLEFT", 4, -y)
            r.button:Show()
            y = y + LOGROW
        end
    end
    for i = n + 1, #ui.logRows do ui.logRows[i].button:Hide() end
    ui.frame:SetHeight(y + 4)
end

-- the fast part: every active arrow, 10 times a second
function ns.update()
    if not ui or not ui.current then return end
    local pmap, px, py = you()
    local face = facing()
    local before = #state.active
    for i, t in ipairs(ui.current) do draw_row(ui.rows[i], t, pmap, px, py, face) end
    if #state.active ~= before then ns.layout() end   -- a waypoint was reached and removed
end

function ns.toggle_log()
    if not ui then return end
    ui.logShown = not ui.logShown
    if db then db.logShown = ui.logShown end
    ns.layout()
end

-- ---------------------------------------------------------------------------------------------------------------
-- commands

local function list()
    refresh_quests()
    if #state.log == 0 then print(string.format(L("LIST_EMPTY"), ADDON)); return end
    print(string.format(L("LIST_HEAD"), ADDON))
    local t = tracked_id()
    for i, q in ipairs(state.log) do
        local mark = (is_on(q.id) and " [x]" or "") .. (q.id == t and " *" or "") .. (q.watched and " +" or "")
        print(string.format(L("LIST_ROW"), i, q.title, mark, state.places[q.id] and L("HAS") or L("NONE")))
    end
    print(L("LIST_HINT"))
end

local function shown()
    local u = make_ui()
    if u then u.frame:Show(); refresh_quests(); ns.layout(); ns.update() end
end

SLASH_HUGINN1 = "/huginn"
SLASH_HUGINN2 = "/hg"
SlashCmdList["HUGINN"] = function(msg)
    msg = (msg or ""):match("^%s*(.-)%s*$")
    local word, rest = msg:match("^(%S*)%s*(.-)$")
    word = (word or ""):lower()
    if word == "" then
        local u = make_ui()
        if not u then list(); return end
        if u.frame:IsShown() then u.frame:Hide() else shown() end
    elseif word == "list" then
        list()
    elseif word == "log" then
        shown(); ns.toggle_log()
    elseif word == "clear" or word == "off" then
        state.active, state.arrived = {}, {}
        save()
        print(string.format(L("CLEARED"), ADDON))
        if ui then ns.layout() end
    elseif word == "way" then
        local x, y, note = rest:match("^(%d+%.?%d*)[%s,]+(%d+%.?%d*)%s*(.-)$")
        x, y = tonumber(x), tonumber(y)
        if not (x and y and x <= 100 and y <= 100) then print(string.format(L("WAY_BAD"), ADDON)); return end
        local pmap = you()
        if not pmap then print(string.format(L("NO_POS"), ADDON)); return end
        if not add_way(pmap, x / 100, y / 100, note) then print(string.format(L("FULL"), ADDON, MAX_ARROWS)); return end
        print(string.format(L("WAY"), ADDON, x, y, (note and note ~= "") and (" (" .. note .. ")") or ""))
        shown()
    elseif tonumber(word) then
        refresh_quests()
        local q = state.log[tonumber(word)]
        if not q then list(); return end
        local on = toggle_quest(q.id)
        if on == nil then print(string.format(L("FULL"), ADDON, MAX_ARROWS)); return end
        print(string.format(on and L("ON") or L("OFF"), ADDON, q.title))
        shown()
    else
        print(L("HELP"))
    end
end

local ev = CreateFrame and CreateFrame("Frame")
if ev then
    for _, e in ipairs({ "ADDON_LOADED", "QUEST_LOG_UPDATE", "ZONE_CHANGED_NEW_AREA", "SUPER_TRACKING_CHANGED",
                         "QUEST_WATCH_LIST_CHANGED" }) do ev:RegisterEvent(e) end
    ev:SetScript("OnEvent", function(_, event, name)
        if event ~= "ADDON_LOADED" then state.stale = true; return end
        if name ~= ADDON then return end
        _G[VAR] = G.hgInit(type(_G[VAR]) == "table" and _G[VAR] or {})
        db = _G[VAR]
        state.active = {}
        for _, a in ipairs(type(db.active) == "table" and db.active or {}) do
            if a.kind == "way" and a.map and a.x and a.y then
                state.active[#state.active + 1] = { kind = "way", map = a.map, x = a.x, y = a.y, note = a.note or "" }
            elseif a.kind == "quest" and a.id then
                state.active[#state.active + 1] = { kind = "quest", id = a.id }
            end
        end
        local u = make_ui()
        if u then
            u.logShown = db.logShown ~= false
            u.frame:Show()
        end
    end)
end

-- for tests
ns.state, ns.refresh_quests, ns.targets, ns.onto = state, refresh_quests, targets, onto
ns.ui_for_tests = function() return ui end
