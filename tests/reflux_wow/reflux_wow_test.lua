-- tests/reflux_wow/reflux_wow_test.lua -- REFLUX core on the WoW target, run under real Lua 5.1.
--
-- usage: lua5.1 reflux_wow_test.lua <reflux_host.lua> <reflux.lua> <cursor.lua> <idiv.lua>
-- The three .lua modules are `parena build` output of stdlib/reflux/reflux.prn, stdlib/reflux/cursor.prn
-- and tests/reflux_wow/idiv.prn. Every call below goes through the PARENA-emitted functions, never straight
-- to RefluxHost, so this is the same path a Collection addon takes. Expected values are hand-derived from
-- SHANKPIT/packages/reflux/reflux_runtime.c (capacity 256, overwrite oldest).

local host_path, reflux_path, cursor_path, idiv_path = arg[1], arg[2], arg[3], arg[4]
assert(host_path and reflux_path and cursor_path and idiv_path, "usage: reflux_wow_test.lua host reflux cursor idiv")

assert(_VERSION == "Lua 5.1", "must run under Lua 5.1 (WoW's dialect), got " .. tostring(_VERSION))

dofile(host_path)
local R = dofile(reflux_path)
local C = dofile(cursor_path)
local D = dofile(idiv_path)

local passed, failed = 0, 0
local function check(cond, what)
    if cond then passed = passed + 1 else failed = failed + 1; print("FAIL: " .. what) end
end
local function eq(got, want, what)
    check(got == want, what .. " (got " .. tostring(got) .. ", want " .. tostring(want) .. ")")
end

-- empty log
eq(R.refluxLogSize(), 0, "fresh log has no rows")
eq(C.refluxTotalDispatched(), 0, "fresh log has dispatched nothing")
eq(C.refluxOldestAbs(), 0, "fresh oldest_abs is 0")
eq(R.refluxActionTypeAt(0), -1, "type_at on an empty log is -1")
eq(R.refluxActionAAt(0), 0, "a_at on an empty log is 0")
eq(C.refluxIndexForAbs(0), -1, "abs 0 is not dispatched yet")

-- one row
R.refluxDispatch(1, 10, 20, 30)
eq(R.refluxLogSize(), 1, "one dispatch, one row")
eq(R.refluxActionTypeAt(0), 1, "row 0 type")
eq(R.refluxActionAAt(0), 10, "row 0 a")
eq(R.refluxActionBAt(0), 20, "row 0 b")
eq(R.refluxActionCAt(0), 30, "row 0 c")
eq(R.refluxActionTypeAt(1), -1, "index past the newest row is -1")
eq(R.refluxActionTypeAt(-1), -1, "negative index is -1")

-- wrap: 300 dispatches into a 256-row ring
RefluxHost.reset()
for i = 0, 299 do R.refluxDispatch(i, i * 2, i * 3, -i) end
eq(R.refluxLogSize(), 256, "log_size saturates at capacity")
eq(C.refluxTotalDispatched(), 300, "total keeps counting past capacity")
eq(C.refluxOldestAbs(), 44, "oldest retained row is 300 - 256")
eq(C.refluxIndexForAbs(43), -1, "an overwritten row maps to -1")
eq(C.refluxIndexForAbs(44), 0, "the oldest retained row is index 0")
eq(C.refluxIndexForAbs(299), 255, "the newest row is index 255")
eq(C.refluxIndexForAbs(300), -1, "a row not dispatched yet maps to -1")
eq(R.refluxActionTypeAt(0), 44, "index 0 holds abs row 44 after the wrap")
eq(R.refluxActionTypeAt(255), 299, "index 255 holds abs row 299")
eq(R.refluxActionTypeAt(256), -1, "index 256 is out of range")
eq(R.refluxActionAAt(C.refluxIndexForAbs(100)), 200, "a of abs row 100")
eq(R.refluxActionBAt(C.refluxIndexForAbs(100)), 300, "b of abs row 100")
eq(R.refluxActionCAt(C.refluxIndexForAbs(100)), -100, "c of abs row 100")

-- the cursor poll pattern from cursor.prn's header: from max(cursor, oldest) to total
local cursor, seen, ordered = 10, 0, true
local from = math.max(cursor, C.refluxOldestAbs())
local last = from - 1
for abs = from, C.refluxTotalDispatched() - 1 do
    local idx = C.refluxIndexForAbs(abs)
    local t = R.refluxActionTypeAt(idx)
    if t ~= last + 1 then ordered = false end
    last = t
    seen = seen + 1
end
eq(from - cursor, 34, "a cursor behind the ring is told 34 rows were lost")
eq(seen, 256, "the poll reads every retained row once")
check(ordered, "the poll reads rows oldest to newest")

-- payloads are integers by contract
RefluxHost.reset()
R.refluxDispatch(7, -2.7, 2.7, "5")
eq(R.refluxActionAAt(0), -2, "a negative fraction truncates toward zero")
eq(R.refluxActionBAt(0), 2, "a positive fraction truncates toward zero")
eq(R.refluxActionCAt(0), 5, "a numeric string is read as its number")

-- a second copy of the host must not split the log
dofile(host_path)
eq(R.refluxLogSize(), 1, "reloading the host keeps the existing log")

-- PARENA `/` is C's truncating division on the Lua 5.1 target too
eq(D.q(-70, 50), -1, "-70 / 50 truncates to -1 (Lua's floor would say -2)")
eq(D.q(70, 50), 1, "70 / 50 is 1")
eq(D.q(-100, 50), -2, "an exact negative quotient is unchanged")
eq(D.q(7, -2), -3, "7 / -2 truncates to -3")

print(string.format("%d passed, %d failed", passed, failed))
if failed > 0 then os.exit(1) end
