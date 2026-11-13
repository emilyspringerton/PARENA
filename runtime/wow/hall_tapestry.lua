-- runtime/wow/hall_tapestry.lua -- Hall Tapestry's host glue (spec addon 69, photo planner). Rules: stdlib/collection/tapestry.prn.
-- /tapestry [shots]   plan the poses (default 4), then for each: arrangement + pose callout, 3-2-1 in group
--                     chat, and a screenshot on zero (Screenshot(), when the client allows it)
local ADDON, VAR = "@ADDON@", "@VAR@"
local _, ns = ...
local K = ns.kit
local G = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/tapestry"]
if not G then print(ADDON .. ": RefluxCore's collection/tapestry module is missing.") return end
K.saved(VAR, function(db) db.sessions = db.sessions or 0; db.shots = db.shots or 0; db.v = 1; return db end)

local busy = false

local function shoot(plan, i, taken)
    if i > #plan then
        G.tapDone_(taken, #plan)
        K.db.sessions = K.db.sessions + 1
        K.say(K.f("DONE", K.L("NAME"), taken))
        busy = false
        return
    end
    local pose, arr = plan[i][1], plan[i][2]
    K.say(K.f("SHOT", i, #plan, K.L("ARR_" .. arr), K.emote_cmd(K.EMOTES[pose])))
    K.after(2, function() K.say("3") end)
    K.after(3, function() K.say("2") end)
    K.after(4, function() K.say("1") end)
    K.after(5, function()
        local ok = Screenshot and pcall(Screenshot)
        G.tapShot_(i, pose, arr)
        if ok then K.db.shots = K.db.shots + 1 end
        K.say(ok and K.L("CLICK") or K.L("NOSHOT"))
        K.after(3, function() shoot(plan, i + 1, taken + (ok and 1 or 0)) end)
    end)
end

SLASH_HALLTAPESTRY1 = "/tapestry"
SlashCmdList["HALLTAPESTRY"] = function(msg)
    if not K.db or busy then return end
    local n = tonumber((msg or ""):match("(%d+)")) or 4
    busy = true
    local plan = G.tapPlan(n, K.now())
    K.say(K.f("START", K.L("NAME"), #plan))
    K.after(2, function() shoot(plan, 1, 0) end)
end
