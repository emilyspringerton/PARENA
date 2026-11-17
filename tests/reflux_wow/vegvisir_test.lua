-- tests/reflux_wow/vegvisir_test.lua -- Vegvísir, the old map (/oldmap), in the packaged client under real Lua 5.1,
-- against a stubbed client: positions, frames, font strings, buttons, the other addons' saved tables, the journal.
-- usage: lua5.1 vegvisir_test.lua <client dir>
assert(_VERSION == "Lua 5.1", "must run under Lua 5.1")
local client = assert(arg[1])

local passed, failed = 0, 0
local function eq(got, want, what)
    if got == want then passed = passed + 1
    else failed = failed + 1; print("FAIL: " .. what .. " (got " .. tostring(got) .. ", want " .. tostring(want) .. ")") end
end
local function has(s, sub, what) eq(type(s) == "string" and s:find(sub, 1, true) ~= nil, true, what .. " [" .. tostring(s):sub(1, 300) .. "]") end

local function toc_files(dir, name)
    local files = {}
    for line in io.lines(dir .. "/" .. name .. ".toc") do
        if line ~= "" and not line:match("^#") then files[#files + 1] = line end
    end
    return files
end

-- a widget that records what it is told; anything else it is asked is a no-op
local function widget(kind, all)
    local w = { kind = kind, shown = true, scripts = {}, events = {} }
    function w:SetText(s) rawset(self, "text", s) end
    function w:GetText() return rawget(self, "text") or "" end
    function w:SetScript(k, fn) self.scripts[k] = fn end
    function w:GetScript(k) return self.scripts[k] end
    function w:Show() rawset(self, "shown", true); if self.scripts.OnShow then self.scripts.OnShow(self) end end
    function w:Hide() rawset(self, "shown", false) end
    function w:IsShown() return rawget(self, "shown") end
    function w:RegisterEvent(e) self.events[e] = true end
    function w:SetColorTexture(r, g, b, a) rawset(self, "rgba", { r, g, b, a }) end
    function w:SetTexture(t) rawset(self, "texture", t) end
    function w:GetCenter() return 100, 100 end
    function w:GetEffectiveScale() return 1 end
    function w:CreateFontString() local f = widget("FontString", all); all[#all + 1] = f; return f end
    function w:CreateTexture() local t = widget("Texture", all); all[#all + 1] = t; return t end
    function w:Click() if self.scripts.OnClick then self.scripts.OnClick(self) end end
    return setmetatable(w, { __index = function() return function() end end })
end

local W = { now = 1763000000, clock = 0, map = 1429, x = 0.25, y = 0.75, name = "Emily", class = "DRUID" }
local function session(opts)
    opts = opts or {}
    local out, all, frames = {}, {}, {}
    local env = { print = function(...)
        local t = {}
        for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
        out[#out + 1] = table.concat(t, " ")
    end }
    for _, k in ipairs({ "pairs", "ipairs", "type", "tostring", "tonumber", "select", "string", "table", "math",
                         "next", "error", "assert", "pcall", "unpack", "setmetatable", "getmetatable", "rawget", "rawset" }) do env[k] = _G[k] end
    env._G = env
    env.time = function() return W.now end
    env.GetTime = function() return W.clock end
    env.GetLocale = function() return "enUS" end
    env.UnitName = function() return W.name end
    env.GetRealmName = function() return "Forever" end
    env.UnitClass = function() return W.class, W.class end
    env.GetZoneText = function() return "Teldrassil" end
    env.RAID_CLASS_COLORS = { DRUID = { r = 1, g = 0.49, b = 0.04 }, PALADIN = { r = 0.96, g = 0.55, b = 0.73 } }
    env.C_Map = {
        GetBestMapForUnit = function() return W.map end,
        GetPlayerMapPosition = function(map) if map == W.map and W.x then return { GetXY = function() return W.x, W.y end } end end,
        GetMapInfo = function(map) return ({ [1429] = { name = "Elwynn Forest" }, [1426] = { name = "Dun Morogh" } })[map] end,
    }
    env.SlashCmdList = { MACROSYNC = function() W.ascended = (W.ascended or 0) + 1 end }
    env.UISpecialFrames = {}
    env.UIParent = widget("UIParent", all)
    env.Minimap = widget("Minimap", all)
    env.GameTooltip = widget("GameTooltip", all)
    env.GetCursorPosition = function() return 180, 100 end
    env.CreateFrame = function(kind, name, parent, template)
        local f = widget(kind, all)
        f.template, f.parent = template, parent
        frames[#frames + 1] = f
        if name then env[name] = f end
        return f
    end
    env.FafnirsBedDB = opts.hoard
    env.AndvarisMapDB = opts.andvari
    env.CharacterBoardDB = opts.board
    env.VegvisirDB = opts.saved
    local s = { env = env, out = out, all = all, frames = frames }
    function s.load(addon)
        local dir = client .. "/AddOns/" .. addon
        local ns = {}
        for _, file in ipairs(toc_files(dir, addon)) do
            local chunk = assert(loadfile(dir .. "/" .. file)); setfenv(chunk, env); chunk(addon, ns)
        end
        s.ns = ns
    end
    function s.fire(event, ...) for _, f in ipairs(frames) do if f.events[event] and f.scripts.OnEvent then f.scripts.OnEvent(f, event, ...) end end end
    function s.advance(secs)
        for _ = 1, math.max(1, math.floor(secs)) do
            W.now, W.clock = W.now + 1, W.clock + 1
            for _, f in ipairs(frames) do if f.scripts.OnUpdate then f.scripts.OnUpdate(f, 1) end end
        end
    end
    function s.cmd(msg) for i = #out, 1, -1 do out[i] = nil end; env.SlashCmdList.VEGVISIR(msg or ""); return table.concat(out, "\n") end
    function s.texts()
        local t = {}
        for _, f in ipairs(all) do if f.kind == "FontString" and rawget(f, "text") and f:IsShown() then t[#t + 1] = rawget(f, "text") end end
        return table.concat(t, "\n")
    end
    function s.buttons(field)
        local b = {}
        for _, f in ipairs(frames) do if f.kind == "Button" and (not field or rawget(f, field)) then b[#b + 1] = f end end
        return b
    end
    for _, a in ipairs({ "RefluxCore", "CollectionJournal", "Vegvisir" }) do s.load(a) end
    for _, a in ipairs({ "RefluxCore", "CollectionJournal", "Vegvisir" }) do s.fire("ADDON_LOADED", a) end
    return s
end

-- ===== the recorder: fields, lands, where each of us is, first arrivals in the journal ======================
local s = session()
eq(type(s.env.VegvisirDB), "table", "the account-wide table exists after ADDON_LOADED")
s.fire("PLAYER_LOGIN")
local db = s.env.VegvisirDB
local V = s.env.RefluxCore.modules["collection/vegvisir"]
local cell = V.vvCell(250, 750)
eq(db.seen[1429] and db.seen[1429][cell], 1, "the field under your feet is charted at login")
eq(db.lands[1429] and db.lands[1429].n, 1, "one field of the land is charted")
eq(db.lands[1429].name, "Elwynn Forest", "the land's name comes from the client")
local me = db.where["Emily-Forever"]
eq(me and me.map, 1429, "where you are is noted")
eq(me and (me.x .. "," .. me.y), "250,750", "in permille of the map")
eq(me and me.class, "DRUID", "with your class, for the pin's colour")
eq(db.went["Emily-Forever"] and db.went["Emily-Forever"][1429] ~= nil, true, "and that you have walked this land")
eq(V.vvCellCol(cell), 9, "the field's column (250 permille of 32)")
eq(V.vvCellRow(cell), 25, "and its row (750 permille of 32)")
eq(V.vvCell(999, 999), 1024, "the bottom-right corner is field 1024")
eq(V.vvCell(0, 0), 1, "the top-left corner is field 1")
s.advance(7)
eq(db.lands[1429].n, 1, "standing still charts nothing new")
W.x, W.y = 0.26, 0.75
s.advance(3)
eq(db.lands[1429].n, 1, "a step inside the same field charts nothing new either")
W.x, W.y = 0.50, 0.50
s.advance(3)
eq(db.lands[1429].n, 2, "a step into another field charts it")
eq(V.vvCoverage(db, 1429), math.floor(2 * 1000 / 1024), "coverage is permille of the 1024 fields")
-- a milestone: the 256th field makes a quarter
for c = 1, 256 do db.seen[1429][c] = 1 end
db.seen[1429][497], db.seen[1429][777], db.seen[1429][256] = nil, nil, nil
local n = 0
for _ in pairs(db.seen[1429]) do n = n + 1 end
db.lands[1429].n = n
eq(n, 255, "the sheet set up with 255 fields charted")
W.x, W.y = 0.999, 0.999
for i = #s.out, 1, -1 do s.out[i] = nil end
s.advance(3)
eq(db.lands[1429].n, 256, "the 256th field")
has(table.concat(s.out, "\n"), "Vegvísir: a quarter of Elwynn Forest is charted.", "crossing a quarter is announced")
eq(V.vvMilestone(240, 260), 250, "the milestone crossed is named")
eq(V.vvMilestone(260, 270), 0, "no milestone between them")
eq(V.vvMilestone(0, 1000), 1000, "the largest one crossed wins")
-- another land
W.map, W.x, W.y = 1426, 0.1, 0.1
s.fire("ZONE_CHANGED_NEW_AREA")
eq(db.lands[1426] and db.lands[1426].name, "Dun Morogh", "a zone change charts the new land at once")
eq(db.where["Emily-Forever"].map, 1426, "and moves your pin")
-- the journal: first arrivals (2420) and the milestone (2421), in addon 43's block
s.advance(10)
local j, types = s.env.CollectionJournalDB, {}
for seq = j.firstSeq, j.nextSeq - 1 do local r = j.rows[seq]; types[r.type] = (types[r.type] or 0) + 1 end
eq(types[2420], 2, "two first arrivals journalled (one per land)")
eq(types[2421], 1, "one milestone journalled")

-- ===== a second character draws on the same sheet ============================================================
local saved = s.env.VegvisirDB
W.name, W.class, W.map, W.x, W.y = "Bjorn", "PALADIN", 1429, 0.8, 0.8
local s2 = session({ saved = saved })
s2.fire("PLAYER_LOGIN")
local db2 = s2.env.VegvisirDB
eq(db2.where["Bjorn-Forever"] and db2.where["Bjorn-Forever"].map, 1429, "Bjorn's pin")
eq(db2.where["Emily-Forever"] and db2.where["Emily-Forever"].map, 1426, "Emily's pin is still where she logged out")
eq(db2.lands[1429].n, 257, "Bjorn charts onto Emily's fields, not a fresh sheet")
eq(db2.went["Bjorn-Forever"][1429] ~= nil and db2.went["Emily-Forever"][1429] ~= nil, true, "both have walked Elwynn")

-- ===== the window: pages, pins from other addons' saves, the hoard, the doors ===============================
local hoard = { v = 1, names = { [2589] = "Linen Cloth", [6948] = "Hearthstone" },
    chars = { ["Emily-Forever"] = { gold = 12345, seen = W.now, loc = { bags = { [2589] = 16, [6948] = 1 }, bank = { [2589] = 4 } } },
              ["Bjorn-Forever"] = { gold = 289, seen = W.now, loc = { bags = { [6948] = 1 } } } },
    post = { ["Bjorn-Forever"] = { [1] = { from = "Emily-Forever", copper = 250, items = { [2589] = 5 } } } } }
local andvari = { v = 1, marks = { { map = 1429, x = 300, y = 300, made = W.now - 100 }, { map = 1429, x = 600, y = 100, made = W.now - 5000, found = W.now } } }
local board = { chars = { ["Astrid-Forever"] = { t = W.now - 90000, class = "MAGE", level = 23, zone = "Westfall", map = 1436 } } }
W.name, W.class, W.map, W.x, W.y = "Emily", "DRUID", 1429, 0.25, 0.75
local s3 = session({ saved = saved, hoard = hoard, andvari = andvari, board = board })
s3.fire("PLAYER_LOGIN")
local out = s3.cmd("")
eq(s3.env.VegvisirFrame and s3.env.VegvisirFrame:IsShown(), true, "/oldmap opens the window")
eq(select(2, out:gsub("\n", "")) >= 2, true, "and a haiku is said on opening")
has(s3.texts(), "Vegvísir -- the old map", "the title")
has(s3.texts(), "2 lands walked by 2 of us", "the world page: lands and folk")
has(s3.texts(), "Elwynn Forest  --  ", "a row per land")
has(s3.texts(), "(Bjorn, Emily here)", "who is on it now")
local doors = s3.buttons("door")
eq(#doors, #s3.ns.addons, "one door per addon in the registry")
local ms
for _, b in ipairs(doors) do if b.door.folder == "MacroSync" then ms = b end end
eq(ms ~= nil, true, "MacroSync has a door")
ms:Click()
eq(W.ascended, 1, "and the door runs its command")
local self_door = false
for _, b in ipairs(doors) do if b.door.folder == "Vegvisir" then self_door = true end end
eq(self_door, false, "the map is not a door to itself")
-- this land
s3.cmd("here")
has(s3.texts(), "Elwynn Forest  --  ", "this land's head")
has(s3.texts(), "fields charted", "with its count")
local pins = s3.buttons("pin")
local kinds = {}
for _, b in ipairs(pins) do if b:IsShown() then kinds[b.pin] = (kinds[b.pin] or 0) + 1 end end
eq(kinds.folk, 2, "two of us pinned on Elwynn")
eq(kinds.x, 2, "Andvari's two Xs pinned from his own save")
local fog, seen_wash = 0, 0
for _, t in ipairs(s3.all) do
    if t.kind == "Texture" and t:IsShown() and rawget(t, "rgba") then
        if t.rgba[4] == 0.45 then fog = fog + 1 elseif t.rgba[4] == 0.28 then seen_wash = seen_wash + 1 end
    end
end
eq(fog + seen_wash, 1024, "every field is drawn: fog or charted wash")
eq(seen_wash, s3.env.VegvisirDB.lands[1429].n, "the charted fields are the ones washed in ink")
-- the hoard
s3.cmd("hoard")
has(s3.texts(), "2 kinds of thing, 22 in all", "the hoard's head counts every stack in bags and bank")
has(s3.texts(), "1g 28s 84c in purses and the post", "and the gold, parcels included")
has(s3.texts(), "Linen Cloth  x20  --  Emily 16 in bags, Emily 4 in the bank", "a row names who holds what")
s3.env.VegvisirHoardSearch:SetText("hearth")
s3.env.VegvisirHoardSearch.scripts.OnTextChanged(s3.env.VegvisirHoardSearch)
has(s3.texts(), "Hearthstone  x2", "the search narrows it")
eq(s3.texts():find("Linen Cloth  x20", 1, true), nil, "and hides the rest")
-- the folk
s3.cmd("folk")
has(s3.texts(), "3 of us, on 2 lands", "the folk page counts everyone we know of")
has(s3.texts(), "Astrid (23)  --  Westfall, 1 d ago", "a character known only to the Character Board")
has(s3.texts(), "Bjorn  --  Elwynn Forest", "a character the map has seen")
-- the stamp
eq(s3.env.VegvisirMinimapStamp ~= nil, true, "a stamp on the minimap")
s3.env.VegvisirMinimapStamp:Click()
eq(s3.env.VegvisirFrame:IsShown(), false, "it closes the map")
s3.env.VegvisirMinimapStamp:Click()
eq(s3.env.VegvisirFrame:IsShown(), true, "and opens it again")
s3.cmd("nonsense")
has(table.concat(s3.out, "\n"), "/oldmap", "anything else prints the help")

-- ===== no frames: every page is printed in chat =============================================================
local s4 = session({ saved = saved, hoard = hoard })
s4.env.CreateFrame, s4.env.UIParent = nil, nil   -- the kit's own frame is made; the window never can be
s4.fire("PLAYER_LOGIN")
has(s4.cmd("world"), "2 lands walked", "the world in chat")
has(s4.cmd("hoard"), "Linen Cloth  x20", "the hoard in chat")
has(s4.cmd("folk"), "Bjorn  --  Elwynn Forest", "the folk in chat")
has(s4.cmd("here"), "Elwynn Forest  --  ", "this land in chat")

print(string.format("vegvisir: %d passed, %d failed", passed, failed))
os.exit(failed == 0 and 0 or 1)
