-- tests/reflux_wow/forge_ascend_test.lua -- a kit planned in the forge and baked by `forge build` is what /ascend writes.
-- Loads the packaged MacroSync under real Lua 5.1 as Emily-Forever (a paladin) with a stubbed macro list.
-- usage: lua5.1 forge_ascend_test.lua <client dir>
assert(_VERSION == "Lua 5.1", "must run under Lua 5.1")
local client = assert(arg[1])
local passed, failed = 0, 0
local function eq(got, want, what)
    if got == want then passed = passed + 1
    else failed = failed + 1; print("FAIL: " .. what .. " (got " .. tostring(got) .. ", want " .. tostring(want) .. ")") end
end
local out, macros, frames = {}, {}, {}
local env = { print = function(...) local t = {}; for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end; out[#out + 1] = table.concat(t, " ") end }
for _, k in ipairs({ "pairs", "ipairs", "type", "tostring", "tonumber", "select", "string", "table", "math", "next", "error",
                     "assert", "pcall", "unpack", "setmetatable", "getmetatable", "rawget", "rawset" }) do env[k] = _G[k] end
env._G = env
env.time = function() return 1763000000 end
env.GetLocale = function() return "enUS" end
env.UnitName = function() return "Emily" end
env.GetRealmName = function() return "Forever" end
env.UnitClass = function() return "Paladin", "PALADIN" end
env.InCombatLockdown = function() return false end
env.MAX_CHARACTER_MACROS = 30
env.C_Spell = { GetSpellInfo = function() return nil end }
env.GetNumMacros = function() return 0, #macros end
env.GetMacroIndexByName = function(n) for i, m in ipairs(macros) do if m.name == n then return i end end return 0 end
env.GetMacroBody = function(i) return macros[i] and macros[i].body end
env.CreateMacro = function(n, _, b) macros[#macros + 1] = { name = n, body = b }; return #macros end
env.EditMacro = function(i, n, _, b) macros[i].body = b; return i end
env.DeleteMacro = function(i) table.remove(macros, i) end
env.SlashCmdList = {}
env.CreateFrame = function() local f = { events = {} }; function f:RegisterEvent(e) self.events[e] = true end
    function f:SetScript(_, fn) self.fn = fn end; frames[#frames + 1] = f; return f end
for _, addon in ipairs({ "RefluxCore", "MacroSync" }) do
    local dir = client .. "/AddOns/" .. addon
    local ns = {}
    for line in io.lines(dir .. "/" .. addon .. ".toc") do
        if line ~= "" and not line:match("^#") then local c = assert(loadfile(dir .. "/" .. line)); setfenv(c, env); c(addon, ns) end
    end
end
for _, f in ipairs(frames) do if f.events.ADDON_LOADED and f.fn then f.fn(f, "ADDON_LOADED", "MacroSync") end end
-- action bars: the forge planned PalJudge for slot 5, which holds nothing; slot 61 holds the player's own spell
local bars = { [61] = { kind = "spell", id = 1 } }
local cursor = nil
env.HasAction = function(s) return bars[s] ~= nil end
env.GetActionInfo = function(s) local b = bars[s]; if b then return b.kind, b.id end end
env.GetMacroInfo = function(i) return macros[i] and macros[i].name end
env.PickupMacro = function(n) for i, m in ipairs(macros) do if m.name == n then cursor = { kind = "macro", id = i } end end end
env.PickupAction = function(s) cursor = bars[s]; bars[s] = nil end
env.PlaceAction = function(s) local old = bars[s]; bars[s] = cursor; cursor = old end
env.ClearCursor = function() cursor = nil end
env.SlashCmdList.MACROSYNC("")
local function slot_of(name) for s, b in pairs(bars) do if b.kind == "macro" and macros[b.id] and macros[b.id].name == name then return s end end end
eq(slot_of("PalJudge"), 5, "a slot planned in the forge (forge place) is where /ascend puts the macro")
local ph = slot_of("PalHeal") or 0
eq(ph >= 62 and ph <= 72, true, "the rest fill the empty bottom-left slots, around the player's spell (PalHeal in " .. ph .. ")")
local first = nil; for s = 61, 72 do if bars[s] and bars[s].kind == "macro" then first = first or s end end
eq(first, 62, "starting at the first empty one")
eq(bars[61].kind, "spell", "the player's spell stays")
local by = {}
for _, m in ipairs(macros) do by[m.name] = m.body end
eq(by.PalExo, nil, "the dropped macro is not written")
eq(by.PalHeal, "#showtooltip\n/cast Holy Light", "the edited macro has the planned body")
eq(by.PalSpeed, "/cast [mod:alt] Divine Steed", "the added macro is written")
eq(by.PalJudge ~= nil, true, "the rest of the class set is written as usual")
eq(#macros, 16, "16 class macros - 1 dropped + 1 added")
eq(table.concat(out, "\n"):find("planned in the forge (3 changes", 1, true) ~= nil, true, "/ascend says the kit came from the forge")
eq(type(env.MacroSyncKits) == "table" and env.MacroSyncKits["Emily-Forever"] ~= nil, true, "the kits are published for the /chars screen")
eq(env.MacroSyncKits["Emily-Forever"].gear and env.MacroSyncKits["Emily-Forever"].gear[17], 2589, "with the gear plan, by slot number")
print(string.format("forge ascend: %d passed, %d failed", passed, failed))
os.exit(failed == 0 and 0 or 1)
