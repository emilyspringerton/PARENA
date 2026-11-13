-- runtime/wow/odinn_says.lua -- Óðinn Says' host glue (spec addon 57, emote Simon Says). Rules: stdlib/collection/odinn.prn.
-- The host calls; everyone else in the group plays (they need no addon -- they only emote).
-- /odinn start   play until one is left (a call every 8 s, 6 s to answer)
-- /odinn stop    end now
local ADDON, VAR = "@ADDON@", "@VAR@"
local _, ns = ...
local K = ns.kit
local G = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/odinn"]
if not G then print(ADDON .. ": RefluxCore's collection/odinn module is missing.") return end
K.saved(VAR, G.odinnInit)

local loop, seed = nil, 0

K.on_emote(function(tok, sender)
    if not (K.db and K.db.round) or sender == UnitName("player") then return end
    for i, t in ipairs(K.EMOTES) do if t == tok then G.odinnAnswer_(K.db, sender, i) return end end
end)

local function call()
    if not (K.db and K.db.round) then K.cancel(loop); loop = nil; return end
    seed = seed + 1
    local c = G.odinnCall_(K.db, K.now() * 7 + seed)
    local cmd = K.emote_cmd(K.EMOTES[c[1]])
    if c[2] == 1 then K.say(K.f("REAL", K.L("NAME"), cmd)) else K.say(K.f("TRAP", cmd)) end
    K.after(6, function()
        if not K.db.round then return end
        for _, o in ipairs(G.odinnClose_(K.db)) do K.say(K.f("OUT_" .. o[2], o[1])) end
        local w = G.odinnWinner(K.db)
        if w then K.notice(K.f("WINNER", K.L("NAME"), w)) end
        if not K.db.round then
            K.cancel(loop); loop = nil
            if not w then K.say(K.f("NOBODY", K.L("NAME"))) end
        end
    end)
end

SLASH_ODINNSAYS1 = "/odinn"
SlashCmdList["ODINNSAYS"] = function(msg)
    if not K.db then return end
    local cmd = (msg or ""):match("^%s*(%S*)")
    if cmd == "start" then
        local names = {}
        for _, m in ipairs(K.group()) do if m.unit ~= "player" then names[#names + 1] = m.name end end
        if #names < 2 then print(K.f("SOLO", K.L("NAME"))) return end
        G.odinnStart_(K.db, names)
        K.say(K.f("START", K.L("NAME"), #names))
        loop = K.every(8, call)
    elseif cmd == "stop" then
        K.cancel(loop); loop = nil; G.odinnStop_(K.db)
    else
        print(K.L("HELP"))
    end
end
