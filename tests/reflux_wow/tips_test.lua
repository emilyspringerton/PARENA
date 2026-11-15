-- tests/reflux_wow/tips_test.lua -- the REFLUX tips API (RefluxCore: stdlib/collection/tips.prn + runtime/wow/
-- reflux_tips.lua) and /rumor, in the packaged client, under real Lua 5.1.
-- usage: lua5.1 tips_test.lua <client dir (wow-package-collection.sh output)>
--
-- Part 1 loads RefluxCore alone and drives the API: publishing, REFLUX 3060/3061, catalogue reads with the locale
-- fallback, and the rules -- a tip from a DIFFERENT addon, weighted no-repeat rotation, no addon or tip twice in a
-- row while there is another, nothing to tell when the asker is alone. Part 2 plays /rumor in stubbed clients.
assert(_VERSION == "Lua 5.1", "must run under Lua 5.1, got " .. tostring(_VERSION))
local client = assert(arg[1], "usage: tips_test.lua <client dir>")

local passed, failed = 0, 0
local function eq(got, want, what)
    if got == want then passed = passed + 1
    else failed = failed + 1; print("FAIL: " .. what .. " (got " .. tostring(got) .. ", want " .. tostring(want) .. ")") end
end
local function has(s, sub, what) eq(type(s) == "string" and s:find(sub, 1, true) ~= nil, true, what .. " [" .. tostring(s) .. "]") end

