-- tests/reflux_wow/board_csv_test.lua -- the Character Board (spec addon 1) in a stubbed client across three sessions,
-- then the CSV export run on the SavedVariables it leaves, as the founder would run it after logging out.
-- usage: lua5.1 board_csv_test.lua <client dir> <workdir>
assert(_VERSION == "Lua 5.1", "must run under Lua 5.1")
local client, work = assert(arg[1]), assert(arg[2])

local passed, failed = 0, 0
local function eq(got, want, what)
    if got == want then passed = passed + 1
    else failed = failed + 1; print("FAIL: " .. what .. " (got " .. tostring(got) .. ", want " .. tostring(want) .. ")") end
end
local function has(s, sub, what) eq(type(s) == "string" and s:find(sub, 1, true) ~= nil, true, what .. " [" .. tostring(s):sub(1, 300) .. "]") end

local function toc_files(dir, name)
    local files = {}
    for line in io.lines(dir .. "/" .. name .. ".toc") do if line ~= "" and not line:match("^#") then files[#files + 1] = line end end
    return files
end

local W = { now = 1763000000, out = {} }

local function session(p, saved)
    local env = { print = function(...) local t = {}; for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end; W.out[#W.out + 1] = table.concat(t, " ") end }
    for _, k in ipairs({ "pairs", "ipairs", "type", "tostring", "tonumber", "select", "string", "table", "math", "next", "error", "assert", "pcall", "unpack" }) do env[k] = _G[k] end
    env._G = env
    env.time = function() return W.now end
    env.date = function(f, t) return os.date("!" .. f:gsub("^!", ""), t) end
    env.GetLocale = function() return "enUS" end
    env.UnitName = function() return p.name end
    env.GetRealmName = function() return "Forever" end
    env.UnitClass = function() return "Paladin", p.class end
    env.UnitRace = function() return "Tauren", "Tauren" end
    env.UnitLevel = function() return p.level end
    env.UnitXP = function() return p.xp end
    env.UnitXPMax = function() return p.xpmax end
    env.GetXPExhaustion = function() return p.rested end
    env.GetMoney = function() return p.gold end
    env.GetZoneText = function() return p.zone end
    env.C_Map = { GetBestMapForUnit = function() return 1412 end, GetPlayerMapPosition = function() return nil end }
    env.RequestTimePlayed = function() W.played_asked = true end
    env.GetProfessions = function() return 1, 2 end
    env.GetProfessionInfo = function(i) if i == 1 then return "Mining", nil, p.mining, 150 end return "Herbalism", nil, 12, 75 end
    env.NUM_BAG_SLOTS, env.BANK_CONTAINER, env.NUM_BANKBAGSLOTS, env.ATTACHMENTS_MAX_RECEIVE = 4, -1, 7, 16
    env.C_Container = {
        GetContainerNumSlots = function(bag) return bag == 0 and #p.bag or 0 end,
        GetContainerItemInfo = function(bag, slot)
            local c = bag == 0 and p.bag[slot]
            if c then return { itemID = c[1], stackCount = c[2], hyperlink = "|Hitem:" .. c[1] .. "|h[" .. c[3] .. "]|h" } end
        end,
    }
    env.GetInboxNumItems = function() return 0 end
    local frames = {}
    env.CreateFrame = function()
        local f = { events = {}, scripts = {} }
        function f:RegisterEvent(e) self.events[e] = true end
        function f:SetScript(k, fn) self.scripts[k] = fn end
        frames[#frames + 1] = f
        return f
    end
    env.SlashCmdList = {}
    for k, v in pairs(saved) do env[k] = v end
    local s = { env = env }
    function s.fire(e, ...) for _, f in ipairs(frames) do if f.events[e] and f.scripts.OnEvent then f.scripts.OnEvent(f, e, ...) end end end
    function s.tick(dt) for _, f in ipairs(frames) do if f.scripts.OnUpdate then f.scripts.OnUpdate(f, dt) end end end
    function s.cmd(m) W.out = {}; env.SlashCmdList.CHARACTERBOARD(m or ""); return table.concat(W.out, "\n") end
    for _, a in ipairs({ "RefluxCore", "CollectionJournal", "FafnirsBed", "CharacterBoard" }) do
        local dir = client .. "/AddOns/" .. a; local ns = {}
        for _, file in ipairs(toc_files(dir, a)) do local c = assert(loadfile(dir .. "/" .. file)); setfenv(c, env); c(a, ns) end
    end
    for _, a in ipairs({ "RefluxCore", "CollectionJournal", "FafnirsBed", "CharacterBoard" }) do s.fire("ADDON_LOADED", a) end
    s.fire("PLAYER_LOGIN")
    return s
end

-- a SavedVariables writer, the way the client writes the file
local function ser(v, ind)
    ind = ind or ""
    if type(v) == "string" then return string.format("%q", v) end
    if type(v) ~= "table" then return tostring(v) end
    local keys = {}
    for k in pairs(v) do keys[#keys + 1] = k end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    local out = { "{\n" }
    for _, k in ipairs(keys) do
        local kk = type(k) == "number" and ("[" .. k .. "]") or ("[" .. string.format("%q", k) .. "]")
        out[#out + 1] = ind .. "  " .. kk .. " = " .. ser(v[k], ind .. "  ") .. ",\n"
    end
    out[#out + 1] = ind .. "}"
    return table.concat(out)
end
local function write_sv(path, var, v) local f = assert(io.open(path, "wb")); f:write(var .. " = " .. ser(v) .. "\n"); f:close() end

local saved = {}
local emily = { name = "Emily", class = "PALADIN", level = 11, xp = 500, xpmax = 4500, rested = 900, gold = 123456, zone = "Durotar",
                mining = 40, bag = { { 2770, 20, "Copper Ore" }, { 6948, 1, "Hearthstone" } } }

-- session 1: log in, play, level, log out
local s = session(emily, saved)
s.tick(1); s.tick(1); s.tick(1.5)
eq(#s.env.CharacterBoardDB.rows, 1, "a snapshot 3 s after login (no /played answer)")
emily.level, emily.xp, emily.zone, emily.gold = 12, 100, "The Barrens", 150000
W.now = W.now + 600
s.fire("PLAYER_LEVEL_UP", 12); s.tick(1.5)
eq(#s.env.CharacterBoardDB.rows, 2, "a snapshot on the level")
s.fire("PLAYER_LOGOUT")
eq(#s.env.CharacterBoardDB.rows, 2, "logout seconds later with nothing changed is not a new row")
local out = s.cmd("")
has(out, "Emily-Forever -- level 12", "/board lists the character")
saved.CharacterBoardDB, saved.FafnirsBedDB = s.env.CharacterBoardDB, s.env.FafnirsBedDB

-- session 2: another character on the account
W.now = W.now + 3600
local brokkr = { name = "Brokkr", class = "WARLOCK", level = 7, xp = 10, xpmax = 2000, rested = 0, gold = 900, zone = "Tirisfal Glades",
                 mining = 0, bag = { { 2589, 12, "Linen Cloth" }, { 2770, 5, "Copper Ore" } } }
s = session(brokkr, saved)
s.fire("TIME_PLAYED_MSG", 7200, 600)
eq(#s.env.CharacterBoardDB.rows, 3, "/played's answer triggers the login snapshot")
eq(s.env.CharacterBoardDB.rows[3].played, 7200, "with the played time")
out = s.cmd("snap")
has(out, "snapshot taken", "/board snap")
saved.CharacterBoardDB, saved.FafnirsBedDB = s.env.CharacterBoardDB, s.env.FafnirsBedDB

-- the client writes the account's SavedVariables; the founder runs the export
local wtf = work .. "/WTF"
os.execute('bash -c "mkdir -p \'' .. wtf .. '/Account/1#1/SavedVariables\'"')
write_sv(wtf .. "/Account/1#1/SavedVariables/CharacterBoard.lua", "CharacterBoardDB", saved.CharacterBoardDB)
write_sv(wtf .. "/Account/1#1/SavedVariables/FafnirsBed.lua", "FafnirsBedDB", saved.FafnirsBedDB)
-- the Collection Journal is per character, and every row carries a map id and x/y: the page's maps
local function jrow(who, seq, t, zone, x, y)
    return { v = 1, id = who .. ":" .. seq, seq = seq, type = 2001, source = "reflux", character = who, time = t,
             zone = zone, x = x, y = y, payload = { 0, 0, 0 } }
end
for _, d in ipairs({ "Emily", "Brokkr" }) do
    os.execute('bash -c "mkdir -p \'' .. wtf .. '/Account/1#1/Forever/' .. d .. '/SavedVariables\'"')
end
write_sv(wtf .. "/Account/1#1/Forever/Emily/SavedVariables/CollectionJournal.lua", "CollectionJournalDB",
    { firstSeq = 2, nextSeq = 5, rows = {
        [1] = jrow("Emily-Forever", 1, W.now - 900, 1412, 0.1, 0.1),           -- below firstSeq: already trimmed
        [2] = jrow("Emily-Forever", 2, W.now - 600, 1412, 0.45123, 0.60201),
        [3] = jrow("Emily-Forever", 3, W.now - 540, 1412, 0.46, 0.61),
        [4] = jrow("Emily-Forever", 4, W.now - 480, 1412, nil, nil) } })       -- no position: not drawn
write_sv(wtf .. "/Account/1#1/Forever/Brokkr/SavedVariables/CollectionJournal.lua", "CollectionJournalDB",
    { firstSeq = 1, nextSeq = 2, rows = { [1] = jrow("Brokkr-Forever", 1, W.now, 1420, 0.3, 0.7) } })
local csvdir = work .. "/csv"
local function export()
    -- through bash -c: on Windows popen goes to cmd.exe, which has no VAR=value prefix
    local p = io.popen('bash -c "WOW_WTF_DIR=\'' .. wtf .. '\' COLLECTION_CSV_DIR=\'' .. csvdir .. '\' LUA51=lua5.1 bash \'' .. client .. '/reader/export-csv.sh\' 2>&1"')
    local o = p:read("*a"); p:close(); return o
end
local function read(name) local f = io.open(csvdir .. "/" .. name, "rb"); if not f then return "" end; local t = f:read("*a"); f:close(); return t end
local function lines(t) local n = 0; for _ in t:gmatch("[^\n]+") do n = n + 1 end; return n end

out = export()
has(out, "3 character snapshots", "the export reports the snapshots")
local chars = read("characters.csv")
has(chars, "time_utc,epoch,character,class,race,level,xp,xp_max,xp_pct,rested_xp,gold_copper,gold,zone,map_id,played_seconds,item_level,professions", "characters.csv header")
eq(lines(chars), 4, "a header and three snapshots")
has(chars, "Emily-Forever,PALADIN,Tauren,11,500,4500,11.1,900,123456,12.3456,Durotar,1412,0,0,Herbalism 12/75; Mining 40/150", "Emily's first snapshot, every field")
has(chars, "Emily-Forever,PALADIN,Tauren,12,100,4500,2.2,900,150000,15.0000,The Barrens", "her level-12 row")
has(chars, "Brokkr-Forever,WARLOCK", "Brokkr's row")
local bags = read("bags.csv")
has(bags, "Emily-Forever,bags,2770,Copper Ore,20,", "bags.csv holds Emily's ore")
has(bags, "Brokkr-Forever,bags,2589,Linen Cloth,12,", "and Brokkr's linen")
local net = read("inventory_net.csv")
has(net, "2770,Copper Ore,25,Brokkr-Forever 5; Emily-Forever 20", "net copper ore across the account, with holders")
local page = read("collection.html")
has(page, '<script id="data" type="application/json">{', "collection.html is written, its data built in")
has(page, '"name":"Emily-Forever"', "with Emily's card")
has(page, '"name":"Brokkr-Forever"', "and Brokkr's")
has(page, '["Emily-Forever","bags",2770,"Copper Ore",20]', "her ore in the inventory")
has(page, '[2770,"Copper Ore",25,"Brokkr-Forever 5; Emily-Forever 20"]', "the account total")
has(out, "open collection.html", "the export points at the page")
has(out, "3 positions", "three positions: trimmed and position-less rows are left out")
local pos = read("positions.csv")
has(pos, "character,time_utc,epoch,map_id,zone,x,y,event_type", "positions.csv header")
has(pos, ",0.4512,0.6020,2001", "Emily's first position, x/y to four places")
-- the stubbed client reports map 1412 in every zone, so the name is whichever snapshot came last; a name is there
eq(pos:find("Emily%-Forever,[^,]*,%d+,1412,[A-Z][^,]*,0%.4512") ~= nil, true, "named from a board snapshot of the same map")
has(pos, "Brokkr-Forever,", "and Brokkr's")
has(page, '"points":[["Brokkr-Forever",', "the page carries the points")
has(page, '"mapNames":{"1412":"', "and the map names")
has(page, '<h2>Maps</h2>', "and has a Maps section")
local hist = read("bags_history.csv")
eq(lines(hist), 5, "the first export writes the history header and every bag row")
out = export()
eq(lines(read("bags_history.csv")), 5, "exporting again with nothing new appends nothing")
-- a new session refreshes Emily; the next export appends only her rows
saved.FafnirsBedDB.chars["Emily-Forever"].seen = saved.FafnirsBedDB.chars["Emily-Forever"].seen + 100
saved.FafnirsBedDB.chars["Emily-Forever"].loc.bags[2770] = 40
write_sv(wtf .. "/Account/1#1/SavedVariables/FafnirsBed.lua", "FafnirsBedDB", saved.FafnirsBedDB)
export()
hist = read("bags_history.csv")
eq(lines(hist), 7, "a refreshed character's rows are appended (Emily's two)")
has(hist, "Emily-Forever,bags,2770,Copper Ore,40", "with her new count")
-- commas and quotes are escaped
saved.CharacterBoardDB.rows[1].zone = 'Orgrimmar, "the Drag"'
write_sv(wtf .. "/Account/1#1/SavedVariables/CharacterBoard.lua", "CharacterBoardDB", saved.CharacterBoardDB)
export()
has(read("characters.csv"), '"Orgrimmar, ""the Drag"""', "a field with a comma and quotes is quoted CSV")
-- a name that tries to close the page's <script> cannot
saved.CharacterBoardDB.rows[#saved.CharacterBoardDB.rows].zone = '</script><b>x'
write_sv(wtf .. "/Account/1#1/SavedVariables/CharacterBoard.lua", "CharacterBoardDB", saved.CharacterBoardDB)
export()
page = read("collection.html")
eq(page:find("</script><b>", 1, true), nil, "a zone holding </script> stays inside the page's JSON")
has(page, "\\u003c/script\\u003e\\u003cb\\u003ex", "escaped as \\u003c")

print(string.format("board+csv: %d passed, %d failed", passed, failed))
os.exit(failed == 0 and 0 or 1)
