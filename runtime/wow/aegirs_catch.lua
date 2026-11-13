-- runtime/wow/aegirs_catch.lua -- Ægir's Catch's host glue (spec addon 62, fishing derby). Rules: stdlib/collection/aegir.prn.
-- /derby start [minutes]   a timed derby for the whole group (default 15)
-- /derby stop | record     end it now / the all-time biggest catch
-- What counts as a catch: your own loot while IsFishingLoot() says the loot window is fishing loot (and such items
-- are remembered as fish for the account), and a group member's loot line (CHAT_MSG_LOOT) for an item already
-- known to be fish. Prices are GetItemInfo's vendor sell price -- the auction house tools are not built yet.
local ADDON, VAR = "@ADDON@", "@VAR@"
local _, ns = ...
local K = ns.kit
local G = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/aegir"]
if not G then print(ADDON .. ": RefluxCore's collection/aegir module is missing.") return end
K.saved(VAR, function(db) db = G.aegirInit(db); db.fish = db.fish or {}; return db end)

local timer = nil

local function money(c) return string.format("%dg %ds %dc", math.floor(c / 10000), math.floor(c / 100) % 100, c % 100) end

local function catch(who, id)
    if not (K.db and K.db.derby) then return end
    local name, _, q, _, _, _, _, _, _, _, price = GetItemInfo(id)
    if G.aegirCatch_(K.db, who, id, price or 0, q or 0, K.now()) == 1 then
        K.say(K.f("BIGGEST", who, name or ("item " .. id), money(price or 0)))
    end
end

local function finish()
    K.cancel(timer); timer = nil
    local r = G.aegirFinish_(K.db)
    K.say(K.f("OVER", K.L("NAME"), r[3]))
    if r[1] then K.say(K.f("WIN_BIG", r[1][1], (GetItemInfo(r[1][2])) or r[1][2], money(r[1][3]))) end
    if r[2] then K.say(K.f("WIN_WEIRD", r[2][1], (GetItemInfo(r[2][2])) or r[2][2])) end
end

K.on("LOOT_OPENED", function()
    if not (IsFishingLoot and IsFishingLoot() and GetNumLootItems and GetLootSlotLink) then return end
    for slot = 1, GetNumLootItems() do
        local link = GetLootSlotLink(slot)
        local id = link and tonumber(link:match("item:(%d+)"))
        if id then K.db.fish[id] = true; catch(UnitName("player"), id) end
    end
end)

K.on("CHAT_MSG_LOOT", function(text, sender)
    local who = K.short(sender)
    if not who or who == "" or who == UnitName("player") then return end
    local id = tonumber((text or ""):match("item:(%d+)"))
    if id and K.db and K.db.fish[id] then catch(who, id) end
end)

SLASH_AEGIRSCATCH1 = "/derby"
SlashCmdList["AEGIRSCATCH"] = function(msg)
    if not K.db then return end
    local cmd, n = (msg or ""):match("^%s*(%S*)%s*(%d*)")
    if cmd == "start" then
        local mins = tonumber(n) or 15
        G.aegirStart_(K.db, mins * 60, K.now())
        K.say(K.f("START", K.L("NAME"), mins))
        timer = K.after(mins * 60, finish)
    elseif cmd == "stop" then
        if K.db.derby then finish() end
    elseif cmd == "record" then
        local b = K.db.records.biggest
        if b then print(K.f("RECORD", b[1], (GetItemInfo(b[2])) or b[2], money(b[3]))) else print(K.f("NORECORD", K.L("NAME"))) end
    else
        print(K.L("HELP"))
    end
end
