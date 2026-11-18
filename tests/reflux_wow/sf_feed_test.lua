-- tests/reflux_wow/sf_feed_test.lua -- the Collection feed: two accounts' SelfFoundDB, made by the real rules, saved
-- as SavedVariables files, read back by runtime/wow/sf_feed_read.lua into REFLUX rows + names.
-- usage: lua5.1 sf_feed_test.lua <compiled sf.lua (reflux.prn + selffound.prn)> <scratch dir>
assert(_VERSION == "Lua 5.1", "must run under Lua 5.1")
local sf_path, dir = assert(arg[1]), assert(arg[2])

local passed, failed = 0, 0
local function eq(got, want, what)
    if got == want then passed = passed + 1
    else failed = failed + 1; print("FAIL: " .. what .. " (got " .. tostring(got) .. ", want " .. tostring(want) .. ")") end
end

dofile("runtime/wow/reflux_host.lua")
local S = dofile(sf_path)

-- SavedVariables the way the client writes them: `VAR = { ... }` (keys sorted so the file is stable).
local function ser(v, ind)
    if type(v) == "table" then
        local keys = {}
        for k in pairs(v) do keys[#keys + 1] = k end
        table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
        local out = { "{\n" }
        for _, k in ipairs(keys) do
            local kk = type(k) == "number" and ("[" .. k .. "]") or ("[" .. string.format("%q", k) .. "]")
            out[#out + 1] = ind .. "  " .. kk .. " = " .. ser(v[k], ind .. "  ") .. ",\n"
        end
        out[#out + 1] = ind .. "}"
        return table.concat(out)
    elseif type(v) == "string" then return string.format("%q", v)
    else return tostring(v) end
end
local function save(path, db)
    local f = assert(io.open(path, "wb")); f:write("SelfFoundDB = ", ser(db, ""), "\n"); f:close()
end

-- account 1: Ingrid (level 1, self-found, found a vendor), Bjorn (broken by a trade at t=1763400100)
local a1 = S.sfInit({})
S.sfRecord(a1, "Ingrid-Forever", 1, S.sfKLoot(), 2589, 1, 0, 1763400000, nil, nil, nil)
S.sfVendor(a1, 3881, "Grimtak", 1763400000, 1420, 0.5, 0.25, 1)
S.sfVendorItem(a1, 3881, 159, 25); S.sfVendorItem(a1, 3881, 4540, 25)
S.sfRecord(a1, "Bjorn-Forever", 12, S.sfKTrade(), 1, 50, 0, 1763400100, nil, nil, nil)
save(dir .. "/acct1.lua", a1)
-- account 2: Cora, self-found since tracking (first seen at level 30), two transactions
local a2 = S.sfInit({})
S.sfRecord(a2, "Cora-Forever", 30, S.sfKVendorBuy(), 159, 5, 3881, 1763400200, nil, nil, nil)
S.sfRecord(a2, "Cora-Forever", 30, S.sfKAhScan(), 0, 0, 0, 1763400300, nil, nil, nil)
save(dir .. "/acct2.lua", a2)

local out = dir .. "/feed.txt"
arg = { sf_path, "SelfFoundDB", out, dir .. "/acct1.lua", dir .. "/acct2.lua" }
dofile("runtime/wow/sf_feed_read.lua")

local names, rows, header = {}, {}, nil
for line in io.lines(out) do
    if not header then header = line
    else
        local id, text = line:match("^name (%d+) (.*)$")
        if id then names[tonumber(id)] = text
        else
            local t, a, b, c = line:match("^row (%-?%d+) (%-?%d+) (%-?%d+) (%-?%d+)$")
            assert(t, "bad feed line: " .. line)
            rows[#rows + 1] = { tonumber(t), tonumber(a), tonumber(b), tonumber(c) }
        end
    end
end
eq(header, "collection-feed 1", "versioned header")

local by = {}
for _, r in ipairs(rows) do
    if r[1] == 3072 then by[names[r[2]]] = { status = r[3], n = r[4] } end
end
eq(by["Ingrid-Forever"] and by["Ingrid-Forever"].status, 2, "Ingrid: self-found since level 1")
eq(by["Bjorn-Forever"] and by["Bjorn-Forever"].status, 0, "Bjorn: broken")
eq(by["Cora-Forever"] and by["Cora-Forever"].status, 1, "Cora (second account): self-found since tracking")
eq(by["Cora-Forever"] and by["Cora-Forever"].n, 2, "with her transaction count")

local broken, vendor, summary
for _, r in ipairs(rows) do
    if r[1] == 3073 then broken = r elseif r[1] == 3074 then vendor = r elseif r[1] == 3075 then summary = r end
end
eq(broken and names[broken[2]], "Bjorn-Forever", "a broken row for the broken character")
eq(broken and broken[3], 8, "broken by a trade")
eq(broken and broken[4], 1763400100, "and when")
eq(vendor and names[vendor[2]], "Grimtak", "the vendor, by name")
eq(vendor and vendor[3], 3881, "its npc id")
eq(vendor and vendor[4], 2, "its item count")
eq(summary and summary[2], 3, "summary: three characters across both accounts")
eq(summary and summary[3], 2, "two still self-found")
eq(summary and summary[4], 1, "one vendor")
eq(rows[#rows][1], 3075, "the summary comes last")

print(string.format("sf_feed: %d passed, %d failed", passed, failed))
os.exit(failed == 0 and 0 or 1)
