-- runtime/wow/guild_feed_read.lua -- GuildHerald's SavedVariables (one file per account) out of the game, two ways:
--   1. the Collection feed (REFLUX rows 3082-3084 + a name table) that SHANKPIT's /ascend reads;
--   2. an NDJSON log stream: every guild MOTD / info text ever logged, appended once each, to work with as community
--      data. A state file beside the log remembers the last history seq written per account file.
-- What the feed holds is stdlib/collection/guildherald.prn (gh-feed-*), passed in compiled.
--
-- usage: lua5.1 guild_feed_read.lua GH_LUA VAR FEED_OUT LOG_OUT SAVEDVARS_FILE...
local gh_path, var, feed_out, log_out = arg[1], arg[2], arg[3], arg[4]
local function die(msg) io.stderr:write("guild_feed_read: " .. msg .. "\n"); os.exit(2) end
if not (gh_path and var and feed_out and log_out) then die("usage: guild_feed_read.lua GH_LUA VAR FEED_OUT LOG_OUT SAVEDVARS_FILE...") end
if not var:match("^[A-Za-z_][A-Za-z0-9_]*$") then die("bad variable name '" .. var .. "'") end

RefluxHost = RefluxHost or { dispatch = function() end }
local H = dofile(gh_path)

local function json_str(s)
    s = tostring(s or "")
    return '"' .. s:gsub('[%c"\\]', function(c)
        if c == '"' then return '\\"' elseif c == "\\" then return "\\\\"
        elseif c == "\n" then return "\\n" elseif c == "\t" then return "\\t"
        else return string.format("\\u%04x", c:byte()) end
    end) .. '"'
end

-- the log's state: "<account file>\t<last seq>" per line
local state, state_path = {}, log_out .. ".state"
do
    local f = io.open(state_path, "rb")
    if f then
        for line in f:lines() do
            local file, seq = line:match("^(.-)\t(%d+)$")
            if file then state[file] = tonumber(seq) end
        end
        f:close()
    end
end

local feed = H.ghFeedNew()
local log = assert(io.open(log_out, "ab"), "cannot append to " .. log_out)
local files, appended = 0, 0
for i = 5, #arg do
    local path = arg[i]
    local chunk, err = loadfile(path)
    if not chunk then die("cannot read " .. path .. ": " .. tostring(err)) end
    local env = {}
    setfenv(chunk, env)
    local ok, e = pcall(chunk)
    if not ok then die("cannot load " .. path .. ": " .. tostring(e)) end
    local db = env[var]
    if type(db) == "table" then
        files = files + 1
        H.ghFeedMerge(feed, db)
        local last = state[path] or 0
        local high = last
        local entries = {}
        for _, g in pairs(db.guilds or {}) do
            for _, h in ipairs(g.history or {}) do
                if type(h) == "table" and (h.seq or 0) > last then entries[#entries + 1] = { g = g, h = h } end
            end
        end
        table.sort(entries, function(a, b) return a.h.seq < b.h.seq end)
        for _, x in ipairs(entries) do
            local codes = {}
            for code in pairs(x.g.invites or {}) do codes[#codes + 1] = json_str(code) end
            table.sort(codes)
            log:write('{"v":1,"type":"guild_text","seq":', x.h.seq, ',"at":', x.h.at or 0,
                ',"guild":', json_str(x.g.name), ',"realm":', json_str(x.g.realm),
                ',"kind":', json_str(x.h.kind == 1 and "motd" or "info"), ',"seen_by":', json_str(x.h.who),
                ',"text":', json_str(x.h.text), ',"guild_invites":[', table.concat(codes, ","), "]}\n")
            appended = appended + 1
            if x.h.seq > high then high = x.h.seq end
        end
        state[path] = high
    end
end
log:close()
H.ghFeedSummaryRow(feed)

local sf = assert(io.open(state_path .. ".tmp", "wb"))
for file, seq in pairs(state) do sf:write(file, "\t", seq, "\n") end
sf:close()
os.remove(state_path)
assert(os.rename(state_path .. ".tmp", state_path))

local tmp = feed_out .. ".tmp"
local f = assert(io.open(tmp, "wb"), "cannot write " .. tmp)
f:write("collection-feed 1\n")
for id = 1, feed.nnames do f:write("name ", id, " ", (tostring(feed.names[id]):gsub("[\t\r\n]", " ")), "\n") end
for n = 1, feed.nrows do
    local r = feed.rows[n]
    f:write(string.format("row %d %d %d %d\n", r[1], r[2] or 0, r[3] or 0, r[4] or 0))
end
f:close()
os.remove(feed_out)
assert(os.rename(tmp, feed_out))
io.stderr:write(string.format("guild_feed_read: %d account file(s), %d guilds, %d invites -> %s; %d new log entries -> %s\n",
    files, feed.guilds, feed.invites, feed_out, appended, log_out))
