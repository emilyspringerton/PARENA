-- runtime/wow/freyas_runway.lua -- Freya's Runway's host glue (spec addon 61, fashion show). Rules: stdlib/collection/freya.prn.
-- /runway start [names...]   queue the named models (default: the whole group); 30 s walks, judges type 1-10
-- /runway stop               end the show now
local ADDON, VAR = "@ADDON@", "@VAR@"
local _, ns = ...
local K = ns.kit
local G = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/freya"]
if not G then print(ADDON .. ": RefluxCore's collection/freya module is missing.") return end
K.saved(VAR, G.freyaInit)

local walk

local function finish()
    local top = G.freyaFinish_(K.db)
    K.say(K.f("OVER", K.L("NAME")))
    for i, row in ipairs(top) do K.say(K.f("ROW", i, row[1], row[2] / 10)) end
    if top[1] then K.notice(K.f("CROWN", top[1][1])) end
end

walk = function()
    local m = G.freyaNext_(K.db)
    if not m then finish() return end
    K.say(K.f("WALK", m))
    K.after(30, function()
        local r = G.freyaClose_(K.db)
        K.say(K.f("SCORED", m, r[1] / 10, r[2]))
        K.after(3, walk)
    end)
end

K.on_chat(function(text, sender)
    if not (K.db and K.db.show) then return end
    local n = tonumber((text or ""):match("^%s*(%d+)%s*$"))
    if n then G.freyaVote_(K.db, sender, n) end
end)

SLASH_FREYASRUNWAY1 = "/runway"
SlashCmdList["FREYASRUNWAY"] = function(msg)
    if not K.db then return end
    local cmd, rest = (msg or ""):match("^%s*(%S*)%s*(.-)%s*$")
    if cmd == "start" then
        local q = {}
        for n in rest:gmatch("%S+") do q[#q + 1] = K.short(n) end
        if #q == 0 then for _, m in ipairs(K.group()) do q[#q + 1] = m.name end end
        G.freyaStart_(K.db, q)
        K.say(K.f("START", K.L("NAME"), #q))
        K.after(3, walk)
    elseif cmd == "stop" then
        if K.db.show then finish() end
    else
        print(K.L("HELP"))
    end
end
