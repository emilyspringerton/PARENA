-- runtime/wow/fun_kit.lua -- the host plumbing every Collection party game shares (spec addons 54-73). Copied
-- into each game's folder as kit.lua and loaded after the game's catalogue; it puts its API on the addon's own
-- namespace (ns.kit), so two games never share state.
--
-- What it wraps, and how far each call is trusted (none is probed on Forever yet; each is feature-tested):
--   group      IsInRaid/IsInGroup/GetNumGroupMembers, UnitName on "player", "partyN" and "raidN"
--   positions  C_Map.GetBestMapForUnit("player") + C_Map.GetPlayerMapPosition(map, unit) -> permille of the
--              player's map; nil for anyone not on that map (retail hides positions in instances)
--   chat       SendChatMessage to the group (RAID / PARTY), or print when solo; CHAT_MSG_PARTY/_LEADER,
--              RAID/_LEADER, GUILD and SAY for answers; CHAT_MSG_TEXT_EMOTE for emotes, matched against the
--              catalogue's per-locale patterns (EMOTE_<TOKEN>) because the client sends the emote as text
--   time       GetTime (sub-second) when present, else time(); one OnUpdate drives every timer
-- The games' own words come from their catalogues; this file only falls back to the kit catalogue.

local ADDON, ns = ...
local K = {}
ns.kit = K

function K.L(key)
    local loc = GetLocale and GetLocale() or "enUS"
    for _, c in ipairs({ ns.catalogue, ns.kit_catalogue }) do
        if c then
            local t = c[loc]
            local v = (t and t[key]) or (c.enUS and c.enUS[key])
            if v then return v end
        end
    end
    return key
end

function K.f(key, ...) return string.format(K.L(key), ...) end

function K.now() return time() end
function K.clock() return GetTime and GetTime() or time() end

function K.short(name) return name and (name:match("^([^-]+)") or name) or nil end

function K.me()
    local name = UnitName and UnitName("player")
    local realm = GetRealmName and GetRealmName()
    if not name then return nil end
    return (realm and realm ~= "") and (name .. "-" .. realm) or name
end

-- Everyone in the group, the player first: { {unit = "player", name = "Emily"}, ... } (short names).
function K.group()
    local out = { { unit = "player", name = UnitName("player") } }
    local raid = IsInRaid and IsInRaid()
    local n = (GetNumGroupMembers and GetNumGroupMembers()) or 0
    if raid then
        for i = 1, n do
            local u = "raid" .. i
            local name = UnitName(u)
            if name and name ~= out[1].name then out[#out + 1] = { unit = u, name = name } end
        end
    else
        for i = 1, math.max(n - 1, 0) do
            local u = "party" .. i
            local name = UnitName(u)
            if name then out[#out + 1] = { unit = u, name = name } end
        end
    end
    return out
end

function K.unit_of(name)
    name = K.short(name)
    for _, m in ipairs(K.group()) do if m.name == name then return m.unit end end
    return nil
end

-- map id, x, y in permille for a unit on the player's map; nil when it cannot be read.
function K.pos(unit)
    if not (C_Map and C_Map.GetBestMapForUnit and C_Map.GetPlayerMapPosition) then return nil end
    local map = C_Map.GetBestMapForUnit("player")
    if not map then return nil end
    local p = C_Map.GetPlayerMapPosition(map, unit or "player")
    if not p then return nil end
    local x, y = p:GetXY()
    if not x or (x == 0 and y == 0) then return nil end
    return map, math.floor(x * 1000 + 0.5), math.floor(y * 1000 + 0.5)
end

function K.channel()
    if IsInRaid and IsInRaid() then return "RAID" end
    if IsInGroup and IsInGroup() then return "PARTY" end
    return nil
end

-- Say it to the group, or to yourself when solo.
function K.say(text)
    local chan = K.channel()
    if chan and SendChatMessage then SendChatMessage(text, chan) else print(text) end
end

function K.notice(text)
    print(text)
    if RaidNotice_AddMessage and RaidWarningFrame and ChatTypeInfo then
        RaidNotice_AddMessage(RaidWarningFrame, text, ChatTypeInfo["RAID_WARNING"])
    end
end

-- ---- events and timers -------------------------------------------------------------------------------
local frame = CreateFrame("Frame")
local handlers = {}
local timers = {}

function K.on(event, fn)
    if not handlers[event] then handlers[event] = {}; frame:RegisterEvent(event) end
    table.insert(handlers[event], fn)
end

frame:SetScript("OnEvent", function(_, event, ...)
    for _, fn in ipairs(handlers[event] or {}) do fn(...) end
end)

-- fn() after `secs`; every > 0 repeats. Returns a handle for K.cancel.
function K.after(secs, fn, every)
    local t = { at = K.clock() + secs, fn = fn, every = every }
    timers[#timers + 1] = t
    return t
end
function K.every(secs, fn) return K.after(secs, fn, secs) end
function K.cancel(t) if t then t.dead = true end end

frame:SetScript("OnUpdate", function()
    local now = K.clock()
    local due = {}
    for i = #timers, 1, -1 do
        local t = timers[i]
        if t.dead then table.remove(timers, i)
        elseif now >= t.at then
            due[#due + 1] = t
            if t.every then t.at = now + t.every else table.remove(timers, i) end
        end
    end
    for _, t in ipairs(due) do if not t.dead then t.fn() end end
end)

-- The addon's SavedVariables, once loaded: fn(db).
function K.saved(var, init)
    K.on("ADDON_LOADED", function(name)
        if name == ADDON then
            _G[var] = init(_G[var] or {})
            K.db = _G[var]
        end
    end)
end

-- ---- chat ---------------------------------------------------------------------------------------------
local CHAT = { "CHAT_MSG_PARTY", "CHAT_MSG_PARTY_LEADER", "CHAT_MSG_RAID", "CHAT_MSG_RAID_LEADER",
               "CHAT_MSG_GUILD", "CHAT_MSG_SAY" }
-- fn(text, sender (short name), event). Other players' words are matched in memory and never stored (D-8).
function K.on_chat(fn)
    for _, e in ipairs(CHAT) do K.on(e, function(text, sender) fn(text or "", K.short(sender), e) end) end
end

K.EMOTES = { "WAVE", "DANCE", "CHEER", "BOW", "CLAP", "SALUTE", "FLEX", "LAUGH", "KNEEL", "ROAR" }

-- The emote token a text emote line shows, from the catalogue's patterns; nil when none matches.
function K.emote_of(text)
    if type(text) ~= "string" then return nil end
    local low = text:lower()
    for _, tok in ipairs(K.EMOTES) do
        local pat = K.L("EMOTE_" .. tok)
        if pat ~= "EMOTE_" .. tok and low:find(pat) then return tok end
    end
    return nil
end

-- fn(token, sender (short name)) for every text emote anyone in range makes, the player's own included.
function K.on_emote(fn)
    K.on("CHAT_MSG_TEXT_EMOTE", function(text, sender)
        local tok = K.emote_of(text)
        if tok then fn(tok, K.short(sender)) end
    end)
end

-- the emote's slash command, for callouts
function K.emote_cmd(tok) return "/" .. tok:lower() end
