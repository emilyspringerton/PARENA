-- runtime/wow/alfar_revel.lua -- Álfar Revel's host glue (spec addon 64, dance floor). Rules: stdlib/collection/alfar.prn.
-- /revel start [beats] [seconds per beat]   the host calls an emote on every beat (default 16 beats, 4 s each)
-- /revel stop                               end now and show the streaks
local ADDON, VAR = "@ADDON@", "@VAR@"
local _, ns = ...
local K = ns.kit
local G = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/alfar"]
if not G then print(ADDON .. ": RefluxCore's collection/alfar module is missing.") return end
K.saved(VAR, G.revelInit)

local loop, beats, left = nil, 0, 0

K.on_emote(function(tok, sender)
    if not (K.db and K.db.party) then return end
    for i, t in ipairs(K.EMOTES) do if t == tok then G.revelHit_(K.db, sender, i) return end end
end)

local function finish()
    K.cancel(loop); loop = nil
    local top = G.revelFinish_(K.db)
    K.say(K.f("OVER", K.L("NAME")))
    for i, row in ipairs(top) do if i <= 5 then K.say(K.f("ROW", i, row[1], row[2])) end end
end

local function beat()
    if K.db.party.beat > 0 then G.revelClose_(K.db) end
    if left <= 0 then finish() return end
    left = left - 1
    local e = G.revelBeat_(K.db, K.now() * 13 + left)
    K.say(K.f("BEAT", K.db.party.beat, K.emote_cmd(K.EMOTES[e])))
end

SLASH_ALFARREVEL1 = "/revel"
SlashCmdList["ALFARREVEL"] = function(msg)
    if not K.db then return end
    local cmd, n, secs = (msg or ""):match("^%s*(%S*)%s*(%d*)%s*(%d*)")
    if cmd == "start" then
        local names = {}
        for _, m in ipairs(K.group()) do names[#names + 1] = m.name end
        G.revelStart_(K.db, names)
        left = tonumber(n) or 16
        K.say(K.f("START", K.L("NAME"), left))
        loop = K.every(tonumber(secs) or 4, beat)
    elseif cmd == "stop" then
        if K.db.party then finish() end
    else
        print(K.L("HELP"))
    end
end
