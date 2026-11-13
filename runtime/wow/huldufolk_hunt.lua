-- runtime/wow/huldufolk_hunt.lua -- Huldufólk Hunt's host glue (spec addon 54, hide and seek). Rules:
-- stdlib/collection/huldufolk.prn; plumbing: kit.lua; words: locale.lua.
-- /hunt start [seconds]  you seek, the group hides (default 300 s, after a 60 s head start)
-- /hunt stop             end the round now
local ADDON, VAR = "@ADDON@", "@VAR@"
local _, ns = ...
local K = ns.kit
local G = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/huldufolk"]
if not G then print(ADDON .. ": RefluxCore's collection/huldufolk module is missing.") return end
K.saved(VAR, G.huntInit)

local ticker, last_hint = nil, nil

local function finish()
    K.cancel(ticker); ticker = nil
    local r = G.huntFinish_(K.db, K.now())
    K.say(K.f("OVER", K.L("NAME"), r[1], r[2]))
    for i, row in ipairs(r[3]) do K.say(K.f("ROW", i, row[1], row[2])) end
end

local function tick()
    local db = K.db
    local _, sx, sy = K.pos("player")
    if not sx then return end
    local nearest = nil
    for _, m in ipairs(K.group()) do
        if m.unit ~= "player" then
            local _, x, y = K.pos(m.unit)
            if x then
                local d = G.gDist(sx, sy, x, y)
                if G.huntCheck_(db, m.name, d, K.now()) == 1 then
                    K.say(K.f("FOUND", K.L("NAME"), m.name, G.huntLeft(db)))
                elseif not (db.round and db.round.found[m.name]) and (not nearest or d < nearest) then
                    nearest = d
                end
            end
        end
    end
    if G.huntOver(db, K.now()) == 1 then finish() return end
    if nearest then
        local h = G.huntHint(nearest)
        if h ~= last_hint then print(K.f("HINT", K.L("NAME"), K.L("HOT_" .. h))); last_hint = h end
    end
end

SLASH_HULDUFOLKHUNT1 = "/hunt"
SlashCmdList["HULDUFOLKHUNT"] = function(msg)
    if not K.db then return end
    local cmd, arg = (msg or ""):match("^%s*(%S*)%s*(.-)%s*$")
    if cmd == "start" then
        local hiders = {}
        for _, m in ipairs(K.group()) do if m.unit ~= "player" then hiders[#hiders + 1] = m.name end end
        if #hiders == 0 then print(K.f("SOLO", K.L("NAME"))) return end
        local secs = tonumber(arg) or 300
        K.say(K.f("HIDE", K.L("NAME"), UnitName("player"), 60))
        K.after(60, function()
            G.huntStart_(K.db, hiders, secs, K.now())
            last_hint = nil
            K.say(K.f("SEEK", K.L("NAME"), secs))
            ticker = K.every(1, tick)
        end)
    elseif cmd == "stop" then
        if K.db.round then finish() end
    else
        print(K.L("HELP"))
    end
end
