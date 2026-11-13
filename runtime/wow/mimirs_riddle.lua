-- runtime/wow/mimirs_riddle.lua -- Mímir's Riddle's host glue (spec addon 59, trivia night). Rules: stdlib/collection/mimir.prn.
-- /mimir start [questions]  ask in group chat (or guild: /mimir start 10 guild); 30 s per question
-- /mimir stop | scores      end the night / the all-time board
-- Answers are matched against the catalogue's A<n> list (lower case, punctuation ignored) in memory; nobody's
-- chat line is stored (D-8).
local ADDON, VAR = "@ADDON@", "@VAR@"
local _, ns = ...
local K = ns.kit
local G = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/mimir"]
if not G then print(ADDON .. ": RefluxCore's collection/mimir module is missing.") return end
K.saved(VAR, G.mimirInit)

local guild, timer = false, nil

local function say(text)
    if guild and SendChatMessage then SendChatMessage(text, "GUILD") else K.say(text) end
end

local function norm(s) return ((s or ""):lower():gsub("[%p]", ""):gsub("%s+", " "):match("^%s*(.-)%s*$")) end

local function right(q, text)
    local t = norm(text)
    for a in K.L("A" .. q):gmatch("[^|]+") do if t == norm(a) then return true end end
    return false
end

local ask

local function finish()
    K.cancel(timer); timer = nil
    local top = G.mimirFinish_(K.db)
    say(K.f("OVER", K.L("NAME")))
    for i, row in ipairs(top) do if i <= 5 then say(K.f("ROW", i, row[1], row[2])) end end
end

ask = function()
    local q = G.mimirAsk_(K.db, K.now())
    if q == 0 then finish() return end
    say(K.f("ASK", K.db.night.at, #K.db.night.order, K.L("Q" .. q)))
    timer = K.after(30, function()
        if G.mimirCurrent(K.db) == q then
            say(K.f("NOBODY", (K.L("A" .. q):match("^[^|]+"))))
            G.mimirSkip_(K.db)
        end
        K.after(3, ask)
    end)
end

K.on_chat(function(text, sender, event)
    if not K.db then return end
    if guild ~= (event == "CHAT_MSG_GUILD") then return end
    local q = G.mimirCurrent(K.db)
    if q > 0 and sender and right(q, text) then
        local s = G.mimirAnswer_(K.db, sender, K.now())
        if s > 0 then say(K.f("RIGHT", sender, s)) end
    end
end)

SLASH_MIMIRSRIDDLE1 = "/mimir"
SlashCmdList["MIMIRSRIDDLE"] = function(msg)
    if not K.db then return end
    local cmd, n, where = (msg or ""):match("^%s*(%S*)%s*(%d*)%s*(%a*)")
    if cmd == "start" then
        guild = (where == "guild")
        local total = tonumber(K.L("Q_COUNT")) or 0
        local count = G.mimirStart_(K.db, total, tonumber(n) or 10, K.now())
        say(K.f("START", K.L("NAME"), count))
        K.after(3, ask)
    elseif cmd == "stop" then
        if K.db.night then finish() end
    elseif cmd == "scores" then
        for i, row in ipairs(G.gBoardTop(K.db.alltime, 10)) do print(K.f("ROW", i, row[1], row[2])) end
    else
        print(K.L("HELP"))
    end
end
