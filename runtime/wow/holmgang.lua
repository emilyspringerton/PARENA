-- runtime/wow/holmgang.lua -- Holmgang's host glue (spec addon 67, duel bracket). Rules: stdlib/collection/holmgang.prn.
-- /holmgang start [names...]   seed the named fighters in that order (default: the group, shuffled)
-- /holmgang win <name>         record a result by hand
-- /holmgang bracket | stop     show the open matches / call the cup off
-- Results are also read from the client's own duel-won system line (DUEL_WINNER_KNOCKOUT / DUEL_WINNER_RETREAT,
-- whose wording comes from the client itself, so this works in every locale that has them).
local ADDON, VAR = "@ADDON@", "@VAR@"
local _, ns = ...
local K = ns.kit
local G = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/holmgang"]
if not G then print(ADDON .. ": RefluxCore's collection/holmgang module is missing.") return end
K.saved(VAR, G.holmInit)

local function show()
    for _, m in ipairs(G.holmMatches(K.db)) do K.say(K.f("MATCH", G.holmRound(K.db), m[2], m[3])) end
end

local function won(name)
    if G.holmWin_(K.db, name) == 0 then return false end
    K.say(K.f("WON", name))
    local champ = G.holmChampion(K.db)
    if champ then K.notice(K.f("CHAMPION", K.L("NAME"), champ)) else show() end
    return true
end

local function as_pattern(fmt)
    return "^" .. fmt:gsub("([%(%)%.%%%+%-%*%?%[%]%^%$])", "%%%1"):gsub("%%%%s", "(.+)") .. "$"
end

K.on("CHAT_MSG_SYSTEM", function(text)
    if not (K.db and K.db.cup) then return end
    for _, g in ipairs({ "DUEL_WINNER_KNOCKOUT", "DUEL_WINNER_RETREAT" }) do
        local fmt = _G[g]
        if type(fmt) == "string" then
            local a = text:match(as_pattern(fmt))
            if a then won(K.short(a)) return end
        end
    end
end)

SLASH_HOLMGANG1 = "/holmgang"
SlashCmdList["HOLMGANG"] = function(msg)
    if not K.db then return end
    local cmd, rest = (msg or ""):match("^%s*(%S*)%s*(.-)%s*$")
    if cmd == "start" then
        local names, seed = {}, 0
        for n in rest:gmatch("%S+") do names[#names + 1] = K.short(n) end
        if #names == 0 then
            for _, m in ipairs(K.group()) do names[#names + 1] = m.name end
            seed = K.now()
        end
        if #names < 2 then print(K.f("SOLO", K.L("NAME"))) return end
        G.holmStart_(K.db, names, seed)
        K.say(K.f("START", K.L("NAME"), #names))
        show()
    elseif cmd == "win" then
        if not won(K.short(rest)) then print(K.f("NOMATCH", rest)) end
    elseif cmd == "bracket" then
        show()
    elseif cmd == "stop" then
        K.db.cup = nil
    else
        print(K.L("HELP"))
    end
end
