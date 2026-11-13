-- runtime/wow/ecowar.lua -- EcoWar's host glue. Every rule is PARENA (stdlib/collection/ecowar.prn); this file counts
-- kills and loot, carries scores, and talks. Plumbing: kit.lua; words: locale.lua.
--
-- CAPTAIN (whoever starts the match):
--   /ecowar new <mode> <game> <minutes> <Team:H|A> <Team:H|A> [<Team:H|A>] [at <name>]
--       mode  hvh | ava | avh | tri        game  cull | harvest | ecowar
--       e.g.  /ecowar new hvh ecowar 10 Wolves:H Bears:H
--             /ecowar new avh cull 15 Lions:A Wolves:H at Friday
--       Teams of your faction play here, one raid subgroup each in the order listed (a plain party is the
--       first local team). Teams of the other faction run their own captain with the SAME command (same teams,
--       same order, same "at" name) and their score comes over the carrier.
--   /ecowar status | stop | code | enter <code> | table
-- PLAYERS: nothing to type. With EcoWar installed, your own critter kills and loot are reported to your captain;
-- without it, the captain still counts the kills it can see.
--
-- How things are counted (client calls; none probed on Forever yet, each feature-tested):
--   critter: a unit whose UnitCreatureType is the catalogue's CRITTER_TYPE, remembered by UnitGUID when seen as
--     target, mouseover or nameplate; scored on the combat log's PARTY_KILL with that GUID as the victim
--   loot:    your own CHAT_MSG_LOOT lines; value = EcoWarPriceSource(itemID, count) when an auction-house addon
--     provides one, else GetItemInfo's vendor price x count
--   reports: C_ChatInfo.SendAddonMessage, prefix ECOWAR, to RAID/PARTY
--   carrier 2: a temporary channel "EcoWar<name>" (whether it crosses factions on Forever is NOT confirmed);
--   carrier 4: the signed match code, typed in by the other captain -- always works.
local ADDON, VAR = "@ADDON@", "@VAR@"
local _, ns = ...
local K = ns.kit
local E = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/ecowar"]
if not E then print(ADDON .. ": RefluxCore's collection/ecowar module is missing.") return end
K.saved(VAR, E.ewInit)

local PREFIX = "ECOWAR"
local MODES = { hvh = 1, ava = 2, avh = 3, tri = 4 }
local GAMES = { cull = 1, harvest = 2, ecowar = 3 }
local FACTION = { H = "Horde", A = "Alliance" }

local critters = {}        -- GUID -> name, for units seen as critters
local roster = {}          -- player name -> team index (captain only)
local captain = false
local active = nil         -- a match id this player reports for (players), or the captain's own
local chan = nil
local relay = nil

if C_ChatInfo and C_ChatInfo.RegisterAddonMessagePrefix then C_ChatInfo.RegisterAddonMessagePrefix(PREFIX) end

local function send(msg, kind, target)
    if C_ChatInfo and C_ChatInfo.SendAddonMessage then C_ChatInfo.SendAddonMessage(PREFIX, msg, kind, target) end
end
local function group_kind() return K.channel() end

local function my_faction() return (UnitFactionGroup and UnitFactionGroup("player")) or "Horde" end

