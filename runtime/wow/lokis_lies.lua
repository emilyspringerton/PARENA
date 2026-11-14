-- runtime/wow/lokis_lies.lua -- Loki's Lies' host glue (spec addon 68, rumour mill). Rules: stdlib/collection/loki.prn.
--
-- /rumour         a fresh rumour, just for you
-- /rumour share   say it to the group
--
-- A rumour at every login and on /rumour, made up from this character's own Collection Journal (the REFLUX rows
-- every Collection addon writes). /rumour share says it to the group instead of only to you.
local ADDON, VAR = "@ADDON@", "@VAR@"
local JOURNAL_VAR = "@JOURNAL_VAR@"
local _, ns = ...
local K = ns.kit
local G = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/loki"]
if not G then print(ADDON .. ": RefluxCore's collection/loki module is missing.") return end
K.saved(VAR, function(db) db.told = db.told or 0; db.v = 1; return db end)

local function class_name()
    local c = UnitClass and UnitClass("player")
    return c or K.L("SOMEONE")
end

-- the journal's rows, newest first, as {type, a, b, c}
local function rows()
    local j, out = _G[JOURNAL_VAR], {}
    if type(j) ~= "table" or not j.rows then return out end
    for seq = (j.nextSeq or 1) - 1, (j.firstSeq or 1), -1 do
        local r = j.rows[seq]
        if r then out[#out + 1] = { r.type, r.a or 0, r.b or 0, r.c or 0 } end
        if #out >= 50 then break end
    end
    return out
end

local function rumour()
    local rs = rows()
    local pick = G.lokiPick(rs, K.now() + (K.db.told or 0) * 17)
    if not pick then return K.L("QUIET") end
    local r = rs[pick[1]]
    local t, a = r[1], math.abs(r[2])
    local value = (t == 2011) and math.floor(a / 10000) or a
    G.lokiTold_(t, pick[2], pick[1])
    K.db.told = (K.db.told or 0) + 1
    return K.f("R" .. t .. "_" .. pick[2], class_name(), value, r[3])
end

K.on("PLAYER_LOGIN", function() K.after(5, function() print(K.f("HEADLINE", K.L("NAME"), rumour())) end) end)

SLASH_LOKISLIES1 = "/rumour"
SLASH_LOKISLIES2 = "/rumor"
SlashCmdList["LOKISLIES"] = function(msg)
    if not K.db then return end
    local line = K.f("HEADLINE", K.L("NAME"), rumour())
    if (msg or ""):match("share") then K.say(line) else print(line) end
end
