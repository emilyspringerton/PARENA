-- tests/reflux_wow/journal_reader_test.lua -- runtime/wow/journal_reader.lua (COV-6b) under real Lua 5.1, fed
-- SavedVariables files written from journals the real PARENA-built journal module produced.
-- usage: lua5.1 journal_reader_test.lua <reflux_host.lua> <journal.lua> <journal_reader.lua> <ids.lua> <workdir> <wtf_sv_path>
--   <wtf_sv_path>: where to leave a clean journal for the wrapper-script check that runs after this test.
assert(_VERSION == "Lua 5.1", "must run under Lua 5.1, got " .. tostring(_VERSION))
dofile(assert(arg[1]))
local J = dofile(assert(arg[2]))
local R = dofile(assert(arg[3]))
local I = dofile(assert(arg[4]))
local work = assert(arg[5])
local wtf_sv = assert(arg[6])

local passed, failed = 0, 0
local function eq(got, want, what)
    if got == want then passed = passed + 1
    else failed = failed + 1; print("FAIL: " .. what .. " (got " .. tostring(got) .. ", want " .. tostring(want) .. ")") end
end

-- The client's SavedVariables shape: NAME = { ["key"] = value, ... }, keys sorted here for stable files.
local function ser(v, indent)
    local t = type(v)
    if t == "string" then return string.format("%q", v) end
    if t == "number" then return (v == math.floor(v)) and string.format("%d", v) or string.format("%.17g", v) end
    if t == "boolean" then return tostring(v) end
    if t ~= "table" then return "nil" end
    local keys = {}
    for k in pairs(v) do keys[#keys + 1] = k end
    table.sort(keys, function(a, b)
        if type(a) == type(b) then return a < b end
        return type(a) == "number"
    end)
    local pad = string.rep("\t", indent + 1)
    local out = { "{\n" }
    for _, k in ipairs(keys) do
        local key = type(k) == "number" and ("[" .. k .. "]") or ("[" .. string.format("%q", k) .. "]")
        out[#out + 1] = pad .. key .. " = " .. ser(v[k], indent + 1) .. ",\n"
    end
    out[#out + 1] = string.rep("\t", indent) .. "}"
    return table.concat(out)
end
local function write(path, text) local f = assert(io.open(path, "wb")); f:write(text); f:close() end
local function write_sv(path, db) write(path, "\nCollectionJournalDB = " .. ser(db, 0) .. "\n") end
local function lines(path)
    local out, f = {}, io.open(path, "rb")
    if not f then return out end
    for l in f:lines() do out[#out + 1] = l end
    f:close()
    return out
end

local sv = work .. "/CollectionJournal.lua"
local events = work .. "/events.ndjson"
os.remove(events)
local function read(path) return R.read_file({ sv_path = path or sv, var = "CollectionJournalDB", events_path = events,
                                               account = "1234#1", classify = I.collectionAddonOf }) end

-- a journal with three rows: two from addon 26 (Position, 2250..2259) and one from outside the range
RefluxHost.reset()
local db = J.journalInit({})
J.refluxDispatch(2250, 10, 20, 30)
J.refluxDispatch(2251, 1, 2, 3)
J.refluxDispatch(23, 0, 0, 0)
J.journalPoll(db, "Emily-Forever", 1763000000, 1415, 0.25, 0.5)
write_sv(sv, db)

local r = assert(read())
eq(r.read, 3, "three rows read")
eq(r.appended, 3, "three appended")
eq(#r.gaps, 0, "no gap on a journal that starts at seq 1")
local L = lines(events)
eq(#L, 3, "three event lines")
eq(L[1]:match('^{"id":"Emily%-Forever:1","type":"reflux%.2250","source":"collection/addon%-26",') ~= nil, true,
   "line 1: id, type and the source addon from ids.prn")
eq(L[1]:match('"occurred_at":"2025%-11%-13T02:13:20Z"') ~= nil, true, "occurred_at is the record's time in RFC 3339 UTC")
eq(L[1]:match('"partition_key":"Emily%-Forever"') ~= nil, true, "partition key is the character")
eq(L[1]:match('"data":{"v":1,"seq":1,"type":2250,"character":"Emily%-Forever","zone":1415,"x":0%.25,"y":0%.5,"payload":%[10,20,30%]}') ~= nil,
   true, "data carries the whole v1 record")
eq(L[1]:match('"metadata":{"reader":"parena/journal_reader","account":"1234#1"}}$') ~= nil, true, "metadata names reader and account")
eq(L[3]:match('"source":"reflux"') ~= nil, true, "a type outside the Collection keeps source reflux")
eq(L[1]:find("ingested_at", 1, true), nil, "ingested_at is left to the store's Append")

-- reading the same file again appends nothing
r = assert(read())
eq(r.appended, 0, "a second read appends nothing")
eq(r.duplicates, 3, "and counts the three it already had")
eq(#lines(events), 3, "the file is unchanged")

-- more play, then the in-game cap trims rows this reader never saw: a gap, said out loud
for i = 1, 200 do J.refluxDispatch(2730, i, 0, 0) end
J.journalPoll(db, "Emily-Forever", 1763000100, 1415, 0, 0)
eq(J.journalTrim(db, 100), 103, "the cap keeps 100 of 203")
write_sv(sv, db)
r = assert(read())
eq(r.appended, 100, "the 100 kept rows are appended")
eq(#r.gaps, 1, "one gap reported")
eq(r.gaps[1] and r.gaps[1].from, 4, "gap starts after the last stored seq")
eq(r.gaps[1] and r.gaps[1].to, 103, "gap ends before firstSeq")
eq(r.trimmed, 103, "the report carries the journal's trimmed count")
eq(R.format_report("x", r):find("GAP Emily-Forever: seqs 4-103", 1, true) ~= nil, true, "the printed report names the gap")
L = lines(events)
eq(L[4]:match('"source":"collection/addon%-74"') ~= nil, true, "AH Scan's types map to addon 74")

-- a first read of an already-trimmed journal is a gap too
os.remove(events)
r = assert(read())
eq(r.gaps[1] and r.gaps[1].from, 1, "on a first read the gap starts at seq 1")

-- escaping: quotes, backslashes, control bytes are escaped; UTF-8 passes through; dedup still holds
os.remove(events)
local odd = J.journalInit({})
J.journalAppend(odd, 2250, 1, 2, 3, 'Ém"ï\\ly-Réalm\t', 5, nil, nil, nil)
write_sv(sv, odd)
r = assert(read())
L = lines(events)
eq(L[1]:match('^{"id":"Ém\\"ï\\\\ly%-Réalm\\t:1"') ~= nil, true, "id escaped, UTF-8 kept")
eq(L[1]:match('"zone":null,"x":null,"y":null') ~= nil, true, "unknown zone and position are null")
r = assert(read())
eq(r.appended, 0, "an escaped id is still recognised on the next read")

-- a malformed row is counted, not written
local bad = J.journalInit({})
J.journalAppend(bad, 2250, 0, 0, 0, "Emily-Forever", 5, nil, nil, nil)
bad.rows[1].seq = 99
write_sv(sv, bad)
os.remove(events)
r = assert(read())
eq(r.malformed, 1, "a row whose seq disagrees with its slot is malformed")
eq(r.appended, 0, "and is not written")

-- the file is data: no globals, an instruction budget, no binary chunks, only the named variable, schema v1
local function refused(text, what)
    write(work .. "/hostile.lua", text)
    local ok, err = read(work .. "/hostile.lua")
    eq(ok, nil, what)
    return err or ""
end
refused("CollectionJournalDB = os.exit(9)", "os is not reachable from the file")
refused("CollectionJournalDB = io.open('x','w')", "io is not reachable from the file")
eq(refused("while true do end", "a runaway loop is stopped"):find("budget", 1, true) ~= nil, true, "by the instruction budget")
refused(string.dump(function() end), "a precompiled chunk is refused")
refused("Other = {}", "a file without the variable is refused")
refused("CollectionJournalDB = { v = 2, rows = {}, nextSeq = 1 }", "an unknown schema version is refused")
refused("CollectionJournalDB = {", "a truncated file is refused")
local _, missing = read(work .. "/does-not-exist.lua")
eq(missing ~= nil, true, "a missing file is an error, not a crash")
eq(#lines(events), 0, "no refused file wrote anything")

-- leave a clean journal (2 rows, seq 1..2) in the fake WTF tree for the wrapper check
RefluxHost.reset()
local clean = J.journalInit({})
J.refluxDispatch(2250, 1, 1, 1)
J.refluxDispatch(2250, 2, 2, 2)
J.journalPoll(clean, "Emily-Forever", 1763000000, 1415, 0.1, 0.1)
write_sv(wtf_sv, clean)
write(wtf_sv .. ".bak", "this .bak must never be read")

print(string.format("%d passed, %d failed", passed, failed))
if failed > 0 then os.exit(1) end
