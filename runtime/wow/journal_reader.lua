-- runtime/wow/journal_reader.lua -- the OUTSIDE half of the Collection's SavedVariables bridge (spec Step 6,
-- COV-6b). Runs on the founder's machine under plain Lua 5.1, never in the client.
--
-- The in-game half (stdlib/collection/journal.prn) keeps a per-character table the client writes to
--   <WTF>/Account/<account>/<realm>/<character>/SavedVariables/<Addon>.lua
-- on logout and /reload. This reads one such file and appends each journal row it has not seen yet to an
-- NDJSON file of PRRJECT_FATBABY eventstore Events (eventstore/types.go: id, type, source, occurred_at,
-- partition_key, data, metadata; the store assigns sequence and ingested_at on Append -- Disputes D-2).
--
-- Rules it keeps:
--   * the SavedVariables file is DATA. It is loaded with no globals at all (no io, os, require), under an
--     instruction budget, and only the one named variable is read out of it.
--   * dedup is on id = "<character>:<seq>", against every id already in the events file, so reading the same
--     file twice appends nothing.
--   * nothing missing is passed over silently: rows the in-game size cap removed before they were read show
--     up as a gap between the highest seq already stored for that character and firstSeq; REFLUX rows that
--     were never journalled are the table's own `lost` count. Both are returned and printed.
--
-- Usage (library): local R = dofile("journal_reader.lua"); R.read_file(opts) -> report | nil, err
-- The command-line entry point is scripts/collection-journal-read.sh.

local R = {}

local MAX_INSTRUCTIONS = 50000000   -- a SavedVariables file is plain assignments; this is far above any real one

-- JSON -------------------------------------------------------------------------------------------------------

local function json_string(s)
    s = tostring(s)
    s = s:gsub('[%c"\\]', function(c)
        if c == '"' then return '\\"' end
        if c == '\\' then return '\\\\' end
        if c == '\n' then return '\\n' end
        if c == '\r' then return '\\r' end
        if c == '\t' then return '\\t' end
        return string.format('\\u%04x', c:byte())
    end)
    return '"' .. s .. '"'          -- bytes >= 0x80 pass through: the client writes UTF-8
end

local function json_number(n)
    if type(n) ~= "number" or n ~= n or n == math.huge or n == -math.huge then return "null" end
    if n == math.floor(n) and math.abs(n) < 2^53 then return string.format("%d", n) end
    return string.format("%.17g", n)
end

local function json_value(v)
    local t = type(v)
    if t == "nil" then return "null" end
    if t == "number" then return json_number(v) end
    if t == "string" then return json_string(v) end
    if t == "boolean" then return tostring(v) end
    return "null"                    -- functions/tables are never expected in a field this writes
end

