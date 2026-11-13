-- runtime/wow/bragis_verse.lua -- Bragi's Verse's host glue (spec addon 63, haiku duel). Rules: stdlib/collection/bragi.prn.
-- /verse duel <poet> <poet>   start a duel; each poet posts ONE chat line with the haiku's three lines split by " / "
-- /verse check <a / b / c>    score your own haiku privately
-- /verse stop                 call the duel off
local ADDON, VAR = "@ADDON@", "@VAR@"
local _, ns = ...
local K = ns.kit
local G = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/bragi"]
if not G then print(ADDON .. ": RefluxCore's collection/bragi module is missing.") return end
K.saved(VAR, G.bragiInit)

local function lines(text)
    local out = {}
    for part in (text .. "/"):gmatch("([^/]*)/") do
        part = part:match("^%s*(.-)%s*$")
        if part ~= "" then out[#out + 1] = part end
    end
    return out
end

local function counts(text)
    local l = lines(text)
    if #l ~= 3 then return nil end
    return G.bragiSyllables(l[1]), G.bragiSyllables(l[2]), G.bragiSyllables(l[3])
end

K.on_chat(function(text, sender)
    local d = K.db and K.db.duel
    if not d or (sender ~= d.a and sender ~= d.b) then return end
    local s1, s2, s3 = counts(text)
    if not s1 then return end
    local score = G.bragiSubmit_(K.db, sender, s1, s2, s3)
    if score < 0 then return end
    K.say(K.f("SCORED", sender, s1, s2, s3, score))
    local r = G.bragiResult(K.db)
    if r then
        if r[1] then K.notice(K.f("WINNER", K.L("NAME"), r[1], r[2], r[3])) else K.say(K.f("TIE", K.L("NAME"), r[2])) end
    end
end)

SLASH_BRAGISVERSE1 = "/verse"
SlashCmdList["BRAGISVERSE"] = function(msg)
    if not K.db then return end
    local cmd, rest = (msg or ""):match("^%s*(%S*)%s*(.-)%s*$")
    if cmd == "duel" then
        local a, b = rest:match("^(%S+)%s+(%S+)$")
        if not a then print(K.L("HELP")) return end
        G.bragiStart_(K.db, K.short(a), K.short(b))
        K.say(K.f("START", K.L("NAME"), K.short(a), K.short(b)))
    elseif cmd == "check" then
        local s1, s2, s3 = counts(rest)
        if not s1 then print(K.L("HELP")) return end
        local l = { G.bragiLineScore(s1, 5), G.bragiLineScore(s2, 7), G.bragiLineScore(s3, 5) }
        print(K.f("CHECK", s1, s2, s3, l[1] + l[2] + l[3] + ((s1 == 5 and s2 == 7 and s3 == 5) and 5 or 0)))
    elseif cmd == "stop" then
        K.db.duel = nil
    else
        print(K.L("HELP"))
    end
end
