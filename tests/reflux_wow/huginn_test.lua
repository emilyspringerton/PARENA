-- tests/reflux_wow/huginn_test.lua -- Huginn (quest arrows) in the packaged client, under real Lua 5.1, against a stubbed
-- client: a quest log, quest locations, maps with a world frame, the player's position and facing, and widgets.
-- usage: lua5.1 huginn_test.lua <client dir>
assert(_VERSION == "Lua 5.1", "must run under Lua 5.1")
local client = assert(arg[1])

local passed, failed = 0, 0
local function eq(got, want, what)
    if got == want then passed = passed + 1
    else failed = failed + 1; print("FAIL: " .. what .. " (got " .. tostring(got) .. ", want " .. tostring(want) .. ")") end
end
local function has(s, sub, what) eq(type(s) == "string" and s:find(sub, 1, true) ~= nil, true, what .. " [" .. tostring(s):sub(1, 300) .. "]") end
local function near(got, want, tol, what) eq(type(got) == "number" and math.abs(got - want) <= tol, true, what .. " (got " .. tostring(got) .. ", want " .. want .. ")") end

local function toc_files(dir, name)
    local files = {}
    for line in io.lines(dir .. "/" .. name .. ".toc") do
        if line ~= "" and not line:match("^#") then files[#files + 1] = line end
    end
    return files
end

local function widget(kind)
    local w = { kind = kind, shown = true, scripts = {}, events = {} }
    function w:SetText(s) rawset(self, "text", s) end
    function w:SetScript(k, fn) self.scripts[k] = fn end
    function w:Show() rawset(self, "shown", true) end
    function w:Hide() rawset(self, "shown", false) end
    function w:IsShown() return rawget(self, "shown") end
    function w:RegisterEvent(e) self.events[e] = true end
    function w:SetRotation(r) rawset(self, "rotation", r) end
    function w:SetVertexColor(r, g, b) rawset(self, "colour", { r, g, b }) end
    function w:SetHeight(h) rawset(self, "height", h) end
    function w:CreateFontString() return widget("FontString") end
    function w:CreateTexture() return widget("Texture") end
    function w:Click() if self.scripts.OnClick then self.scripts.OnClick(self) end end
    return setmetatable(w, { __index = function() return function() end end })
end

-- maps: map x runs east, map y south. The world frame is WoW's: world X grows NORTH, world Y grows WEST, so map
-- east = -Y and map south = -X -- the axes do not line up, which is what the conversion has to get right.
local MAPS = {
    [1412] = { continent = 1, north = 0, west = 0, width = 1000, height = 700 },       -- yards
    [1413] = { continent = 1, north = 0, west = -1000, width = 1000, height = 700 },   -- the map due east of it
    [1999] = { continent = 2, north = 0, west = 0, width = 800, height = 600 },        -- another continent
}

local function world(opts)
    return { map = 1412, x = 0.5, y = 0.5, facing = 0, tracked = 101, out = {},
             quests = {   -- the log, with one header
                 { header = true, title = "Durotar" },
                 { id = 101, title = "Sarkoth", watched = true },
                 { id = 102, title = "Vile Familiars" },
                 { id = 103, title = "Cactus Apples" },
                 { id = 104, title = "Report to Orgnil" },
                 { id = 105, title = "Lost in Thought" },
                 { id = 106, title = "Far Away" },
             },
             waypoints = { [101] = { 1412, 0.5, 0.4 }, [104] = { 1413, 0.5, 0.5 }, [106] = { 1999, 0.5, 0.5 } },
             onmap = { [1412] = { { questID = 102, x = 0.8, y = 0.5 } } },
             objectives = { [103] = { { text = "Cactus Apple (30.0, 70.0): 0/10" } } },
             calls = 0, opts = opts or {} }
end

local function session(w, saved)
    local frames, env = {}, {}
    env.print = function(...)
        local t = {}
        for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
        w.out[#w.out + 1] = table.concat(t, " ")
    end
    for _, k in ipairs({ "pairs", "ipairs", "type", "tostring", "tonumber", "select", "string", "table", "math",
                         "next", "error", "assert", "pcall", "unpack", "setmetatable", "getmetatable", "rawget", "rawset" }) do env[k] = _G[k] end
    env._G = env
    env.time = function() return 1763000000 end
    env.GetLocale = function() return "enUS" end
    env.UnitName = function() return "Emily" end
    env.GetRealmName = function() return "Forever" end
    env.C_Map = {
        GetBestMapForUnit = function() return w.map end,
        GetPlayerMapPosition = function(map) if w.nopos then return nil end
            return { GetXY = function() return w.x, w.y end } end,
    }
    if not w.opts.noworld then
        env.CreateVector2D = function(x, y) return { x = x, y = y } end
        env.C_Map.GetWorldPosFromMapPos = function(map, v)
            local m = MAPS[map]
            if not m then return nil end
            local X, Y = m.north - v.y * m.height, m.west - v.x * m.width
            return m.continent, { GetXY = function() return X, Y end }
        end
    end
    if not w.opts.nofacing then env.GetPlayerFacing = function() return w.facing end end
    local entries = w.quests
    if w.opts.classic then
        env.GetNumQuestLogEntries = function() return #entries end
        env.GetQuestLogTitle = function(i) local q = entries[i]; return q.title, 10, nil, q.header, nil, nil, nil, q.id end
        env.IsQuestWatched = function(i) return entries[i].watched end
        env.GetNumQuestLeaderBoards = function(i) return w.objectives[entries[i].id] and #w.objectives[entries[i].id] or 0 end
        env.GetQuestLogLeaderBoard = function(j, i) return w.objectives[entries[i].id][j].text end
    else
        env.C_QuestLog = {
            GetNumQuestLogEntries = function() return #entries end,
            GetInfo = function(i) local q = entries[i]; return { title = q.title, questID = q.id, isHeader = q.header } end,
            GetQuestWatchType = function(id) for _, q in ipairs(entries) do if q.id == id and q.watched then return 0 end end end,
            GetNextWaypoint = function(id) w.calls = w.calls + 1; local p = w.waypoints[id]; if p then return p[1], p[2], p[3] end end,
            GetQuestsOnMap = function(map) return w.onmap[map] or {} end,
            GetQuestObjectives = function(id) return w.objectives[id] or {} end,
        }
        env.C_SuperTrack = { GetSuperTrackedQuestID = function() return w.tracked or 0 end }
    end
    env.SlashCmdList = {}
    env.UIParent = widget("UIParent")
    env.CreateFrame = function(kind, name)
        local f = widget(kind)
        frames[#frames + 1] = f
        if name then env[name] = f end
        return f
    end
    for k, v in pairs(saved or {}) do env[k] = v end
    local s = { env = env, frames = frames }
    for _, addon in ipairs({ "RefluxCore", "CollectionJournal", "Huginn" }) do
        local dir = client .. "/AddOns/" .. addon
        local ns = {}
        for _, file in ipairs(toc_files(dir, addon)) do
            local chunk = assert(loadfile(dir .. "/" .. file)); setfenv(chunk, env); chunk(addon, ns)
        end
        if addon == "Huginn" then s.ns = ns end
    end
    function s.fire(event, ...) for _, f in ipairs(frames) do if f.events[event] and f.scripts.OnEvent then f.scripts.OnEvent(f, event, ...) end end end
    function s.tick(dt) for _, f in ipairs(frames) do if f.scripts.OnUpdate then f.scripts.OnUpdate(f, dt or 0.1) end end end
    function s.cmd(msg) w.out = {}; env.SlashCmdList.HUGINN(msg or ""); return table.concat(w.out, "\n") end
    for _, a in ipairs({ "RefluxCore", "CollectionJournal", "Huginn" }) do s.fire("ADDON_LOADED", a) end
    s.tick(0.1)
    return s
end

-- the arrow rows, in order: { title, info, arrow texture }
local function arrows(s)
    local list, ui = {}, s.ns.ui_for_tests and s.ns.ui_for_tests()
    for i = 1, #ui.current do
        local r = ui.rows[i]
        list[i] = { title = rawget(r.title, "text"), info = rawget(r.info, "text"), arrow = r.arrow }
    end
    return list
end

-- ===== nothing on: the tracked quest gets the arrow; distance in yards, direction from your facing =============
local w = world()
local s = session(w)
local a = arrows(s)
eq(#a, 1, "with no arrows on, one arrow: the tracked quest")
eq(a[1].title, "Sarkoth", "the tracked quest's title")
has(a[1].info, "70 yd", "0.1 of a 700-yard-tall map north of you is 70 yards")
has(a[1].info, "ahead", "facing north, it is ahead")
near(rawget(a[1].arrow, "rotation"), 0, 1e-6, "the arrow points straight up")
eq(rawget(a[1].arrow, "colour")[2] > 0.9, true, "and is green")
w.facing = math.pi / 2                                  -- facing west: north is to your right
s.tick(0.1)
a = arrows(s)
has(a[1].info, "right", "facing west, a target to the north is on your right")
near(rawget(a[1].arrow, "rotation"), math.rad(270), 1e-6, "the arrow turns 270 degrees counterclockwise (90 clockwise)")
eq(rawget(a[1].arrow, "colour")[1] > 0.9 and rawget(a[1].arrow, "colour")[2] < 0.5, true, "and is red: off to the side by 90 degrees")
w.facing = 0

-- ===== the mini quest log: clicking quests turns several arrows on ===========================================
local ui = s.ns.ui_for_tests()
eq(#ui.logRows, 6, "the mini log has a line per quest (the header left out)")
has(rawget(ui.logRows[1].label, "text"), "[ ] Sarkoth", "an off quest shows [ ]")
ui.logRows[2].button:Click()      -- Vile Familiars: a marker on this map
ui.logRows[3].button:Click()      -- Cactus Apples: coordinates in its objective text
s.tick(0.1)
a = arrows(s)
eq(#a, 2, "two arrows on at once (the tracked one steps aside)")
eq(a[1].title .. "|" .. a[2].title, "Vile Familiars|Cactus Apples", "in the order they were turned on")
has(a[1].info, "300 yd", "the map marker 0.3 east: 300 yards")
has(a[1].info, "right", "east of you, facing north: right")
has(a[2].info, "244 yd", "the objective's (30, 70): 200 yards west and 140 south is 244 yards")
has(a[2].info, "behind, left", "south-west of you, facing north: behind, left")
has(rawget(ui.logRows[2].label, "text"), "[x]", "an on quest shows [x] in the log")
eq(#s.env.HuginnDB.active, 2, "the arrows that are on are saved")

-- ===== a quest on another map of the same continent: placed through the world frame =========================
ui.logRows[4].button:Click()      -- Report to Orgnil: its next waypoint is the middle of the map to the east
s.tick(0.1)
a = arrows(s)
eq(a[3].title, "Report to Orgnil", "a third arrow")
has(a[3].info, "1000 yd", "the middle of the next map east is 1000 yards away")
has(a[3].info, "right", "and due east: on your right")
ui.logRows[6].button:Click()      -- Far Away: another continent
s.tick(0.1)
has(arrows(s)[4].info, "on another continent", "a quest on another continent says so instead of pointing")
ui.logRows[5].button:Click()      -- Lost in Thought: no location at all
s.tick(0.1)
has(arrows(s)[5].info, "no location from the client", "a quest the client gives no location for says so")
eq(rawget(ui.logRows[5].label, "text"):find("|cff7f7f7f", 1, true) ~= nil, true, "and is greyed in the log")
ui.logRows[2].button:Click()      -- Vile Familiars off again
s.tick(0.1)
eq(#arrows(s), 4, "clicking an on quest turns its arrow off")

-- ===== waypoints: added by command, several at once, cleared on arrival with a REFLUX row =====================
local out = s.cmd("way 50 45 Camp")
has(out, "waypoint arrow at 50.0, 45.0 (Camp)", "/huginn way adds a waypoint arrow")
s.tick(0.1)
a = arrows(s)
eq(a[#a].title, "Camp", "named by its note")
has(a[#a].info, "35 yd", "half a tenth of 700 yards north: 35")
local before = s.env.RefluxHost.total_dispatched()
w.y = 0.4504                                            -- walk there
w.out = {}
s.tick(0.1)
has(table.concat(w.out, "\n"), "you are there -- Camp", "arriving says so")
eq(s.env.RefluxHost.total_dispatched() - before >= 1, true, "and dispatches a REFLUX arrival")
eq(s.env.RefluxHost.action_type_at(s.env.RefluxHost.log_size() - 1), 3010, "type 3010")
eq(s.env.HuginnDB.arrivals, 1, "counted")
s.tick(0.1)
eq(#arrows(s), 4, "a reached waypoint clears itself")
w.y = 0.5

-- ===== the cap, clear, and the arrows coming back next session ================================================
for i = 1, 6 do s.cmd("way " .. (10 + i) .. " 10") end
out = s.cmd("way 90 90")
has(out, "8 arrows is the most at once", "more than eight is refused")
out = s.cmd("clear")
has(out, "every arrow off", "/huginn clear")
eq(#s.env.HuginnDB.active, 0, "and saves that")
s.cmd("2"); s.cmd("way 20 20 Rock")
local saved = { HuginnDB = s.env.HuginnDB }
local w2 = world()
local s2 = session(w2, saved)
a = arrows(s2)
eq(#a, 2, "next session: the same arrows are on")
eq(a[1].title .. "|" .. a[2].title, "Vile Familiars|Rock", "a quest and a waypoint")

-- ===== optimized: quest locations are looked up on changes and every 2 s, not every frame =====================
local w3 = world()
local s3 = session(w3)
local calls0 = w3.calls
for i = 1, 15 do s3.tick(0.1) end                     -- 1.5 s of frames
eq(w3.calls, calls0, "no quest lookups between changes, however many frames")
s3.fire("QUEST_LOG_UPDATE")
s3.tick(0.1)
eq(w3.calls > calls0, true, "a quest log change triggers one lookup")
local calls1 = w3.calls
for i = 1, 25 do s3.tick(0.1) end                     -- 2.5 s
eq(w3.calls - calls1 <= 6, true, "and the 2-second backstop refreshes once, not per frame")

-- ===== /huginn list, the log toggle, and degraded clients =====================================================
out = s3.cmd("list")
has(out, "1. Sarkoth * + -- has a location", "the list marks the tracked and watched quest")
has(out, "5. Lost in Thought -- no location from the client", "and quests with no location")
local u3 = s3.ns.ui_for_tests()
eq(u3.logShown, true, "the mini log starts open")
s3.cmd("log")
eq(u3.logShown, false, "/huginn log closes it")
eq(s3.env.HuginnDB.logShown, false, "and remembers")

local nw = world({ noworld = true })
local sn = session(nw)
a = arrows(sn)
has(a[1].info, "66 map units", "no world API: distances in map units (0.1 of a 667-unit-tall stand-in)")
local nf = world({ nofacing = true })
local sf = session(nf)
a = arrows(sf)
has(a[1].info, "north", "no facing: the compass word instead")
eq(rawget(a[1].arrow, "shown"), false, "and no arrow")
local cl = world({ classic = true })
local sc = session(cl)
out = sc.cmd("list")
has(out, "3. Cactus Apples -- has a location", "classic quest log: objective coordinates found through the leaderboard")
has(out, "1. Sarkoth + -- no location from the client", "and quests with none")
sc.cmd("3"); sc.tick(0.1)
has(arrows(sc)[1].info, "244 yd", "and its arrow works")

print(string.format("huginn: %d passed, %d failed", passed, failed))
os.exit(failed == 0 and 0 or 1)