local function toc_files(dir, name)
    local files = {}
    for line in io.lines(dir .. "/" .. name .. ".toc") do
        if line ~= "" and not line:match("^#") then files[#files + 1] = line end
    end
    return files
end

-- a bare environment: just enough of the client for RefluxCore (it needs no frames)
local function bare(world)
    local env = { print = function(...) world.out[#world.out + 1] = table.concat({ ... }, " ") end }
    for _, k in ipairs({ "pairs", "ipairs", "type", "tostring", "tonumber", "select", "string", "table", "math",
                         "next", "error", "assert", "pcall", "unpack", "setmetatable", "getmetatable" }) do
        env[k] = _G[k]
    end
    env._G = env
    env.time = function() return world.now end
    env.GetLocale = function() return world.locale end
    env.SlashCmdList = {}
    return env
end

local function load_addon(env, addon)
    local dir = client .. "/AddOns/" .. addon
    local ns = {}
    for _, file in ipairs(toc_files(dir, addon)) do
        local chunk = assert(loadfile(dir .. "/" .. file))
        setfenv(chunk, env)
        chunk(addon, ns)
    end
    return ns
end

-- every REFLUX row so far, oldest first, as {type, a, b, c}
local function rows(env)
    local H, out = env.RefluxHost, {}
    for i = 0, H.log_size() - 1 do
        out[#out + 1] = { H.action_type_at(i), H.action_a_at(i), H.action_b_at(i), H.action_c_at(i) }
    end
    return out
end

local function catalogue(name, n, extra)
    local en = { NAME = name }
    for i = 1, n do en["TIP_" .. i] = name .. " tip " .. i .. "." end
    for k, v in pairs(extra or {}) do en[k] = v end
    local gb = {}
    for k, v in pairs(en) do gb[k] = v end
    return { enUS = en, enGB = gb }
end

-- ===== Part 1: the API and its rules =======================================================================
local world = { out = {}, now = 1762732800, locale = "enUS" }
local env = bare(world)
load_addon(env, "RefluxCore")
local TIPS = env.RefluxTips
eq(type(TIPS), "table", "RefluxCore makes the global RefluxTips")
eq(TIPS.count(), 1, "RefluxCore published its own tips at load")
local core = TIPS.at(1)
eq(core.addon, "RefluxCore", "slot 1 is RefluxCore")
eq(core.spec, 0, "RefluxCore has no spec number")
eq(#core.keys >= 3 and #core.keys <= 6, true, "RefluxCore has 3-6 tips (" .. #core.keys .. ")")
eq(TIPS.name("RefluxCore"), "REFLUX Core", "its display name is its catalogue's NAME")
local r = rows(env)
eq(r[1][1], 3060, "publishing dispatched 3060")
eq(r[1][3], #core.keys, "with the tip count")
eq(r[1][4], 1, "and the registry slot")

-- publishing: TIP_1..n by default, an explicit list, weights, and publishing again keeps the slot
local A = catalogue("Alpha", 3)
local B = catalogue("Beta", 6)
local C = catalogue("Gamma", 2)
eq(TIPS.publish({ addon = "Alpha", spec = 54, catalogue = A }), 2, "Alpha gets slot 2")
eq(TIPS.publish({ addon = "Beta", spec = 0, catalogue = B, weights = { TIP_2 = 3 } }), 3, "Beta gets slot 3")
eq(TIPS.publish({ addon = "Gamma", spec = 65, catalogue = C, tips = { "TIP_2", "TIP_1" } }), 4, "Gamma gets slot 4")
eq(TIPS.at(4).keys[1], "TIP_2", "an explicit key list is kept in its order")
eq(TIPS.publish({ addon = "Alpha", spec = 54, catalogue = A }), 2, "publishing again keeps the slot")
eq(TIPS.count(), 4, "and adds no addon")
eq(TIPS.publish({ addon = "Empty", spec = 9, catalogue = { enUS = { NAME = "Empty" } } }), 0, "no TIP_ keys publishes nothing")
eq(TIPS.publish({ spec = 9, catalogue = A }), 0, "no addon name publishes nothing")
eq(TIPS.count(), 4, "neither took a slot")
r = rows(env)
local last = r[#r]
eq(last[1] == 3060 and last[2] == 54 and last[3] == 3 and last[4] == 2, true, "3060 carries spec 54, 3 tips, slot 2")
local T = env.RefluxCore.modules["collection/tips"]
eq(T.tipsWeight(nil), 1, "an unset weight is 1")
eq(T.tipsWeight(0), 1, "a weight under 1 is 1")
eq(T.tipsWeight(12), 9, "a weight over 9 is 9")
eq(T.tipsWeight(2.7), 2, "weights are whole tickets")

-- the text stays in the publisher's catalogue and is read in the client's locale, falling back to enUS
eq(TIPS.text("Beta", "TIP_4"), "Beta tip 4.", "a tip's text comes from its catalogue")
B.enGB.TIP_4 = "Beta tip 4, innit."
world.locale = "enGB"
eq(TIPS.text("Beta", "TIP_4"), "Beta tip 4, innit.", "an enGB client reads the enGB line")
world.locale = "deDE"
eq(TIPS.text("Beta", "TIP_4"), "Beta tip 4.", "a locale with no catalogue falls back to enUS")
world.locale = "enUS"
eq(TIPS.name("Nobody"), "Nobody", "an unknown addon is called by its folder name")

-- the rotation: asker Alpha, so RefluxCore (4 x 1) + Beta (5 x 1 + 1 x 3) + Gamma (2 x 1) = 14 tickets a cycle
local weight = { ["Beta:TIP_2"] = 3 }
local function cycle_size(asker)
    local n = 0
    for slot = 1, TIPS.count() do
        local e = TIPS.at(slot)
        if e.addon ~= asker then for _, k in ipairs(e.keys) do n = n + (weight[e.addon .. ":" .. k] or 1) end end
    end
    return n
end
local function play(asker, cycles)
    local st, size = {}, cycle_size(asker)
    local ok = { own = true, counts = true, addon_twice = true, tip_twice = true }
    local prev_addon, prev_tip
    for _ = 1, cycles do
        local told = {}
        for _ = 1, size do
            local addon, key = TIPS.pick(asker, st)
            local id = tostring(addon) .. ":" .. tostring(key)
            if addon == asker or addon == nil then ok.own = false end
            -- the same addon twice running is allowed only when every other addon has spent its tickets
            if addon == prev_addon then
                for slot = 1, TIPS.count() do
                    local e = TIPS.at(slot)
                    if e.addon ~= asker and e.addon ~= addon then
                        for _, k in ipairs(e.keys) do
                            local i2 = e.addon .. ":" .. k
                            if (told[i2] or 0) < (weight[i2] or 1) then ok.addon_twice = false end
                        end
                    end
                end
            end
            if id == prev_tip then
                local e = nil
                for slot = 1, TIPS.count() do if TIPS.at(slot).addon == addon then e = TIPS.at(slot) end end
                for _, k in ipairs(e and e.keys or {}) do
                    local i2 = addon .. ":" .. k
                    if i2 ~= id and (told[i2] or 0) < (weight[i2] or 1) then ok.tip_twice = false end
                end
            end
            told[id] = (told[id] or 0) + 1
            prev_addon, prev_tip = addon, id
        end
        for slot = 1, TIPS.count() do
            local e = TIPS.at(slot)
            if e.addon ~= asker then
                for _, k in ipairs(e.keys) do
                    local id = e.addon .. ":" .. k
                    if (told[id] or 0) ~= (weight[id] or 1) then ok.counts = false end
                end
            end
        end
    end
    return ok, st
end
local ok, st = play("Alpha", 6)
eq(ok.own, true, "asked by Alpha: never Alpha's own tips, never nothing")
eq(ok.counts, true, "every cycle tells each tip exactly its weight in times (Beta's TIP_2 three times)")
eq(ok.addon_twice, true, "never the same addon twice running while another still has tips this cycle")
eq(ok.tip_twice, true, "never the same tip twice running while its addon has another")
eq(st.told, 6 * 14, "the state counts every tip told")
eq(T.tipsLeft({ list = { TIPS.at(1), TIPS.at(2), TIPS.at(3), TIPS.at(4) } }, { seen = {} }, "Alpha"), 14,
   "a fresh cycle for Alpha holds 14 tickets")
ok = play("Beta", 3)
eq(ok.own and ok.counts and ok.addon_twice and ok.tip_twice, true, "the same holds asked by Beta (a weighted addon left out)")

-- the addon is chosen evenly, not by its tip count: over many first picks of a fresh cycle, Beta (6 tips) is not
-- favoured over Gamma (2 tips)
local firsts = {}
for i = 1, 300 do
    world.now = world.now + 37
    local addon = TIPS.pick("Alpha", {})
    firsts[addon] = (firsts[addon] or 0) + 1
end
eq((firsts.Beta or 0) < 2 * (firsts.Gamma or 0) and (firsts.Gamma or 0) < 2 * (firsts.Beta or 0), true,
   string.format("addons are picked evenly (Beta %d, Gamma %d, RefluxCore %d of 300)", firsts.Beta or 0, firsts.Gamma or 0, firsts.RefluxCore or 0))

-- telling a tip dispatches 3061: the told addon's spec, the tip's index, its slot
local addon, key = TIPS.pick("Alpha", {})
r = rows(env)
last = r[#r]
local slot = 0
for s = 1, TIPS.count() do if TIPS.at(s).addon == addon then slot = s end end
local idx = 0
for i, k in ipairs(TIPS.at(slot).keys) do if k == key then idx = i end end
eq(last[1] == 3061 and last[2] == TIPS.at(slot).spec and last[3] == idx and last[4] == slot, true,
   "3061 carries the told addon's spec, the tip's index and the slot")

-- rumor: without a carrier, the plain tip line; with one, its voice first; alone, the alone line
local lines = TIPS.rumor("Alpha")
eq(#lines, 1, "no carrier: one line")
has(lines[1], "  Tip (", "the plain tip line")
local voiced = {}
TIPS.carrier({ addon = "Gamma", state = {}, voice = function(name, own) voiced[#voiced + 1] = own; return "Gamma says, about " .. name .. ":" end,
               alone = function() return "Gamma has nobody to talk about." end })
lines = TIPS.rumor("Alpha")
eq(#lines, 2, "a carrier adds its voiced line")
has(lines[1], "Gamma says, about ", "in the carrier's voice")
has(lines[2], "  Tip (", "with the plain tip beside it")
TIPS.reset()
eq(TIPS.count(), 0, "reset empties the registry")
TIPS.publish({ addon = "Gamma", spec = 65, catalogue = C })
eq(TIPS.pick("Gamma", {}), nil, "the asker alone has nothing to pick")
TIPS.carrier({ addon = "Gamma", state = {}, voice = function() return "x" end, alone = function() return "Gamma has nobody to talk about." end })
lines = TIPS.rumor("Gamma")
eq(#lines == 1 and lines[1] == "Gamma has nobody to talk about.", true, "the asker alone gets the carrier's alone line")
TIPS.reset()
TIPS.publish({ addon = "Gamma", spec = 65, catalogue = C })
lines = TIPS.rumor("Gamma")
has(lines[1], "no other Collection addon", "without a carrier, RefluxCore's own alone line")

-- claim_slash: never a second handler for a command another addon already registered
env.SLASH_LOKISLIES1, env.SLASH_LOKISLIES2 = "/rumour", "/rumor"
env.SlashCmdList.LOKISLIES = function() end
eq(TIPS.claim_slash("TESTRUMOR", { "/rumor", "/rumour" }, function() end), false, "a claimed /rumor is left alone")
eq(env.SlashCmdList.TESTRUMOR, nil, "and nothing is registered")
env.SlashCmdList.LOKISLIES, env.SLASH_LOKISLIES1, env.SLASH_LOKISLIES2 = nil, nil, nil
eq(TIPS.claim_slash("TESTRUMOR", { "/rumor", "/rumour" }, function() end), true, "a free /rumor is claimed")
eq(env.SLASH_TESTRUMOR1 == "/rumor" and env.SLASH_TESTRUMOR2 == "/rumour" and type(env.SlashCmdList.TESTRUMOR) == "function",
   true, "both spellings, one handler")

-- ===== Part 2: /rumor in stubbed clients ====================================================================
-- A client close enough for every Collection addon to load and log in: frames answer any method, the group is a
-- party of four, chat to the group is recorded.
local function client_env(w)
    local env = bare(w)
    local frames = {}
    local noop = function() end
    env.CreateFrame = function(_, name)
        local f = setmetatable({ events = {}, scripts = {} }, { __index = function() return noop end })
        f.RegisterEvent = function(self, e) self.events[e] = true end
        f.SetScript = function(self, k, fn) self.scripts[k] = fn end
        f.GetScript = function(self, k) return self.scripts[k] end
        f.CreateFontString = function() return setmetatable({}, { __index = function() return noop end }) end
        frames[#frames + 1] = f
        if name then env[name] = f end
        return f
    end
    env.UIParent = {}
    env.UnitName = function(u)
        if u == "player" then return "Emily" end
        local i = u and tonumber(u:match("^party(%d)$"))
        if i and i <= 3 then return ({ "Brokkr", "Sindri", "Eitri" })[i] end
    end
    env.UnitClass = function() return "Mage", "MAGE" end
    env.UnitRace = function() return "Gnome", "Gnome" end
    env.UnitLevel = function() return 20 end
    env.UnitExists = function(u) return u == "player" end
    env.UnitIsPlayer = function(u) return u == "player" end
    env.GetRealmName = function() return "Forever" end
    env.GetMoney = function() return 123456 end
    env.GetTime = function() return w.now end
    env.IsInRaid = function() return false end
    env.IsInGroup = function() return w.grouped end
    env.GetNumGroupMembers = function() return w.grouped and 4 or 0 end
    env.SendChatMessage = function(text, chan) w.said[#w.said + 1] = chan .. ": " .. text end
    env.C_Map = { GetBestMapForUnit = function() return 1420 end,
                  GetPlayerMapPosition = function() return { GetXY = function() return 0.5, 0.5 end } end }
    env.NUM_BAG_SLOTS = 4
    env.C_Container = { GetContainerNumSlots = function() return 16 end, GetContainerItemInfo = function() return nil end }
    env.DoEmote = noop
    env.JumpOrAscendStart = noop
    env.GetInboxText = noop
    env.hooksecurefunc = function(name, fn)
        local orig = env[name]
        if orig then env[name] = function(...) local r = { orig(...) }; fn(...); return unpack(r) end end
    end
    local s = { env = env }
    function s.fire(event, ...)
        for _, f in ipairs(frames) do if f.events[event] and f.scripts.OnEvent then f.scripts.OnEvent(f, event, ...) end end
    end
    function s.login(addons, before_each)
        for _, a in ipairs(addons) do
            if before_each then before_each(a) end
            load_addon(env, a)
        end
        for _, a in ipairs(addons) do s.fire("ADDON_LOADED", a) end
        s.fire("PLAYER_LOGIN")
    end
    function s.slash(key, msg) w.out = {}; env.SlashCmdList[key](msg or ""); return w.out end
    return s
end

local function tip_of(lines)
    for _, l in ipairs(lines) do local name = l:match("^  Tip %((.-)%): ") if name then return name end end
end
local function all_filled(lines)
    for _, l in ipairs(lines) do if l:find("%%[sd]") then return false end end
    return true
end

-- the Collection with Loki's Lies: /rumor is Loki's, and also tells a tip in Ratatoskr's voice, never Loki's own
local cw = { out = {}, said = {}, now = 1762732800 + 3600, locale = "enUS", grouped = false }
local c1 = client_env(cw)
c1.login({ "RefluxCore", "CollectionJournal", "FafnirsBed", "CharacterBoard", "MacroSync", "Hrungnir", "RatatoskrRoll",
           "Volva", "SagaOfFools", "LokisLies", "MjolnirPass" })
local CT = c1.env.RefluxTips
eq(c1.env.SlashCmdList.RATATOSKRRUMOR, nil, "Ratatoskr leaves /rumor to Loki's Lies, which already has it")
local tipped, from_loki, voiced_ok, filled, rows_ok = {}, 0, true, true, true
for _ = 1, 40 do
    cw.now = cw.now + 11
    local out = c1.slash("LOKISLIES")
    has(out[1], "Loki's Lies: ", "/rumor still tells Loki's rumour first")
    local name = tip_of(out)
    if not name then rows_ok = false end
    if name == "Loki's Lies" then from_loki = from_loki + 1 end
    if name then tipped[name] = (tipped[name] or 0) + 1 end
    if not (out[2] and out[2]:find("^Ratatoskr Roll: ")) then voiced_ok = false end
    if not all_filled(out) then filled = false end
end
eq(rows_ok, true, "every /rumor carries a plain tip line")
eq(from_loki, 0, "/rumor never tells one of Loki's own tips")
eq(voiced_ok, true, "Ratatoskr voices every tip /rumor tells")
eq(filled, true, "every /rumor line is filled in")
local kinds = 0
for _ in pairs(tipped) do kinds = kinds + 1 end
eq(kinds >= 6, true, "40 rumours cover many addons (" .. kinds .. ")")

-- /census rumor: Ratatoskr asks, so never Ratatoskr's own tip, and never the self line
local from_self, self_voice = 0, 0
for _ = 1, 40 do
    cw.now = cw.now + 13
    local out = c1.slash("RATATOSKRROLL", "rumor")
    if tip_of(out) == "Ratatoskr Roll" then from_self = from_self + 1 end
    if out[1] and out[1]:find("For once the gossip is about me", 1, true) then self_voice = self_voice + 1 end
    if not (out[1] and out[1]:find("^Ratatoskr Roll: ") and tip_of(out)) then voiced_ok = false end
end
eq(from_self, 0, "/census rumor never tells one of Ratatoskr's own tips while others exist")
eq(self_voice, 0, "so it never gossips about itself")
eq(voiced_ok, true, "and always voices a tip from another addon")
eq(c1.env.RatatoskrRollDB.rumor.told, 80, "the rotation is kept in Ratatoskr Roll's saved table")

-- /rumor share in a party: the rumour and the tip go to the group
cw.grouped, cw.said = true, {}
c1.slash("LOKISLIES", "share")
local shared_tip = false
for _, l in ipairs(cw.said) do if l:find("^PARTY:   Tip %(") then shared_tip = true end end
eq(#cw.said >= 3 and shared_tip, true, "/rumor share says the rumour, the voice and the tip to the party")
cw.grouped = false

-- Ratatoskr Roll without Loki's Lies: it claims /rumor and /rumour itself
local rw = { out = {}, said = {}, now = 1762732800 + 3600, locale = "enUS", grouped = false }
local c2 = client_env(rw)
c2.login({ "RefluxCore", "RatatoskrRoll" })
eq(c2.env.SLASH_RATATOSKRRUMOR1 == "/rumor" and c2.env.SLASH_RATATOSKRRUMOR2 == "/rumour", true,
   "without Loki's Lies, Ratatoskr Roll registers /rumor and /rumour")
local out = c2.slash("RATATOSKRRUMOR")
eq(#out, 2, "its /rumor: a voiced line and a tip")
eq(tip_of(out), "REFLUX Core", "about the only other addon, REFLUX Core")

-- Ratatoskr Roll as the only addon with tips: the alone line, nothing made up
local aw = { out = {}, said = {}, now = 1762732800 + 3600, locale = "enUS", grouped = false }
local c3 = client_env(aw)
c3.login({ "RefluxCore", "RatatoskrRoll" }, function(a) if a == "RatatoskrRoll" then c3.env.RefluxTips.reset() end end)
eq(c3.env.RefluxTips.count(), 1, "only Ratatoskr Roll published")
out = c3.slash("RATATOSKRROLL", "rumor")
eq(#out, 1, "alone: one line")
has(out[1], "found no other Collection addon to gossip about", "Ratatoskr says it found nobody else")
out = c3.slash("RATATOSKRRUMOR")
eq(#out == 1 and tip_of(out) == nil, true, "and /rumor tells no tip either")

-- Loki asking with only Ratatoskr Roll beside it: Ratatoskr's own tip, in its self voice
local lw = { out = {}, said = {}, now = 1762732800 + 3600, locale = "enUS", grouped = false }
local c4 = client_env(lw)
c4.login({ "RefluxCore", "RatatoskrRoll", "LokisLies" }, function(a) if a == "RatatoskrRoll" then c4.env.RefluxTips.reset() end end)
out = c4.slash("LOKISLIES")
eq(tip_of(out), "Ratatoskr Roll", "asked by Loki, Ratatoskr Roll is one of the other addons")
has(out[2], "For once the gossip is about me", "and Ratatoskr says so in its own voice")

-- the voice follows the locale rules: an enGB client reads enGB, a deDE one falls back to enUS
cw.locale = "deDE"
out = c1.slash("RATATOSKRROLL", "rumor")
eq(out[1] ~= nil and out[1]:find("^Ratatoskr Roll: ") ~= nil and tip_of(out) ~= nil, true, "a deDE client falls back to the enUS lines")
cw.locale = "enUS"

print(string.format("tips: %d passed, %d failed", passed, failed))
os.exit(failed == 0 and 0 or 1)
