-- runtime/wow/reflux_tips.lua -- the REFLUX tips API: every Collection addon publishes its tips here, and /rumor
-- tells you one about ANOTHER installed addon (the founder, 2026-11-13: "can we have /rumor also tell you a tip for
-- one of the other plugins installed? this means all plugins should publish tips via some kind of reflux
-- compatible tips api"). Part of RefluxCore, loaded after its catalogue (locale.lua) and the rules
-- (stdlib/collection/tips.prn, RefluxCore.modules["collection/tips"]), which decide every pick. This file only
-- holds the registry, reads catalogues and formats the lines.
--
-- PUBLISHING -- every addon, once, at file load (its locale.lua is loaded before its glue):
--   if RefluxTips then RefluxTips.publish({ addon = ADDON, spec = 65, catalogue = ns.catalogue }) end
--     addon      your folder name (the first value of `...`, the packager's @ADDON@)
--     spec       your spec addon number, 0 when you have none
--     catalogue  your { enUS = {...}, enGB = {...} }; your tips are its keys TIP_1 .. TIP_n (enUS says how many),
--                and NAME, when present, is the name /rumor calls you by
--     tips       optional: an explicit key list instead of TIP_1 .. TIP_n
--     weights    optional: { TIP_2 = 3 } -- 1 to 9, default 1; a weight-3 tip is told three times a rotation
--   Returns your registry slot, 0 when nothing was published. Tips are catalogue KEYS: the text stays in your
--   catalogue and is read when the tip is told, in the client's locale, falling back to enUS. Write 3-6 true tips
--   about what your addon really does, in plain text (no %).
--
-- REFLUX -- tips has no spec number; its block is 3060-3069 (BOARD.md):
--   3060 tips published   a = spec (0 when none), b = tip count, c = registry slot
--   3061 tip told         a = the told addon's spec, b = the tip's index in its keys, c = its registry slot
--
-- READING -- for any subscriber:
--   RefluxTips.count()                how many addons published
--   RefluxTips.at(slot)               -> { addon, spec, keys, weights } (copies), nil past the last slot
--   RefluxTips.text(addon, key)       a tip's text in this locale   RefluxTips.name(addon) -> its NAME, else addon
--   RefluxTips.pick(asker[, state])   -> addon, key: the next tip from an addon other than `asker`, by the rules'
--                                     weighted no-repeat rotation, or nil when nobody else has one. state: a
--                                     table you keep; by default the carrier's
--   RefluxTips.rumor(asker)           -> lines: the carrier's voiced line and "Tip (Name): text", or the alone line
--   RefluxTips.carrier({ addon, voice = fn(name, own) -> line, alone = fn() -> line, state = table })
--                                     the addon that voices rumours (Ratatoskr Roll); its saved state keeps the
--                                     rotation across sessions
--   RefluxTips.claim_slash(key, { "/a", "/b" }, fn)  registers the commands unless another addon has one of them
--   RefluxTips.line(n)                1..n, a voice line for a carrier, from the rules
-- Client calls: GetLocale and time, both feature-tested, so RefluxCore still loads under bare Lua 5.1.

if RefluxTips then return end
local ADDON, ns = ...

local T = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/tips"]
if not T then
    print("|cffff5555REFLUX Core: the collection/tips module is missing; no tips.|r")
    return
end

local reg = T.tipsInit({})
local cats = {}                         -- addon -> its catalogue (a table, or a function of the key)
local carrier = nil
local own_state = T.tipsStateInit({})   -- the rotation when no carrier keeps one
local calls = 0

local function lookup(cat, key)
    if type(cat) == "function" then return cat(key) end
    if type(cat) ~= "table" then return nil end
    local t = cat[(GetLocale and GetLocale()) or "enUS"]
    return (t and t[key]) or (cat.enUS and cat.enUS[key])
end

local function L(key) return lookup(ns and ns.catalogue, key) or key end

local function seed(state)
    calls = calls + 1
    return ((time and time()) or 0) + calls * 7919 + (state.told or 0) * 104729
end

local API = {}

function API.publish(p)
    if type(p) ~= "table" or type(p.addon) ~= "string" or p.addon == "" then return 0 end
    local keys = {}
    if type(p.tips) == "table" then
        for _, k in ipairs(p.tips) do if type(k) == "string" then keys[#keys + 1] = k end end
    else
        local en = type(p.catalogue) == "table" and p.catalogue.enUS
        local i = 1
        while en and en["TIP_" .. i] do keys[#keys + 1] = "TIP_" .. i; i = i + 1 end
    end
    local ws = {}
    for i, k in ipairs(keys) do ws[i] = type(p.weights) == "table" and tonumber(p.weights[k]) or nil end
    local slot = T.tipsRegister(reg, p.addon, math.floor(tonumber(p.spec) or 0), keys, ws)
    if slot > 0 then cats[p.addon] = p.catalogue end
    return slot
end

function API.count() return #reg.list end

function API.at(slot)
    local e = reg.list[tonumber(slot) or 0]
    if not e then return nil end
    local keys, weights = {}, {}
    for i, k in ipairs(e.keys) do keys[i], weights[i] = k, e.weights[i] end
    return { addon = e.addon, spec = e.spec, keys = keys, weights = weights }
end

function API.text(addon, key) return lookup(cats[addon], key) end
function API.name(addon) return lookup(cats[addon], "NAME") or addon end

function API.carrier(c)
    if type(c) ~= "table" or type(c.addon) ~= "string" then return end
    c.state = T.tipsStateInit(type(c.state) == "table" and c.state or {})
    carrier = c
end

function API.pick(asker, state)
    state = T.tipsStateInit(type(state) == "table" and state or (carrier and carrier.state) or own_state)
    local r = T.tipsPick_(reg, state, asker or "", seed(state))
    if not r then return nil end
    local e = reg.list[r[1]]
    return e.addon, e.keys[r[2]]
end

function API.line(n) return T.tipsLine(seed(own_state), tonumber(n) or 0) end

function API.rumor(asker)
    local addon, key = API.pick(asker)
    if not addon then
        return { (carrier and carrier.alone and carrier.alone()) or string.format(L("ALONE"), API.name(asker or "")) }
    end
    local name, lines = API.name(addon), {}
    if carrier and carrier.voice then lines[1] = carrier.voice(name, addon == carrier.addon) end
    lines[#lines + 1] = string.format(L("TIP_LINE"), name, API.text(addon, key) or key)
    return lines
end

-- Register SlashCmdList[key] for every command in cmds, unless another addon already claimed any of them (two
-- handlers for one command would leave the client to pick one at random). True when registered.
function API.claim_slash(key, cmds, fn)
    if type(SlashCmdList) ~= "table" or type(key) ~= "string" or type(cmds) ~= "table" then return false end
    local G = _G or {}
    local want = {}
    for _, c in ipairs(cmds) do want[string.lower(c)] = true end
    for k in pairs(SlashCmdList) do
        if k ~= key then
            local i, s = 1, G["SLASH_" .. tostring(k) .. "1"]
            while s do
                if want[string.lower(tostring(s))] then return false end
                i = i + 1
                s = G["SLASH_" .. tostring(k) .. i]
            end
        end
    end
    for i, c in ipairs(cmds) do G["SLASH_" .. key .. i] = c end
    SlashCmdList[key] = fn
    return true
end

-- For tests: an empty registry (RefluxHost.reset's counterpart).
function API.reset() reg.list = {}; cats = {}; carrier = nil; own_state = T.tipsStateInit({}) end

RefluxTips = API

-- RefluxCore's own tips (runtime/wow/locales/reflux.lua).
API.publish({ addon = ADDON or "RefluxCore", spec = 0, catalogue = ns and ns.catalogue })
