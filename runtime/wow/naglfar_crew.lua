-- runtime/wow/naglfar_crew.lua -- Naglfar Crew's host glue (spec addon 71, sardines). Rules:
-- stdlib/collection/naglfar.prn.
-- /sardines start <hider> [seconds]  the hider gets 60 s to hide, then everyone else seeks (default 300 s)
-- /sardines stop                     end now
local ADDON, VAR = "@ADDON@", "@VAR@"
local _, ns = ...
local K = ns.kit
local G = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/naglfar"]
if not G then print(ADDON .. ": RefluxCore's collection/naglfar module is missing.") return end
K.saved(VAR, G.sardInit)

local ticker = nil

local function finish()
    K.cancel(ticker); ticker = nil
    local r = G.sardFinish_(K.db, K.now())
    if r[1] then K.say(K.f("LAST", K.L("NAME"), r[1], r[2], r[3])) else K.say(K.f("ALLIN", K.L("NAME"), r[2], r[3])) end
end

local function tick()
    local r = K.db.round
    if not r then return end
    local hu = K.unit_of(r.hider)
    local _, hx, hy = K.pos(hu)
    if hx then
        for _, m in ipairs(K.group()) do
            if m.name ~= r.hider then
                local _, x, y = K.pos(m.unit)
                if x then
                    local order = G.sardCheck_(K.db, m.name, G.gDist(hx, hy, x, y), K.now())
                    if order > 0 then K.say(K.f("SQUEEZED", m.name, order)) end
                end
            end
        end
    end
    if G.sardOver(K.db, K.now()) == 1 then finish() end
end

SLASH_NAGLFARCREW1 = "/sardines"
SlashCmdList["NAGLFARCREW"] = function(msg)
    if not K.db then return end
    local cmd, hider, secs = (msg or ""):match("^%s*(%S*)%s*(%S*)%s*(%d*)")
    if cmd == "start" then
        if hider == "" or not K.unit_of(hider) then print(K.L("HELP")) return end
        hider = K.short(hider)
        local seekers = {}
        for _, m in ipairs(K.group()) do if m.name ~= hider then seekers[#seekers + 1] = m.name end end
        if #seekers < 1 then print(K.f("SOLO", K.L("NAME"))) return end
        K.say(K.f("HIDE", K.L("NAME"), hider, 60))
        K.after(60, function()
            G.sardStart_(K.db, hider, seekers, tonumber(secs) or 300, K.now())
            K.say(K.f("SEEK", K.L("NAME"), hider))
            ticker = K.every(1, tick)
        end)
    elseif cmd == "stop" then
        if K.db.round then finish() end
    else
        print(K.L("HELP"))
    end
end
