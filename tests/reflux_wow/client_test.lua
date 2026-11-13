-- tests/reflux_wow/client_test.lua -- the packaged Collection client, end to end, under real Lua 5.1.
-- usage: lua5.1 client_test.lua <client dir (wow-package-collection.sh output)> <workdir>
--
-- Loads AddOns/RefluxCore, CollectionJournal and FafnirsBed exactly as the client does -- each .toc's files in
-- order, each file given (addonName, addonTable) -- into a fresh stubbed WoW per login session, fires the
-- events a session sees, checks the inventory, the slash commands and the journal, then writes the
-- SavedVariables as the client would and runs the reader from the package's reader/ folder over them.
assert(_VERSION == "Lua 5.1", "must run under Lua 5.1, got " .. tostring(_VERSION))
local client, work = assert(arg[1]), assert(arg[2])

local passed, failed = 0, 0
local function eq(got, want, what)
    if got == want then passed = passed + 1
    else failed = failed + 1; print("FAIL: " .. what .. " (got " .. tostring(got) .. ", want " .. tostring(want) .. ")") end
end
local function has(s, sub, what) eq(type(s) == "string" and s:find(sub, 1, true) ~= nil, true, what .. " [" .. tostring(s) .. "]") end

local function toc_files(dir, name)
    local files, interface = {}, nil
    for line in io.lines(dir .. "/" .. name .. ".toc") do
        local iface = line:match("^## Interface: (%d+)")
        if iface then interface = iface end
        if line ~= "" and not line:match("^#") then files[#files + 1] = line end
    end
    return files, interface
end

-- One login session: a fresh global environment with the WoW API stubbed from `world`.
local function session(world, saved)
    local env = { print = function(...)
        local t = {}
        for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
        world.out[#world.out + 1] = table.concat(t, " ")
    end }
    for _, k in ipairs({ "pairs", "ipairs", "type", "tostring", "tonumber", "select", "string", "table", "math",
                         "next", "error", "assert", "pcall", "unpack", "setmetatable", "getmetatable", "rawget", "rawset" }) do
        env[k] = _G[k]
    end
    env._G = env
    env.time = function() return world.now end
    env.UnitName = function(u) return world.name end
    env.GetRealmName = function() return world.realm end
    env.GetMoney = function() return world.money end
    env.NUM_BAG_SLOTS = world.no_bag_globals and nil or 4
    env.BANK_CONTAINER = -1
    env.NUM_BANKBAGSLOTS = 7
    env.ATTACHMENTS_MAX_RECEIVE = 16
    local function slots(bag) local b = world.bags[bag]; return b and #b or 0 end
    local function cell(bag, slot) local b = world.bags[bag]; return b and b[slot] end
    if world.api == "c_container" then
        env.C_Container = {
            GetContainerNumSlots = slots,
            GetContainerItemInfo = function(bag, slot)
                local c = cell(bag, slot)
                if not c or not c.id then return nil end
                return { itemID = c.id, stackCount = c.n, hyperlink = "|Hitem:" .. c.id .. "|h[" .. c.name .. "]|h" }
            end,
        }
    elseif world.api == "legacy" then
        env.GetContainerNumSlots = slots
        env.GetContainerItemInfo = function(bag, slot)
            local c = cell(bag, slot)
            if not c or not c.id then return nil end
            return "tex", c.n, false, 1, false, false, "|Hitem:" .. c.id .. "|h[" .. c.name .. "]|h", false, false, c.id
        end
    end
    env.GetInboxNumItems = function() return #world.mail end
    env.GetInboxItem = function(i, j) local m = world.mail[i] and world.mail[i][j]; if m then return m.name, m.id, "tex", m.n end end
    if world.c_map then
        env.C_Map = {
            GetBestMapForUnit = function() return 1415 end,
            GetPlayerMapPosition = function() return { GetXY = function() return 0.25, 0.75 end } end,
        }
    end
    local frames = {}
    env.CreateFrame = function()
        local f = { events = {}, scripts = {} }
        function f:RegisterEvent(e) self.events[e] = true end
        function f:SetScript(k, fn) self.scripts[k] = fn end
        function f:GetScript(k) return self.scripts[k] end
        frames[#frames + 1] = f
        return f
    end
    env.SlashCmdList = {}
    for k, v in pairs(saved) do env[k] = v end          -- SavedVariables are set before the addon's files run

    local s = { env = env }
    function s.fire(event, ...)
        for _, f in ipairs(frames) do
            if f.events[event] and f.scripts.OnEvent then f.scripts.OnEvent(f, event, ...) end
        end
    end
    function s.tick(dt) for _, f in ipairs(frames) do if f.scripts.OnUpdate then f.scripts.OnUpdate(f, dt) end end end
    function s.slash(key, msg) world.out = {}; env.SlashCmdList[key](msg or ""); return table.concat(world.out, "\n") end
    function s.load(addon)
        local dir = client .. "/AddOns/" .. addon
        local files, iface = toc_files(dir, addon)
        local ns = {}
        for _, file in ipairs(files) do
            local chunk = assert(loadfile(dir .. "/" .. file))
            setfenv(chunk, env)
            chunk(addon, ns)
        end
        return iface
    end
    return s
end

local function item(id, name, n) return { id = id, name = name, n = n } end
local world = {
    out = {}, now = 1763000000, name = "Emily", realm = "Forever", money = 1234567, api = "c_container", c_map = true,
    bags = { [0] = { item(2770, "Copper Ore", 20), item(2770, "Copper Ore", 5), {}, item(6948, "Hearthstone", 1) },
             [1] = { item(2589, "Linen Cloth", 12) } },
    mail = {},
}
local saved = {}

-- session 1 -----------------------------------------------------------------------------------------------
local s = session(world, saved)
for _, a in ipairs({ "RefluxCore", "CollectionJournal", "FafnirsBed" }) do eq(s.load(a) ~= nil, true, a .. " .toc declares an interface") end
eq(type(s.env.RefluxCore.modules["reflux/reflux"]), "table", "RefluxCore registered its module")
eq(type(s.env.RefluxCore.modules["collection/inventory"]), "table", "Fafnir's Bed registered its module")
eq(type(s.env.RefluxCore.modules["collection/journal"]), "table", "the journal registered its module")
s.fire("ADDON_LOADED", "RefluxCore")
s.fire("ADDON_LOADED", "CollectionJournal")
s.fire("ADDON_LOADED", "FafnirsBed")
s.fire("PLAYER_LOGIN")
local inv = s.env.FafnirsBedDB
eq(inv.chars["Emily-Forever"].loc.bags[2770], 25, "two ore stacks counted as 25")
eq(inv.chars["Emily-Forever"].gold, 1234567, "gold read")
eq(inv.names[2770], "Copper Ore", "item names learned from links")

-- loot 10 ore, sell the linen, earn money
world.bags[0][3] = item(2770, "Copper Ore", 10)
world.bags[1][1] = {}
world.money = 1300000
world.now = world.now + 60
s.fire("BAG_UPDATE_DELAYED")
s.fire("PLAYER_MONEY")
eq(inv.chars["Emily-Forever"].loc.bags[2770], 35, "loot raises the count")
eq(inv.chars["Emily-Forever"].loc.bags[2589], nil, "a sold item leaves the bags")

-- open the bank: 40 ore and a bag of 3 more there
world.bags[-1] = { item(2770, "Copper Ore", 40) }
world.bags[5] = { item(2770, "Copper Ore", 3) }
s.fire("BANKFRAME_OPENED")
eq(inv.chars["Emily-Forever"].loc.bank[2770], 43, "bank and bank bags read while open")
s.fire("BANKFRAME_CLOSED")
world.bags[-1] = {}                                  -- unreadable once closed: must not be re-read as empty
s.fire("BAG_UPDATE_DELAYED")
eq(inv.chars["Emily-Forever"].loc.bank[2770], 43, "a closed bank keeps its last read")

-- mail: 2 attachments
world.mail = { { item(2589, "Linen Cloth", 20), item(2770, "Copper Ore", 2) } }
s.fire("MAIL_INBOX_UPDATE")
eq(inv.chars["Emily-Forever"].loc.mail[2589], 20, "mail attachments read")

local out = s.slash("FAFNIRSBED", "")
has(out, "net inventory -- 1 characters", "/fafnir shows the net totals")
has(out, "130g 0s 0c", "/fafnir shows account gold")
out = s.slash("FAFNIRSBED", "copper")
has(out, "Copper Ore -- net 80", "search by name: 35 bags + 43 bank + 2 mail")
has(out, "Emily-Forever 80", "who has it")
out = s.slash("FAFNIRSBED", "|Hitem:2589|h[Linen Cloth]|h")
has(out, "Linen Cloth -- net 20", "an item link works as the query")
out = s.slash("FAFNIRSBED", "mithril")
has(out, "nothing seen named like", "an unknown name says so")

-- the journal saw Fafnir's Bed's REFLUX events
s.tick(10)
local jdb = s.env.CollectionJournalDB
local types = {}
for seq = jdb.firstSeq, jdb.nextSeq - 1 do local t = jdb.rows[seq].type; types[t] = (types[t] or 0) + 1 end
eq((types[2010] or 0) > 0, true, "item deltas (2010) reached the journal through REFLUX")
eq((types[2011] or 0), 2, "two gold changes (2011): login, then the sale")
eq((types[2012] or 0) > 0, true, "location reads (2012) journalled")
eq(jdb.rows[jdb.firstSeq].zone, 1415, "journal rows carry the zone")
out = s.slash("COLLECTIONJOURNAL")
has(out, "rows held", "/cov reports the journal")
-- the real client tears the bags down before PLAYER_LOGOUT (0 slots); the snapshot must survive it
-- (the founder's first live account came back with every character's bags empty)
local kept_bags = world.bags
world.bags = {}
s.fire("BAG_UPDATE_DELAYED")
s.fire("PLAYER_LOGOUT")
world.bags = kept_bags
eq(s.env.FafnirsBedDB.chars["Emily-Forever"].loc.bags[2770], 35, "torn-down bags at logout do not wipe the snapshot")
saved.FafnirsBedDB = s.env.FafnirsBedDB             -- account-wide: carried to the next session
local journal_emily = s.env.CollectionJournalDB      -- per character: stays with Emily

-- session 2: an alt, on the OLDER container API, no C_Map ---------------------------------------------------
world.name, world.api, world.c_map, world.money = "Brokkr", "legacy", false, 50000
world.bags = { [0] = { item(2770, "Copper Ore", 7) } }
world.mail = {}
world.now = world.now + 3600
s = session(world, { FafnirsBedDB = saved.FafnirsBedDB })
for _, a in ipairs({ "RefluxCore", "CollectionJournal", "FafnirsBed" }) do s.load(a) end
s.fire("ADDON_LOADED", "CollectionJournal")
s.fire("ADDON_LOADED", "FafnirsBed")
s.fire("PLAYER_LOGIN")
out = s.slash("FAFNIRSBED", "2770")
has(out, "net 87", "net ore across both characters (80 + 7)")
has(out, "Brokkr-Forever 7", "the alt is listed")
has(out, "Emily-Forever 80", "and the main, from the saved table")
out = s.slash("FAFNIRSBED", "")
has(out, "2 characters", "two characters tracked")
has(out, "135g 0s 0c", "account gold sums both")
s.tick(10)
eq(s.env.CollectionJournalDB.rows[1].zone, nil, "no C_Map: zone is nil, not guessed")
s.fire("PLAYER_LOGOUT")
local journal_brokkr = s.env.CollectionJournalDB

-- session 3: a client with no container API at all says so instead of recording empty bags
world.api = "none"
s = session(world, { FafnirsBedDB = saved.FafnirsBedDB })
for _, a in ipairs({ "RefluxCore", "CollectionJournal", "FafnirsBed" }) do s.load(a) end
s.fire("ADDON_LOADED", "FafnirsBed")
s.fire("PLAYER_LOGIN")
out = s.slash("FAFNIRSBED", "")
has(out, "bags not read on this client", "a missing bag API is reported")
eq(saved.FafnirsBedDB.chars["Brokkr-Forever"].loc.bags[2770], 7, "and the last real read is kept")

-- the reader, from the package, over the files the client would have written -------------------------------
local function ser(v, ind)
    local t = type(v)
    if t == "string" then return string.format("%q", v) end
    if t == "number" then return v == math.floor(v) and string.format("%d", v) or string.format("%.17g", v) end
    if t == "boolean" then return tostring(v) end
    if t ~= "table" then return "nil" end
    local out = { "{\n" }
    for k, x in pairs(v) do
        local key = type(k) == "number" and ("[" .. k .. "]") or ("[" .. string.format("%q", k) .. "]")
        out[#out + 1] = string.rep("\t", ind + 1) .. key .. " = " .. ser(x, ind + 1) .. ",\n"
    end
    out[#out + 1] = string.rep("\t", ind) .. "}"
    return table.concat(out)
end
local function write_sv(path, var, db) local f = assert(io.open(path, "wb")); f:write(var .. " = " .. ser(db, 0) .. "\n"); f:close() end
write_sv(work .. "/emily.lua", "CollectionJournalDB", journal_emily)
write_sv(work .. "/brokkr.lua", "CollectionJournalDB", journal_brokkr)
local R = dofile(client .. "/reader/journal_reader.lua")
local I = dofile(client .. "/reader/ids.lua")
local events = work .. "/events.ndjson"
os.remove(events)
local r1 = assert(R.read_file({ sv_path = work .. "/emily.lua", var = "CollectionJournalDB", events_path = events, classify = I.collectionAddonOf }))
local r2 = assert(R.read_file({ sv_path = work .. "/brokkr.lua", var = "CollectionJournalDB", events_path = events, classify = I.collectionAddonOf }))
eq(r1.appended > 0 and r2.appended > 0, true, "both characters' journals become events")
local n, src = 0, 0
for line in io.lines(events) do
    n = n + 1
    if line:find('"source":"collection/addon-2"', 1, true) then src = src + 1 end
end
eq(n, r1.appended + r2.appended, "one line per journal row")
eq(src, n, "every event is attributed to addon 2 (Fafnir's Bed)")

print(string.format("%d passed, %d failed", passed, failed))
if failed > 0 then os.exit(1) end
