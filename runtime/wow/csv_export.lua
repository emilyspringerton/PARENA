-- runtime/wow/csv_export.lua -- the Collection's CSV export (founder, 2026-11-13: "save the whole array of character
-- data and bag data into a csv so we can track our character progression"). Runs outside the game under Lua 5.1,
-- after logging out (the client writes SavedVariables on logout and /reload only).
--
-- Reads, with the journal reader's sandboxed loader (no globals, an instruction budget):
--   every Account/<account>/SavedVariables/<board addon>.lua   the Total Character Board's snapshots (addon 1)
--   every Account/<account>/SavedVariables/<Fafnir addon>.lua  Fafnir's Bed's bags, bank, mail and gold (addon 2)
-- Writes, into the output folder:
--   characters.csv        one row per snapshot ever taken: the progression series (rewritten each run)
--   bags.csv              what every character holds now, by location (rewritten each run)
--   bags_history.csv      bags.csv's rows, APPENDED with the export time whenever a character's holdings were
--                         refreshed since the last export -- run it after each session and it becomes a history
--   inventory_net.csv     net count per item across the account, and who holds it
-- The output folder belongs outside every repository (spec dispute D-8: the founder's data stays local).
--
-- usage: lua5.1 csv_export.lua <journal_reader.lua> <out dir> <board var> <fafnir var> <board files...> -- <fafnir files...>
local reader_path, out_dir, board_var, fafnir_var = arg[1], arg[2], arg[3], arg[4]
assert(reader_path and out_dir and board_var and fafnir_var, "usage: csv_export.lua <reader> <out> <board var> <fafnir var> <files...> -- <files...>")
local R = dofile(reader_path)

local board_files, fafnir_files, into = {}, {}, nil
into = board_files
for i = 5, #arg do
    if arg[i] == "--" then into = fafnir_files else into[#into + 1] = arg[i] end
end

local function read(path)
    local f = io.open(path, "rb")
    if not f then return nil end
    local t = f:read("*a"); f:close()
    return t
end

local function csv(v)
    if v == nil then return "" end
    v = tostring(v)
    if v:find('[,"\r\n]') then return '"' .. v:gsub('"', '""') .. '"' end
    return v
end

local function line(fields)
    local out = {}
    for i = 1, #fields do out[i] = csv(fields[i]) end
    return table.concat(out, ",") .. "\n"
end

local function utc(t) return t and t > 0 and os.date("!%Y-%m-%dT%H:%M:%SZ", t) or "" end

local function write(name, header, rows, mode)
    local path = out_dir .. "/" .. name
    local exists = io.open(path, "rb")
    if exists then exists:close() end
    local f = assert(io.open(path, mode or "wb"))
    if mode ~= "ab" or not exists then f:write(line(header)) end
    for _, r in ipairs(rows) do f:write(line(r)) end
    f:close()
    return #rows
end

local report, problems = {}, 0
local function load(path, var)
    local db, err = R.load_saved_variables(read(path), var, "=" .. path)
    if not db then io.stderr:write("csv-export: skipped " .. path .. ": " .. tostring(err) .. "\n"); problems = problems + 1 end
    return db
end

-- characters.csv -------------------------------------------------------------------------------------------
local snaps = {}
for _, p in ipairs(board_files) do
    local db = load(p, board_var)
    if db and type(db.rows) == "table" then for _, s in ipairs(db.rows) do snaps[#snaps + 1] = s end end
end
table.sort(snaps, function(a, b) if (a.t or 0) ~= (b.t or 0) then return (a.t or 0) < (b.t or 0) end return tostring(a.character) < tostring(b.character) end)
local crow = {}
for _, s in ipairs(snaps) do
    local pct = (s.xpmax and s.xpmax > 0) and string.format("%.1f", (s.xp or 0) * 100 / s.xpmax) or ""
    crow[#crow + 1] = { utc(s.t), s.t, s.character, s.class, s.race, s.level, s.xp, s.xpmax, pct, s.rested,
                        s.gold, string.format("%.4f", (s.gold or 0) / 10000), s.zone, s.map, s.played, s.ilvl, s.profs }
end
report.characters = write("characters.csv", { "time_utc", "epoch", "character", "class", "race", "level", "xp", "xp_max",
    "xp_pct", "rested_xp", "gold_copper", "gold", "zone", "map_id", "played_seconds", "item_level", "professions" }, crow)

-- bags.csv / bags_history.csv / inventory_net.csv -------------------------------------------------------------
local held, net, names, seen = {}, {}, {}, {}
for _, p in ipairs(fafnir_files) do
    local db = load(p, fafnir_var)
    if db and type(db.chars) == "table" then
        for id, n in pairs(db.names or {}) do names[id] = n end
        for who, ch in pairs(db.chars) do
            seen[who] = ch.seen or 0
            for loc, items in pairs(ch.loc or {}) do
                for id, count in pairs(items) do
                    held[#held + 1] = { who, loc, id, count, ch.seen or 0 }
                    net[id] = net[id] or { 0, {} }
                    net[id][1] = net[id][1] + count
                    net[id][2][who] = (net[id][2][who] or 0) + count
                end
            end
        end
    end
end
table.sort(held, function(a, b)
    if a[1] ~= b[1] then return a[1] < b[1] end
    if a[2] ~= b[2] then return a[2] < b[2] end
    return a[3] < b[3]
end)
local brow = {}
for _, h in ipairs(held) do brow[#brow + 1] = { h[1], h[2], h[3], names[h[3]], h[4], utc(h[5]) } end
report.bags = write("bags.csv", { "character", "location", "item_id", "item_name", "count", "seen_utc" }, brow)

-- history: only characters refreshed since the last export (state file: character<TAB>seen)
local state_path = out_dir .. "/.bags_history.state"
local last = {}
local sf = io.open(state_path, "rb")
if sf then for l in sf:lines() do local who, t = l:match("^(.-)\t(%d+)$"); if who then last[who] = tonumber(t) end end; sf:close() end
local now = os.time()
local hrow = {}
for _, h in ipairs(held) do
    if (last[h[1]] or -1) ~= seen[h[1]] then hrow[#hrow + 1] = { utc(now), h[1], h[2], h[3], names[h[3]], h[4] } end
end
report.history = write("bags_history.csv", { "export_utc", "character", "location", "item_id", "item_name", "count" }, hrow, "ab")
sf = assert(io.open(state_path, "wb"))
local whos = {}
for who in pairs(seen) do whos[#whos + 1] = who end
table.sort(whos)
for _, who in ipairs(whos) do sf:write(who .. "\t" .. seen[who] .. "\n") end
sf:close()

local nrow, ids = {}, {}
for id in pairs(net) do ids[#ids + 1] = id end
table.sort(ids)
for _, id in ipairs(ids) do
    local holders = {}
    for who, n in pairs(net[id][2]) do holders[#holders + 1] = who .. " " .. n end
    table.sort(holders)
    nrow[#nrow + 1] = { id, names[id], net[id][1], table.concat(holders, "; ") }
end
report.net = write("inventory_net.csv", { "item_id", "item_name", "net_count", "holders" }, nrow)

print(string.format("csv-export: %d character snapshots, %d bag rows, %d history rows appended, %d items net -> %s",
    report.characters, report.bags, report.history, report.net, out_dir))
os.exit(problems == 0 and 0 or 1)
