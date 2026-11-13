-- runtime/wow/rune_riddle.lua -- Rune Riddle's host glue (spec addon 56, scavenger hunt). Rules:
-- stdlib/collection/runeriddle.prn.
-- /riddle mark <riddle>   this spot becomes the next stop, with your riddle for it
-- /riddle list | clear    the stops so far / forget them
-- /riddle start           read the first riddle to the group; anyone arriving checks it off
-- /riddle stop            end the hunt
local ADDON, VAR = "@ADDON@", "@VAR@"
local _, ns = ...
local K = ns.kit
local G = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/runeriddle"]
if not G then print(ADDON .. ": RefluxCore's collection/runeriddle module is missing.") return end
K.saved(VAR, G.riddleInit)

local ticker = nil

local function read_current()
    local s = G.riddleCurrent(K.db)
    if s then K.say(K.f("RIDDLE", K.L("NAME"), K.db.run.stop, #K.db.stops, s.riddle)) end
end

local function tick()
    for _, m in ipairs(K.group()) do
        local map, x, y = K.pos(m.unit)
        if map then
            local r = G.riddleArrive_(K.db, m.name, map, x, y, K.now())
            if r == 1 then K.say(K.f("REACHED", m.name)); read_current(); return end
            if r == 2 then
                K.cancel(ticker); ticker = nil
                K.notice(K.f("DONE", K.L("NAME"), m.name, K.db.run.secs))
                K.db.run = nil
                return
            end
        end
    end
end

SLASH_RUNERIDDLE1 = "/riddle"
SlashCmdList["RUNERIDDLE"] = function(msg)
    if not K.db then return end
    local cmd, arg = (msg or ""):match("^%s*(%S*)%s*(.-)%s*$")
    if cmd == "mark" then
        local map, x, y = K.pos("player")
        if not map then print(K.f("NOPOS", K.L("NAME"))) return end
        if arg == "" then print(K.L("HELP")) return end
        print(K.f("MARKED", K.L("NAME"), G.riddleMark_(K.db, map, x, y, arg)))
    elseif cmd == "list" then
        for i, s in ipairs(K.db.stops) do print(string.format("  %d. %s", i, s.riddle)) end
    elseif cmd == "clear" then
        G.riddleClear_(K.db); print(K.f("CLEARED", K.L("NAME")))
    elseif cmd == "start" then
        if G.riddleStart_(K.db, K.now()) == 0 then print(K.f("EMPTY", K.L("NAME"))) return end
        read_current()
        ticker = K.every(1, tick)
    elseif cmd == "stop" then
        K.cancel(ticker); ticker = nil; K.db.run = nil
    else
        print(K.L("HELP"))
    end
end
