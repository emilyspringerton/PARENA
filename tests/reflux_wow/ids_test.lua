-- tests/reflux_wow/ids_test.lua -- stdlib/collection/ids.prn under real Lua 5.1.
-- usage: lua5.1 ids_test.lua <ids.lua>
assert(_VERSION == "Lua 5.1", "must run under Lua 5.1 (WoW's dialect), got " .. tostring(_VERSION))
local I = dofile(assert(arg[1]))

local passed, failed = 0, 0
local function eq(got, want, what)
    if got == want then passed = passed + 1
    else failed = failed + 1; print("FAIL: " .. what .. " (got " .. tostring(got) .. ", want " .. tostring(want) .. ")") end
end

eq(I.collectionActionFirst(), 2000, "range starts at 2000")
eq(I.collectionActionLast(), 2999, "range ends at 2999")
eq(I.collectionAddonFirst(1), 2000, "addon 1 owns 2000..")
eq(I.collectionAddonFirst(26), 2250, "addon 26 (Position) owns 2250..")
eq(I.collectionAddonFirst(74), 2730, "addon 74 (AH Scan) owns 2730..")
eq(I.collectionAddonFirst(100), 2990, "addon 100 owns 2990..")
eq(I.collectionAddonFirst(0), -1, "addon 0 is not an addon")
eq(I.collectionAddonFirst(101), -1, "addon 101 is not an addon")
eq(I.collectionActionType(26, 0), 2250, "addon 26 type 0")
eq(I.collectionActionType(26, 9), 2259, "addon 26 type 9")
eq(I.collectionActionType(26, 10), -1, "only ten types per addon")
eq(I.collectionActionType(26, -1), -1, "no negative k")
eq(I.collectionActionType(0, 0), -1, "no type for a non-addon")
eq(I.collectionAddonOf(2250), 26, "2250 belongs to addon 26")
eq(I.collectionAddonOf(2259), 26, "2259 belongs to addon 26")
eq(I.collectionAddonOf(2260), 27, "2260 belongs to addon 27")
eq(I.collectionAddonOf(2000), 1, "2000 belongs to addon 1")
eq(I.collectionAddonOf(2999), 100, "2999 belongs to addon 100")
eq(I.collectionAddonOf(1999), 0, "1999 is not the Collection's")
eq(I.collectionAddonOf(3000), 0, "3000 is not the Collection's")
eq(I.collectionAddonOf(1100), 0, "BRAWLPIT's family is not the Collection's")
eq(I.collectionIsAction(2500), 1, "2500 is in range")
eq(I.collectionIsAction(128), 0, "SHANKPIT's citizen family is not")

-- every type maps back to the addon that owns it, and no two addons share one
local owner, clash = {}, 0
for n = 1, 100 do
    for k = 0, 9 do
        local t = I.collectionActionType(n, k)
        if owner[t] then clash = clash + 1 end
        owner[t] = n
        if I.collectionAddonOf(t) ~= n then clash = clash + 1 end
    end
end
eq(clash, 0, "1000 types, each owned by exactly one addon, each round-trips")

print(string.format("%d passed, %d failed", passed, failed))
if failed > 0 then os.exit(1) end