-- local teams in listed order -> raid subgroup k is the k-th local team
local function build_roster()
    roster = {}
    local m = K.db.match
    if not m then return end
    local locals = {}
    for i, t in ipairs(m.teams) do if t.remote == 0 then locals[#locals + 1] = i end end
    if IsInRaid and IsInRaid() and GetRaidRosterInfo then
        for i = 1, (GetNumGroupMembers and GetNumGroupMembers() or 0) do
            local name, _, sub = GetRaidRosterInfo(i)
            if name and locals[sub] then roster[K.short(name)] = locals[sub] end
        end
    else
        for _, mem in ipairs(K.group()) do roster[mem.name] = locals[1] end
    end
end

local function team_name(i) return K.db.match.teams[i].name end

local function remember(unit)
    if not (UnitExists and UnitExists(unit)) then return end
    if UnitCreatureType and UnitCreatureType(unit) == K.L("CRITTER_TYPE") and UnitGUID then
        critters[UnitGUID(unit)] = UnitName(unit)
    end
end
K.on("PLAYER_TARGET_CHANGED", function() remember("target") end)
K.on("UPDATE_MOUSEOVER_UNIT", function() remember("mouseover") end)
K.on("NAME_PLATE_UNIT_ADDED", function(u) remember(u) end)

-- the captain credits a kill
local function credit_kill(player, cid, kind)
    local team = roster[K.short(player)]
    if not team then return end
    local pts = E.ewKill_(K.db, team, K.short(player), cid, kind, K.now())
    if pts > 0 then print(K.f("KILL", team_name(team), player, kind, pts)) end
end

local function credit_eco(player, copper)
    local team = roster[K.short(player)]
    if team then E.ewEco_(K.db, team, copper, K.now()) end
end

K.on("COMBAT_LOG_EVENT_UNFILTERED", function()
    if not (active and CombatLogGetCurrentEventInfo) then return end
    local _, sub, _, srcGUID, srcName, _, _, dstGUID = CombatLogGetCurrentEventInfo()
    if sub ~= "PARTY_KILL" or not critters[dstGUID] then return end
    if captain then credit_kill(srcName, dstGUID, critters[dstGUID]) end
    if UnitGUID and srcGUID == UnitGUID("player") and not captain then
        send("K|" .. active .. "|" .. dstGUID .. "|" .. critters[dstGUID], group_kind())
    end
end)

local function price(id, count)
    if type(EcoWarPriceSource) == "function" then
        local p = EcoWarPriceSource(id, count)
        if type(p) == "number" then return p end
    end
    local sell = GetItemInfo and select(11, GetItemInfo(id))
    return (sell or 0) * count
end

K.on("CHAT_MSG_LOOT", function(text)
    if not active or type(text) ~= "string" then return end
    if not (LOOT_ITEM_SELF and text:find((LOOT_ITEM_SELF:gsub("%%s.*", "")), 1, true) == 1) then return end
    local id = tonumber(text:match("item:(%d+)"))
    if not id then return end
    local count = tonumber(text:match("x(%d+)%.?$")) or 1
    local copper = price(id, count)
    if copper <= 0 then return end
    if captain then credit_eco(UnitName("player"), copper) else send("E|" .. active .. "|" .. copper, group_kind()) end
end)

K.on("CHAT_MSG_ADDON", function(prefix, msg, kind, sender)
    if prefix ~= PREFIX or type(msg) ~= "string" then return end
    local parts = {}
    for p in (msg .. "|"):gmatch("([^|]*)|") do parts[#parts + 1] = p end
    local who = K.short(sender)
    if parts[1] == "S" then active = tonumber(parts[2])            -- a captain started: report to them
    elseif parts[1] == "X" then if not captain then active = nil end
    elseif captain and K.db.match and tonumber(parts[2]) == active then
        if parts[1] == "K" then credit_kill(who, parts[3], parts[4])
        elseif parts[1] == "E" then credit_eco(who, tonumber(parts[3]) or 0) end
    end
    if captain and K.db.match and parts[1] == "R" and kind == "CHANNEL" then
        local t = E.ewTeamOf(K.db, parts[2])
        if t > 0 then E.ewRemote_(K.db, t, tonumber(parts[3]) or 0, tonumber(parts[4]) or 0, tonumber(parts[5]) or 0, 2) end
    end
end)

local function relay_scores()
    if not (chan and GetChannelName) then return end
    local idx = GetChannelName(chan)
    if not idx or idx == 0 then return end
    for _, t in ipairs(K.db.match.teams) do
        if t.remote == 0 then send(table.concat({ "R", t.name, t.score, t.kills, t.eco }, "|"), "CHANNEL", idx) end
    end
end

local function codes()
    local m = K.db.match
    for i, t in ipairs(m.teams) do
        if t.remote == 0 then print(K.f("CODE", t.name, E.ewCode(m.id, i, t.score, t.kills, t.eco))) end
    end
end

local function status()
    for i, row in ipairs(E.ewStandings(K.db)) do
        local t = K.db.match.teams[row[1]]
        K.say(K.f("STANDING", i, row[2], row[3], t.kills, math.floor(t.eco / 10000)))
    end
end

local function finish()
    K.cancel(relay); relay = nil
    relay_scores()
    codes()
    local st = E.ewFinish_(K.db)
    K.say(K.f("OVER", K.L("NAME")))
    for i, row in ipairs(st) do K.say(K.f("FINAL", i, row[2], row[3], math.floor(K.db.ratings[row[2]] or 1500))) end
    if st[1] and st[2] and st[1][3] == st[2][3] then K.notice(K.f("TIE", st[1][3]))
    elseif st[1] then K.notice(K.f("WINNER", K.L("NAME"), st[1][2])) end
    send("X|" .. tostring(active), group_kind())
    captain, active = false, nil
    if chan and LeaveChannelByName then LeaveChannelByName(chan) end
    chan = nil
end

SLASH_ECOWAR1 = "/ecowar"
SlashCmdList["ECOWAR"] = function(msg)
    if not K.db then return end
    local words = {}
    for w in (msg or ""):gmatch("%S+") do words[#words + 1] = w end
    local cmd = (words[1] or ""):lower()
    if cmd == "new" then
        local mode, game, mins = MODES[(words[2] or ""):lower()], GAMES[(words[3] or ""):lower()], tonumber(words[4])
        local teams, at = {}, nil
        local i = 5
        while words[i] do
            if words[i]:lower() == "at" then at = words[i + 1]; break end
            local name, side = words[i]:match("^([^:]+):([HhAa])$")
            if not name then print(K.L("HELP")) return end
            teams[#teams + 1] = { name, FACTION[side:upper()] }
            i = i + 1
        end
        if not (mode and game and mins) then print(K.L("HELP")) return end
        local id = E.ewNew_(K.db, mode, game, teams, my_faction(), mins * 60, K.now())
        if id == 0 then print(K.f("BADTEAMS", K.L("MODE_" .. mode))) return end
        captain, active = true, id
        build_roster()
        K.say(K.f("START", K.L("NAME"), K.L("MODE_" .. mode), K.L("GAME_" .. game), mins))
        for j, t in ipairs(K.db.match.teams) do K.say(K.f("TEAM", j, t.name, t.faction, t.remote == 1 and K.L("REMOTE") or "")) end
        send("S|" .. id, group_kind())
        for _, t in ipairs(K.db.match.teams) do
            if t.remote == 1 then
                chan = "EcoWar" .. (at or "Match")
                if JoinTemporaryChannel then JoinTemporaryChannel(chan) end
                break
            end
        end
        relay = K.every(15, relay_scores)
        K.after(mins * 60, function() if K.db.match then finish() end end)
    elseif cmd == "status" then
        if K.db.match then status() else print(K.f("NOMATCH", K.L("NAME"))) end
    elseif cmd == "stop" then
        if K.db.match then finish() end
    elseif cmd == "code" then
        if K.db.match then codes() else print(K.f("NOMATCH", K.L("NAME"))) end
    elseif cmd == "enter" then
        local p = E.ewParseCode(words[2] or "")
        if not p then print(K.L("BADCODE")) return end
        if not K.db.match or E.ewRemote_(K.db, p[2], p[3], p[4], p[5], 4) == 0 then print(K.L("CODEREFUSED")) return end
        print(K.f("CODEOK", team_name(p[2]), p[3]))
    elseif cmd == "table" then
        local rows = {}
        for name, lg in pairs(K.db.league) do rows[#rows + 1] = { name, lg } end
        table.sort(rows, function(a, b) if a[2].points ~= b[2].points then return a[2].points > b[2].points end return a[1] < b[1] end)
        print(K.f("TABLE", K.L("NAME")))
        for i, r in ipairs(rows) do
            local lg = r[2]
            print(K.f("TABLEROW", i, r[1], lg.played, lg.won, lg.drawn, lg.lost, lg.points, math.floor(K.db.ratings[r[1]] or 1500)))
        end
    else
        print(K.L("HELP"))
    end
end

K.on("GROUP_ROSTER_UPDATE", function() if captain then build_roster() end end)
