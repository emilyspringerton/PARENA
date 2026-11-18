-- runtime/wow/guild_herald.lua -- GuildHerald's host glue. Every decision (what counts as a change, how much history a
-- guild keeps, what a valid Discord invite code is, the REFLUX rows) is PARENA: stdlib/collection/guildherald.prn,
-- RefluxCore.modules["collection/guildherald"]. This file reads the client and finds invite-looking text.
--
-- Which client calls, all feature-tested (a missing one is skipped and named in /herald why):
--   IsInGuild, GetGuildInfo("player") -> name, rank, rankIndex, realm (realm is nil for the player's own realm)
--   GetGuildRosterMOTD, GetGuildInfoText; C_GuildInfo.GuildRoster or GuildRoster to ask the server for them
--   events GUILD_MOTD (the MOTD as it is set or shown at login), GUILD_ROSTER_UPDATE, PLAYER_GUILD_UPDATE
-- The log (every MOTD and info text a guild has shown, with who saw it) leaves the game through SavedVariables;
-- PARENA's scripts/collection-guild-feed.sh turns it into an NDJSON log stream and the Collection feed.

local ADDON = "@ADDON@"
local VAR = "@VAR@"

local H = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/guildherald"]
if not H then
    print("|cffff5555" .. ADDON .. ": RefluxCore's collection/guildherald module is missing; GuildHerald disabled.|r")
    return
end

local db = nil
local missing = {}

local function who()
    local name = UnitName and UnitName("player")
    local realm = GetRealmName and GetRealmName()
    if not name or name == "" then return nil end
    if realm and realm ~= "" then return name .. "-" .. realm end
    return name
end

local function guild()
    if not (IsInGuild and IsInGuild()) then return nil end
    if not GetGuildInfo then missing.guild = "no GetGuildInfo"; return nil end
    local name, _, _, realm = GetGuildInfo("player")
    if not name or name == "" then return nil end
    if not realm or realm == "" then realm = (GetRealmName and GetRealmName()) or "?" end
    return name, realm
end

-- discord.gg/<code>, discord.com/invite/<code>, discordapp.com/invite/<code>. The link is found case-insensitively,
-- the code is read from the original text (invite codes are case-sensitive) and checked by the rules.
local PREFIXES = { "discord.gg/", "discord.com/invite/", "discordapp.com/invite/" }

local function valid_code(code)
    if H.ghInviteLenOk(#code) ~= 1 then return false end
    for i = 1, #code do if H.ghInviteCharOk(code:byte(i)) ~= 1 then return false end end
    return true
end

local function invites_in(text)
    local found, seen = {}, {}
    local lower = text:lower()
    for _, prefix in ipairs(PREFIXES) do
        local from = 1
        while true do
            local s, e = lower:find(prefix, from, true)
            if not s then break end
            local code = text:sub(e + 1):match("^([%w%-]+)")
            if code and valid_code(code) and not seen[code] then seen[code] = true; found[#found + 1] = code end
            from = e + 1
        end
    end
    return found
end

local function observe(kind, text)
    if not (db and text and text ~= "") then return end
    local name, realm = guild()
    if not name then return end
    local key = name .. "-" .. realm
    local now = time()
    H.ghGuild(db, key, name, realm, now)
    local codes = invites_in(text)
    H.ghObserve(db, key, kind, text, now, who() or "?", #codes)
    for _, code in ipairs(codes) do H.ghAddInvite(db, key, code, now) end
end

local last_ask = 0
local function scan()
    if not (IsInGuild and IsInGuild()) then return end
    local now = time()
    if now - last_ask >= 30 then   -- ask the server for the roster (and its MOTD/info) at most every 30 s
        last_ask = now
        if C_GuildInfo and C_GuildInfo.GuildRoster then C_GuildInfo.GuildRoster()
        elseif GuildRoster then GuildRoster()
        else missing.roster = "no C_GuildInfo.GuildRoster or GuildRoster" end
    end
    if GetGuildRosterMOTD then observe(H.ghKindMotd(), GetGuildRosterMOTD()) else missing.motd = "no GetGuildRosterMOTD" end
    if GetGuildInfoText then observe(H.ghKindInfo(), GetGuildInfoText()) else missing.info = "no GetGuildInfoText" end
end

local frame = CreateFrame("Frame")
for _, e in ipairs({ "ADDON_LOADED", "PLAYER_LOGIN", "GUILD_MOTD", "GUILD_ROSTER_UPDATE", "PLAYER_GUILD_UPDATE" }) do
    pcall(frame.RegisterEvent, frame, e)
end
frame:SetScript("OnEvent", function(_, event, a1)
    if event == "ADDON_LOADED" then
        if a1 == ADDON then _G[VAR] = H.ghInit(_G[VAR] or {}); db = _G[VAR] end
    elseif not db then
        return
    elseif event == "PLAYER_LOGIN" then
        if C_Timer and C_Timer.After then C_Timer.After(5, scan) else scan() end
    elseif event == "GUILD_MOTD" then
        observe(H.ghKindMotd(), a1)
    else
        scan()
    end
end)

SLASH_GUILDHERALD1 = "/herald"
SlashCmdList.GUILDHERALD = function(msg)
    if not db then print("GuildHerald is not loaded yet."); return end
    if (msg or ""):lower() == "why" then
        local any = false
        for k, why in pairs(missing) do any = true; print("  not seen on this client: " .. k .. " -- " .. why) end
        if not any then print("  every guild call is available on this client.") end
        return
    end
    local n = 0
    for _, g in pairs(db.guilds) do
        n = n + 1
        local codes = {}
        for code in pairs(g.invites or {}) do codes[#codes + 1] = "discord.gg/" .. code end
        table.sort(codes)
        print(string.format("|cff40ff40<%s>|r (%s): %d messages logged%s", g.name, g.realm, #(g.history or {}),
            #codes > 0 and (", Discord: " .. table.concat(codes, " ")) or ""))
        if g.motd and g.motd ~= "" then print("  MOTD: " .. g.motd) end
    end
    if n == 0 then print("GuildHerald: no guild seen yet (log in a character that is in a guild).") end
    print("Type /ascend in SHANKPIT's chat to open these Discords. /herald why")
end
