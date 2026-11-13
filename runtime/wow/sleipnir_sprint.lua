-- runtime/wow/sleipnir_sprint.lua -- Sleipnir Sprint's host glue (spec addon 60, race marshal). Rules: stdlib/collection/sleipnir.prn.
-- /sprint start | finish     set the start line / finish line where you stand (setting a new start clears the board)
-- /sprint go [laps]          3-2-1 in group chat, then time everyone in the group
-- /sprint board | stop       the course's best times / end the race
local ADDON, VAR = "@ADDON@", "@VAR@"
local _, ns = ...
local K = ns.kit
local G = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/sleipnir"]
if not G then print(ADDON .. ": RefluxCore's collection/sleipnir module is missing.") return end
K.saved(VAR, G.raceInit)

local ticker = nil
local function tenths() return math.floor(K.clock() * 10) end
local function secs(t) return string.format("%.1f", t / 10) end

local function finish()
    K.cancel(ticker); ticker = nil
    local res = G.raceFinish_(K.db)
    K.say(K.f("RESULTS", K.L("NAME")))
    for i, row in ipairs(res) do K.say(K.f("ROW", i, row[1], secs(row[2]))) end
end

local function tick()
    for _, m in ipairs(K.group()) do
        local map, x, y = K.pos(m.unit)
        if map then
            local r = G.raceMove_(K.db, m.name, map, x, y, tenths())
            if r >= 2 then K.say(K.f("FINISHED", m.name, r - 1, secs(K.db.race.done[m.name]))) end
        end
    end
    if G.raceOver(K.db) == 1 then finish() end
end

SLASH_SLEIPNIRSPRINT1 = "/sprint"
SlashCmdList["SLEIPNIRSPRINT"] = function(msg)
    if not K.db then return end
    local cmd, n = (msg or ""):match("^%s*(%S*)%s*(%d*)")
    local map, x, y = K.pos("player")
    if cmd == "start" or cmd == "finish" then
        if not map then print(K.f("NOPOS", K.L("NAME"))) return end
        local ok = (cmd == "start") and G.raceSetStart_(K.db, map, x, y) or G.raceSetFinish_(K.db, map, x, y)
        print(ok == 1 and K.f("SET_" .. cmd:upper(), K.L("NAME")) or K.f("NEEDSTART", K.L("NAME")))
    elseif cmd == "go" then
        if G.raceReady(K.db) == 0 then print(K.f("NEEDSTART", K.L("NAME"))) return end
        local names = {}
        for _, m in ipairs(K.group()) do names[#names + 1] = m.name end
        local laps = tonumber(n) or 1
        K.say(K.f("READY", K.L("NAME"), laps))
        K.after(1, function() K.say("3") end)
        K.after(2, function() K.say("2") end)
        K.after(3, function() K.say("1") end)
        K.after(4, function()
            G.raceGo_(K.db, names, laps, tenths())
            K.say(K.L("GO"))
            ticker = K.every(0.2, tick)
        end)
    elseif cmd == "board" then
        for i, row in ipairs(G.gBoardTop(K.db.board, 10)) do print(K.f("ROW", i, row[1], secs(-row[2]))) end
    elseif cmd == "stop" then
        if K.db.race then finish() end
    else
        print(K.L("HELP"))
    end
end
