-- runtime/wow/collection_journal.lua -- the CollectionJournal addon's host glue (Collection of Value, Step 6).
--
-- /cov   how many rows the journal holds, trimmed and lost
--
-- The journal itself is PARENA (stdlib/collection/journal.prn, registered by the packager as
-- RefluxCore.modules["collection/journal"]). This file only does what emit_lua cannot express yet -- a frame,
-- event and OnUpdate callbacks, a slash command -- and hands the journal its context. Same role as
-- reflux_host.lua: host glue, not addon logic.
--
-- Client calls, and how far each is trusted (docs/COLLECTION_OF_VALUE_STEP1_AND_DISPUTES.md, section 1):
--   CreateFrame, RegisterEvent, SetScript("OnEvent"/"OnUpdate"), ADDON_LOADED, PLAYER_LOGOUT, SlashCmdList,
--   UnitName, GetRealmName, time, print   -- present in every WoW client since 1.x
--   C_Map.GetBestMapForUnit, C_Map.GetPlayerMapPosition -- retail baseline, NOT confirmed on Forever:
--     feature-tested on every call; when absent the record's zone/x/y are nil, never guessed.
--
-- The packager substitutes the @...@ values from the environment (scripts/wow-package-collection.sh).

local ADDON = "@ADDON@"
local VAR = "@VAR@"                      -- the ## SavedVariablesPerCharacter global
local KEEP = tonumber("@KEEP@")          -- journal size cap (rows), journal-trim
local POLL_SECONDS = tonumber("@POLL@")  -- how often REFLUX is polled while playing

local J = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/journal"]
if not J then
    print("|cffff5555" .. ADDON .. ": RefluxCore's collection/journal module is missing; journal disabled.|r")
    return
end

local db = nil

local function character()
    local name = UnitName and UnitName("player")
    local realm = GetRealmName and GetRealmName()
    if not name or name == "" then return nil end
    if realm and realm ~= "" then return name .. "-" .. realm end
    return name
end

local function where()
    if not (C_Map and C_Map.GetBestMapForUnit) then return nil, nil, nil end
    local map = C_Map.GetBestMapForUnit("player")
    if not map then return nil, nil, nil end
    if not C_Map.GetPlayerMapPosition then return map, nil, nil end
    local pos = C_Map.GetPlayerMapPosition(map, "player")
    if not (pos and pos.GetXY) then return map, nil, nil end
    local x, y = pos:GetXY()
    return map, x, y
end

-- One poll: everything REFLUX dispatched since the last one becomes journal rows, then the cap applies.
local function poll()
    local who = character()
    if not db or not who then return 0 end
    local zone, x, y = where()
    local n = J.journalPoll(db, who, time(), zone, x, y)
    J.journalTrim(db, KEEP)
    return n
end

local frame = CreateFrame("Frame")
frame:RegisterEvent("ADDON_LOADED")
frame:RegisterEvent("PLAYER_LOGOUT")
frame:SetScript("OnEvent", function(_, event, arg1)
    if event == "ADDON_LOADED" and arg1 == ADDON then
        -- SavedVariables are loaded by now: adopt (or create) the table, upgrading it in place.
        _G[VAR] = J.journalInit(_G[VAR] or {})
        db = _G[VAR]
    elseif event == "PLAYER_LOGOUT" then
        poll()                               -- last rows in before the client writes the file
    end
end)

local elapsed = 0
frame:SetScript("OnUpdate", function(_, dt)
    elapsed = elapsed + (dt or 0)
    if elapsed >= POLL_SECONDS then
        elapsed = 0
        poll()
    end
end)

SLASH_COLLECTIONJOURNAL1 = "/cov"
SlashCmdList["COLLECTIONJOURNAL"] = function()
    if not db then print(ADDON .. ": not loaded yet."); return end
    poll()
    print(string.format("%s: %d rows held (seq %d-%d), %d trimmed by the cap, %d lost in REFLUX. Written to disk on logout or /reload.",
        ADDON, db.nextSeq - db.firstSeq, db.firstSeq, db.nextSeq - 1, db.trimmed, db.lost))
end

-- for tests and other glue: the poll, by hand
CollectionJournal = { poll = poll }
