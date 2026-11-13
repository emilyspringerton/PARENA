-- tests/reflux_wow/interop_test.lua -- run the `parena build` output of interop.prn under real Lua 5.1 against
-- stand-in WoW globals. usage: lua5.1 interop_test.lua <interop.lua>
assert(_VERSION == "Lua 5.1", "must run under Lua 5.1 (WoW's dialect), got " .. tostring(_VERSION))

-- stand-ins for the client API. Values are arbitrary test data, not claims about WoW Forever.
C_Map = { GetBestMapForUnit = function(unit) if unit == "player" then return 1415 end return 7 end }
function GetBuildInfo() return "1.60.1", "70938", "Nov 1 2026", 99999 end

local M = dofile(assert(arg[1], "usage: interop_test.lua <interop.lua>"))

local passed, failed = 0, 0
local function eq(got, want, what)
    if got == want then passed = passed + 1
    else failed = failed + 1; print("FAIL: " .. what .. " (got " .. tostring(got) .. ", want " .. tostring(want) .. ")") end
end

eq(M.bestMap(), 1415, "dotted global call")
eq(M.mapOf("party1"), 7, "dotted global call with an argument")
eq(M.buildToc(), 99999, "select(4, GetBuildInfo())")
eq(M.hasCmap(), true, "lua/defined? sees an existing global")
eq(M.hasMissing(), false, "lua/defined? on a missing global is false, not an error")

local frame = { name = "RefluxCoreFrame" }
function frame:GetName() return self.name end
eq(M.frameName(frame), "RefluxCoreFrame", "method call passes the object as self")

eq(M.title("Emily", 60), "Emily (60)", "str concatenates strings and numbers")
eq(M.quoted(), 'say "hi"\n\tback\\slash', "quotes, newline, tab and backslash survive")
eq(M.unicode(), "Hallo Welt, 世界, мир", "UTF-8 bytes pass through unchanged")

local r = M.record(1415, 10)
eq(M.recordZone(r), 1415, "lua/get by name")
eq(r["not a name"], 7, "a non-name key is written with brackets")
eq(r["end"], 9, "a reserved-word key is written with brackets")
eq(#M.point(r), 2, "lua/array builds a sequence")
eq(M.point(r)[1], 10, "arrays are 1-based: first element at [1]")
eq(M.secondOf(r), 11, "lua/aget indexes raw")
eq(M.nilCheck(r), true, "lua/nil? on a missing field")

print(string.format("%d passed, %d failed", passed, failed))
if failed > 0 then os.exit(1) end
