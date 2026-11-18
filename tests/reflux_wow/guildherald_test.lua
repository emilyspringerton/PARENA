-- tests/reflux_wow/guildherald_test.lua -- GuildHerald in the packaged client, under real Lua 5.1, against a stubbed
-- client; then its SavedVariables through runtime/wow/guild_feed_read.lua into the Collection feed and the NDJSON log.
-- usage: lua5.1 guildherald_test.lua <client dir> <compiled gh.lua (reflux.prn + guildherald.prn)> <scratch dir>
assert(_VERSION == "Lua 5.1", "must run under Lua 5.1")
local client, gh_path, dir = assert(arg[1]), assert(arg[2]), assert(arg[3])

local passed, failed = 0, 0
local function eq(got, want, what)
    if got == want then passed = passed + 1
    else failed = failed + 1; print("FAIL: " .. what .. " (got " .. tostring(got) .. ", want " .. tostring(want) .. ")") end
end
local function has(s, sub, what) eq(type(s) == "string" and s:find(sub, 1, true) ~= nil, true, what .. " [" .. tostring(s) .. "]") end

local function toc_files(d, name)
    local files = {}
    for line in io.lines(d .. "/" .. name .. ".toc") do
        if line ~= "" and not line:match("^#") then files[#files + 1] = line end
    end
    return files
end

local function session(w, saved)
    local env = { print = function(...)
        local t = {}
        for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
        w.out[#w.out + 1] = table.concat(t, " ")
    end }
    for _, k in ipairs({ "pairs", "ipairs", "type", "tostring", "tonumber", "select", "string", "table", "math",
                         "next", "error", "assert", "pcall", "unpack", "setmetatable", "getmetatable" }) do env[k] = _G[k] end
    env._G = env
    env.time = function() return w.now end
    env.UnitName = function() return w.name end
    env.GetRealmName = function() return "Forever" end
    env.IsInGuild = function() return w.guild ~= nil end
    env.GetGuildInfo = function() if w.guild then return w.guild, "Member", 3, nil end end
    env.GetGuildRosterMOTD = function() return w.motd end
    env.GetGuildInfoText = function() return w.info end
    env.C_GuildInfo = { GuildRoster = function() w.asked = (w.asked or 0) + 1 end }
    local frames = {}
    env.CreateFrame = function()
        local f = { events = {}, scripts = {} }
        function f:RegisterEvent(e) self.events[e] = true end
        function f:SetScript(k, fn) self.scripts[k] = fn end
        frames[#frames + 1] = f
        return f
    end
    env.SlashCmdList = {}
    env.GuildHeraldDB = saved
    local s = { env = env }
    function s.load(addon)
        local d = client .. "/AddOns/" .. addon
        for _, file in ipairs(toc_files(d, addon)) do
            local chunk = assert(loadfile(d .. "/" .. file)); setfenv(chunk, env); chunk(addon, {})
        end
    end
    function s.fire(event, ...) for _, f in ipairs(frames) do if f.events[event] and f.scripts.OnEvent then f.scripts.OnEvent(f, event, ...) end end end
    function s.herald(msg) w.out = {}; env.SlashCmdList.GUILDHERALD(msg or ""); return table.concat(w.out, "\n") end
    for _, a in ipairs({ "RefluxCore", "GuildHerald" }) do s.load(a) end
    for _, a in ipairs({ "RefluxCore", "GuildHerald" }) do s.fire("ADDON_LOADED", a) end
    return s
end

local saved = {}
local w = { out = {}, name = "Ingrid", guild = "Kaldorei Vanguard", now = 1763500000,
            motd = "", info = "Rules: be kind. Voice: discord.gg/AbC-123 and https://discord.com/invite/AbC-123" }
local s = session(w, saved)
s.fire("PLAYER_LOGIN")   -- no C_Timer here: scans at once
local key = "Kaldorei Vanguard-Forever"
local g = saved.guilds[key]
eq(g ~= nil, true, "the guild is recorded on login")
eq(g.realm, "Forever", "with the player's realm when GetGuildInfo gives none")
eq(g.motd, nil, "an empty MOTD (roster not loaded yet) is not a change")
eq(#g.history, 1, "the info text is logged")
eq(g.invites["AbC-123"], w.now, "its invite is found, case kept, once for both link forms")
local n = 0; for _ in pairs(g.invites) do n = n + 1 end
eq(n, 1, "the same code in two link forms is one invite")
eq(w.asked, 1, "the client asks the server for the roster")

w.now = w.now + 10
w.motd = "Raid Friday! Discord: DISCORD.GG/RaidNight -- bring flasks"
s.fire("GUILD_MOTD", w.motd)
eq(g.motd, w.motd, "a MOTD is recorded")
eq(#g.history, 2, "and logged")
eq(g.history[2].kind, 1, "as a MOTD")
eq(g.history[2].who, "Ingrid-Forever", "with who saw it")
eq(g.invites["RaidNight"] ~= nil, true, "a link written in capitals is still found, its code case kept")
s.fire("GUILD_MOTD", w.motd)
eq(#g.history, 2, "the same MOTD again is not a new entry")
s.fire("GUILD_MOTD", "Bad link: discord.gg/x and discord.gg/" .. string.rep("a", 40))
eq(g.invites["x"], nil, "a 1-character code is not an invite")
local long = false; for code in pairs(g.invites) do if #code > 32 then long = true end end
eq(long, false, "a 40-character code is not an invite")
eq(#g.history, 3, "but the MOTD itself is still logged")

for i = 1, 40 do s.fire("GUILD_MOTD", "motd " .. i) end
eq(#g.history, 30, "a guild keeps gh-history-max entries")
eq(g.history[30].text, "motd 40", "the newest last")
eq(saved.nextSeq, 44, "seqs keep counting past the cap (the log has them all)")

-- REFLUX saw each change: 3080, kind, length, invites
local rows = s.env.RefluxHost and s.env.RefluxHost.log or nil
local out = s.herald("")
has(out, "<Kaldorei Vanguard>|r (Forever)", "/herald lists the guild (name coloured)")
has(out, "discord.gg/AbC-123", "with its Discord invites")
has(out, "discord.gg/RaidNight", "all of them")
has(out, "/ascend", "and points at SHANKPIT's /ascend")
has(s.herald("why"), "every guild call is available", "/herald why")

-- a second character in another guild, same account file
local w2 = { out = {}, name = "Bjorn", guild = "Ash Ravens", now = 1763600000, motd = "discord.gg/ravens", info = "" }
local s2 = session(w2, saved)
s2.fire("PLAYER_LOGIN")
eq(saved.guilds["Ash Ravens-Forever"].invites["ravens"] ~= nil, true, "a second guild on the same account")
eq(saved.guilds[key].chars["Ingrid-Forever"], true, "and the first guild still remembers who saw it")
-- a character with no guild logs nothing
local w3 = { out = {}, name = "Loner", guild = nil, now = 1763700000 }
local s3 = session(w3, saved)
s3.fire("PLAYER_LOGIN")
local ng = 0; for _ in pairs(saved.guilds) do ng = ng + 1 end
eq(ng, 2, "a guildless character adds nothing")

-- ---- out of the game: the feed and the log stream ------------------------------------------------------------
local function ser(v, ind)
    if type(v) == "table" then
        local keys = {}
        for k in pairs(v) do keys[#keys + 1] = k end
        table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
        local o = { "{\n" }
        for _, k in ipairs(keys) do
            local kk = type(k) == "number" and ("[" .. k .. "]") or ("[" .. string.format("%q", k) .. "]")
            o[#o + 1] = ind .. "  " .. kk .. " = " .. ser(v[k], ind .. "  ") .. ",\n"
        end
        o[#o + 1] = ind .. "}"
        return table.concat(o)
    elseif type(v) == "string" then return string.format("%q", v)
    else return tostring(v) end
end
local acct = dir .. "/GuildHerald.lua"
local f = assert(io.open(acct, "wb")); f:write("GuildHeraldDB = ", ser(saved, ""), "\n"); f:close()
local feed_out, log_out = dir .. "/guild_feed.txt", dir .. "/guilds.ndjson"
os.remove(log_out); os.remove(log_out .. ".state")
arg = { gh_path, "GuildHeraldDB", feed_out, log_out, acct }
dofile("runtime/wow/guild_feed_read.lua")

local names, rows3 = {}, {}
for line in io.lines(feed_out) do
    local id, text = line:match("^name (%d+) (.*)$")
    if id then names[tonumber(id)] = text end
    local t, a, b, c = line:match("^row (%d+) (%d+) (%d+) (%d+)$")
    if t then rows3[#rows3 + 1] = { tonumber(t), tonumber(a), tonumber(b), tonumber(c) } end
end
local invites, guildrows = {}, 0
for _, r in ipairs(rows3) do
    if r[1] == 3082 then guildrows = guildrows + 1 end
    if r[1] == 3083 then invites[names[r[2]] .. "|" .. names[r[3]]] = true end
end
eq(guildrows, 2, "the feed has both guilds")
eq(invites["Kaldorei Vanguard|AbC-123"], true, "and each guild's invites")
eq(invites["Ash Ravens|ravens"], true, "for every guild")
eq(rows3[#rows3][1], 3084, "the summary row last")
eq(rows3[#rows3][2], 2, "two guilds")

local lines = 0
for line in io.lines(log_out) do
    lines = lines + 1
    if lines == 1 then
        has(line, '"type":"guild_text"', "log lines are typed")
        has(line, '"guild":"Kaldorei Vanguard"', "with the guild")
        -- the first info entry (seq 1) was trimmed by the 30-entry cap before this read: the log gets what the
        -- game still holds, which is why the reader runs after every session
        has(line, '"kind":"motd"', "and the kind")
        has(line, '"seq":14', "starting at the oldest entry the game still held")
    end
end
eq(lines, 31, "the log has every entry still held (30 + 1 for the second guild)")
dofile("runtime/wow/guild_feed_read.lua")
local again = 0
for _ in io.lines(log_out) do again = again + 1 end
eq(again, 31, "a second run appends nothing already logged")

print(string.format("guildherald: %d passed, %d failed", passed, failed))
os.exit(failed == 0 and 0 or 1)