-- Fields in a fixed order, so the same row always serialises to the same bytes.
local function json_object(pairs_list)
    local out = {}
    for i = 1, #pairs_list, 2 do
        out[#out + 1] = json_string(pairs_list[i]) .. ":" .. pairs_list[i + 1]
    end
    return "{" .. table.concat(out, ",") .. "}"
end

R.json_string = json_string

-- Loading ----------------------------------------------------------------------------------------------------

-- Run a SavedVariables chunk with no globals and return the value of `var`, or nil, err.
function R.load_saved_variables(text, var, chunkname)
    if type(text) ~= "string" then return nil, "no file contents" end
    if text:sub(1, 1) == "\27" then return nil, "refusing a precompiled (binary) chunk" end
    local chunk, err = loadstring(text, chunkname or "=SavedVariables")
    if not chunk then return nil, "not loadable Lua: " .. tostring(err) end
    local env = {}
    setfenv(chunk, env)
    local function budget() error("instruction budget exceeded", 2) end
    debug.sethook(budget, "", MAX_INSTRUCTIONS)
    local ok, run_err = pcall(chunk)
    debug.sethook()
    if not ok then return nil, "running it failed: " .. tostring(run_err) end
    local db = env[var]
    if db == nil then return nil, "the file does not define " .. var end
    if type(db) ~= "table" then return nil, var .. " is a " .. type(db) .. ", not a table" end
    return db
end

-- The events file ---------------------------------------------------------------------------------------------

-- Every id already stored, and the highest seq stored per character (ids are "<character>:<seq>"; a character
-- name-realm may itself contain ':' only in theory, so the seq is taken after the LAST colon).
function R.scan_events(path)
    local ids, max_seq = {}, {}
    local f = io.open(path, "rb")
    if not f then return ids, max_seq end
    for line in f:lines() do
        local id = line:match('^{"id":"(.-[^\\])"')
        if id then
            ids[id] = true
            local character, seq = id:match("^(.*):(%d+)$")
            seq = tonumber(seq)
            if character and seq and seq > (max_seq[character] or 0) then max_seq[character] = seq end
        end
    end
    f:close()
    return ids, max_seq
end

-- One journal row -> one eventstore Event line. `classify(type)` gives the addon number for a Collection
-- action type (stdlib/collection/ids.prn's collection-addon-of), 0 for anything else.
function R.event_line(row, account, classify)
    local addon = classify and classify(row.type) or 0
    local source = addon > 0 and ("collection/addon-" .. addon) or tostring(row.source or "reflux")
    local payload = type(row.payload) == "table" and row.payload or {}
    local data = json_object({
        "v", json_value(row.v),
        "seq", json_value(row.seq),
        "type", json_value(row.type),
        "character", json_value(row.character),
        "zone", json_value(row.zone),
        "x", json_value(row.x),
        "y", json_value(row.y),
        "payload", "[" .. json_value(payload[1]) .. "," .. json_value(payload[2]) .. "," .. json_value(payload[3]) .. "]",
    })
    local meta = { "reader", json_string("parena/journal_reader") }
    if account then meta[#meta + 1] = "account"; meta[#meta + 1] = json_string(account) end
    local occurred = type(row.time) == "number" and os.date("!%Y-%m-%dT%H:%M:%SZ", row.time) or nil
    local fields = {
        "id", json_string(row.id),
        "type", json_string("reflux." .. tostring(row.type)),
        "source", json_string(source),
    }
    if occurred then fields[#fields + 1] = "occurred_at"; fields[#fields + 1] = json_string(occurred) end
    fields[#fields + 1] = "partition_key"; fields[#fields + 1] = json_string(row.character)
    fields[#fields + 1] = "data"; fields[#fields + 1] = data
    fields[#fields + 1] = "metadata"; fields[#fields + 1] = json_object(meta)
    return json_object(fields)
end

-- The whole read. opts: sv_path, var, events_path, account (optional), classify (optional).
-- Returns a report table, or nil, err with nothing written.
function R.read_file(opts)
    local f = io.open(opts.sv_path, "rb")
    if not f then return nil, "cannot open " .. tostring(opts.sv_path) end
    local text = f:read("*a")
    f:close()
    local db, err = R.load_saved_variables(text, opts.var, "@" .. opts.sv_path)
    if not db then return nil, opts.sv_path .. ": " .. err end
    if db.v ~= 1 then return nil, opts.sv_path .. ": journal schema v" .. tostring(db.v) .. " is not one this reader knows (1)" end
    local rows, first, nxt = db.rows, tonumber(db.firstSeq) or 1, tonumber(db.nextSeq)
    if type(rows) ~= "table" or not nxt then return nil, opts.sv_path .. ": no rows/nextSeq" end

    local ids, max_seq = R.scan_events(opts.events_path)
    local report = { read = 0, appended = 0, duplicates = 0, malformed = 0, gaps = {},
                     lost = tonumber(db.lost) or 0, trimmed = tonumber(db.trimmed) or 0 }
    local lines, seen_chars = {}, {}
    for seq = first, nxt - 1 do
        local row = rows[seq]
        if type(row) ~= "table" or type(row.id) ~= "string" or type(row.character) ~= "string" or row.seq ~= seq then
            report.malformed = report.malformed + 1
        else
            report.read = report.read + 1
            if not seen_chars[row.character] then
                seen_chars[row.character] = true
                local have = max_seq[row.character] or 0
                -- seqs <= have are stored; have+1 .. first-1 were trimmed in game before any read reached them
                -- (on a first read have is 0, so a journal already trimmed past seq 1 is a gap too)
                if first > have + 1 then
                    report.gaps[#report.gaps + 1] = { character = row.character, from = have + 1, to = first - 1 }
                end
            end
            local key = json_string(row.id):sub(2, -2)    -- ids are compared in their stored (escaped) form
            if ids[key] then
                report.duplicates = report.duplicates + 1
            else
                ids[key] = true
                lines[#lines + 1] = R.event_line(row, opts.account, opts.classify)
            end
        end
    end
    if #lines > 0 then
        local out, oerr = io.open(opts.events_path, "ab")
        if not out then return nil, "cannot append to " .. tostring(opts.events_path) .. ": " .. tostring(oerr) end
        out:write(table.concat(lines, "\n"), "\n")
        out:close()
    end
    report.appended = #lines
    return report
end

function R.format_report(path, r)
    local s = string.format("%s: %d rows read, %d appended, %d already stored, %d malformed, %d lost in REFLUX, %d trimmed in game",
        path, r.read, r.appended, r.duplicates, r.malformed, r.lost, r.trimmed)
    for _, g in ipairs(r.gaps) do
        s = s .. string.format("\n  GAP %s: seqs %d-%d were trimmed in game before this reader saw them", g.character, g.from, g.to)
    end
    return s
end

return R
