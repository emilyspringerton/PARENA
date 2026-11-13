-- runtime/wow/mjolnir_pass.lua -- Mjölnir Pass's host glue (spec addon 55, tag). Rules: stdlib/collection/mjolnir.prn.
-- /tag start [seconds]  the hammer goes to a random player; touching range passes it (default 180 s)
-- /tag stop             end now and show who held it longest
local ADDON, VAR = "@ADDON@", "@VAR@"
local _, ns = ...
local K = ns.kit
local G = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/mjolnir"]
if not G then print(ADDON .. ": RefluxCore's collection/mjolnir module is missing.") return end
K.saved(VAR, G.tagInit)

local ticker, ends = nil, nil

local function finish()
    K.cancel(ticker); ticker = nil
    local top = G.tagFinish_(K.db, K.now())
    K.say(K.f("OVER", K.L("NAME")))
    for i, row in ipairs(top) do K.say(K.f("ROW", i, row[1], row[2])) end
    if top[1] then K.notice(K.f("LONGEST", top[1][1], top[1][2])) end
end

local function tick()
    if K.now() >= ends then finish() return end
    local it = G.tagIt(K.db)
    local u = it and K.unit_of(it)
    if not u then return end
    local _, ix, iy = K.pos(u)
    if not ix then return end
    for _, m in ipairs(K.group()) do
        if m.name ~= it then
            local _, x, y = K.pos(m.unit)
            if x and G.tagCanPass(K.db, m.name, G.gDist(ix, iy, x, y), K.now()) == 1 then
                G.tagPass_(K.db, m.name, K.now())
                K.say(K.f("PASSED", it, m.name))
                return
            end
        end
    end
end

SLASH_MJOLNIRPASS1 = "/tag"
SlashCmdList["MJOLNIRPASS"] = function(msg)
    if not K.db then return end
    local cmd, arg = (msg or ""):match("^%s*(%S*)%s*(.-)%s*$")
    if cmd == "start" then
        local names = {}
        for _, m in ipairs(K.group()) do names[#names + 1] = m.name end
        if #names < 2 then print(K.f("SOLO", K.L("NAME"))) return end
        local first = G.tagStart_(K.db, names, K.now() + #names, K.now())
        ends = K.now() + (tonumber(arg) or 180)
        K.say(K.f("START", K.L("NAME"), first))
        ticker = K.every(0.5, tick)
    elseif cmd == "stop" then
        if K.db.round then finish() end
    else
        print(K.L("HELP"))
    end
end
