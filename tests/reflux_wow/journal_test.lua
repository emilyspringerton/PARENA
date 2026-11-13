-- tests/reflux_wow/journal_test.lua -- stdlib/collection/journal.prn under real Lua 5.1, against the real WoW
-- REFLUX host. usage: lua5.1 journal_test.lua <reflux_host.lua> <journal.lua>
-- (journal.lua = parena build of reflux.prn + cursor.prn + journal.prn as one module)
assert(_VERSION == "Lua 5.1", "must run under Lua 5.1 (WoW's dialect), got " .. tostring(_VERSION))
dofile(assert(arg[1]))
local J = dofile(assert(arg[2]))

local passed, failed = 0, 0
local function eq(got, want, what)
    if got == want then passed = passed + 1
    else failed = failed + 1; print("FAIL: " .. what .. " (got " .. tostring(got) .. ", want " .. tostring(want) .. ")") end
end

-- a fresh SavedVariables table, as the client hands over on first login (nil -> {})
local db = J.journalInit({})
eq(db.v, 1, "init stamps schema v1")
eq(db.nextSeq, 1, "init starts seq at 1")
eq(db.cursor, 0, "init starts the cursor at 0")
eq(#db.rows, 0, "init gives an empty row list")

-- init never resets existing data
db.nextSeq = 9
J.journalInit(db)
eq(db.nextSeq, 9, "a second init keeps existing data")
db.nextSeq = 1

-- a pure record
local r = J.journalRecord(3, 26, 1, 2, 3, "Emily-Forever", 1763000000, 1415, 0.25, 0.75)
eq(r.v, 1, "record v")
eq(r.id, "Emily-Forever:3", "record id is character:seq")
eq(r.type, 26, "record type")
eq(r.source, "reflux", "record source")
eq(r.time, 1763000000, "record time")
eq(r.zone, 1415, "record zone")
eq(r.x, 0.25, "record x")
eq(r.y, 0.75, "record y")
eq(r.payload[1] + r.payload[2] + r.payload[3], 6, "record payload a,b,c")
local r2 = J.journalRecord(4, 23, 0, 0, 0, "Emily-Forever", 1, nil, nil, nil)
eq(r2.zone, nil, "zone may be nil when the client cannot say")

-- poll: three dispatches become three records, in order
RefluxHost.reset()
J.refluxDispatch(23, 100, 0, 0)
J.refluxDispatch(24, 100, 50, 0)
J.refluxDispatch(25, 7, 1, 0)
eq(J.journalPoll(db, "Emily-Forever", 1000, 1415, 0.5, 0.5), 3, "first poll appends 3")
eq(#db.rows, 3, "3 rows stored")
eq(db.rows[1].type, 23, "row 1 is the first dispatch")
eq(db.rows[3].type, 25, "row 3 is the last dispatch")
eq(db.rows[2].payload[2], 50, "payload copied")
eq(db.rows[3].id, "Emily-Forever:3", "ids follow seq")
eq(db.cursor, 3, "cursor moved to total")
eq(J.journalPoll(db, "Emily-Forever", 1001, 1415, 0.5, 0.5), 0, "a poll with nothing new appends nothing")

-- more rows later continue the sequence
J.refluxDispatch(26, 0, 0, 0)
eq(J.journalPoll(db, "Emily-Forever", 1002, 1416, 0.1, 0.2), 1, "a later poll appends only the new row")
eq(db.rows[4].seq, 4, "seq continues")
eq(db.rows[4].zone, 1416, "context is the poll's, not the first one's")
eq(db.lost, 0, "nothing lost so far")

-- the ring overwrites rows before the poll: say how many were lost, keep what is retained
for i = 1, 300 do J.refluxDispatch(51, i, 0, 0) end   -- total 304, oldest retained 48, cursor was 4
eq(J.journalPoll(db, "Emily-Forever", 1003, 1416, 0, 0), 256, "a poll after a wrap copies the 256 retained rows")
eq(db.lost, 44, "and records the 44 overwritten rows as lost (48 - 4)")
eq(db.rows[5].payload[1], 45, "the first copied row is abs 48, the 45th of the 300")
eq(db.rows[260].payload[1], 300, "the last copied row is the newest")
eq(db.nextSeq, 261, "seq never skips even when REFLUX rows were lost")

print(string.format("%d passed, %d failed", passed, failed))
if failed > 0 then os.exit(1) end
