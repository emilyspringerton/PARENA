-- runtime/collection/store_ingest.lua -- host glue for the Collection's event store (spec Step 7). The store's rules
-- are PARENA (stdlib/collection/eventstore.prn, built to es.lua); this file only reads and writes file contents.
-- Directories, the append lock and the journal listing are done by scripts/collection-store-ingest.sh, so this
-- runs the same under Windows (cmd) and Linux.
--
-- usage: lua5.1 store_ingest.lua <es.lua> <store root> <events.ndjson> [<journal.ndjson>...]
--   the journal arguments are every existing <root>/events/*.ndjson, sorted (Go's journalPaths order).
-- Prints one report line. Exit 0 ok; 1 nothing could be done (bad state file, unreadable input, write error).
assert(_VERSION == "Lua 5.1", "run under Lua 5.1, got " .. tostring(_VERSION))
local E = dofile(assert(arg[1], "es.lua"))
local root, events_path = assert(arg[2], "store root"), assert(arg[3], "events file")
local now = os.time()

local function fail(msg) io.stderr:write("collection-store: ", msg, "\n"); os.exit(1) end

local function read_all(path)
    local f = io.open(path, "rb")
    if not f then return nil end
    local s = f:read("*a")
    f:close()
    return s
end

-- Scan the existing journals: every id already stored, and the highest complete record's sequence.
local known, journal_max, partial = {}, 0, 0
for i = 4, #arg do
    local f = io.open(arg[i], "rb")
    if not f then fail("cannot read journal " .. arg[i]) end
    for line in f:lines() do
        if line:match("%S") then
            local seq = E.esRecordSequence(line)
            if seq then
                if seq > journal_max then journal_max = seq end
                local id = E.esRecordId(line)
                if id then known[id] = true end
            else
                partial = partial + 1
            end
        end
    end
    f:close()
end

local state_path = root .. "/state/latest-sequence"
local latest = E.esLatest(read_all(state_path), journal_max)
if latest < 0 then fail(state_path .. " does not hold a sequence number; refusing to guess one (Go does the same)") end

local f = io.open(events_path, "rb")
if not f then fail("cannot read events file " .. events_path) end
local lines, read, dup, bad = {}, 0, 0, 0
local first = latest + 1
for line in f:lines() do
    if line:match("%S") then
        read = read + 1
        if E.esEventOk(line) ~= 1 then
            bad = bad + 1
        else
            local id = E.esEventId(line)
            if known[id] then
                dup = dup + 1
            else
                known[id] = true
                latest = latest + 1
                lines[#lines + 1] = E.esRecordLine(latest, line, now)
            end
        end
    end
end
f:close()

if #lines > 0 then
    local journal = root .. "/events/" .. E.esJournalName(now)
    local out = io.open(journal, "ab")
    if not out then fail("cannot append to " .. journal) end
    local ok = out:write(table.concat(lines, "\n"), "\n")
    out:flush()
    out:close()
    if not ok then fail("write failed on " .. journal) end
    -- persistLatestSequence: temp file, then rename over the old one. Windows' rename will not replace an
    -- existing file, so the old one is removed first -- safe here because the wrapper holds the append lock.
    local tmp = state_path .. ".tmp"
    local s = io.open(tmp, "wb")
    if not s then fail("cannot write " .. tmp) end
    s:write(string.format("%d\n", latest))
    s:close()
    if not os.rename(tmp, state_path) then
        os.remove(state_path)
        if not os.rename(tmp, state_path) then fail("cannot replace " .. state_path) end
    end
end

print(string.format("%s: %d events read, %d appended%s, %d already stored, %d invalid; store at sequence %d%s",
    events_path, read, #lines, #lines > 0 and string.format(" (sequence %d-%d)", first, latest) or "",
    dup, bad, latest, partial > 0 and string.format("; %d partial journal line(s) skipped", partial) or ""))
