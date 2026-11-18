-- runtime/wow/sf_feed_read.lua -- turn Self Found's SavedVariables (one file per account) into the Collection feed:
-- REFLUX rows plus a name table, the form SHANKPIT's Collection overlay reads (docs/SELF_FOUND.md, "The feed").
-- Which rows are written is stdlib/collection/selffound.prn (sf-feed-merge, sf-feed-summary-row), passed in compiled.
--
-- usage: lua5.1 sf_feed_read.lua SF_LUA VAR OUT_FILE SAVEDVARS_FILE...
-- Feed format v1, one record per line, written to OUT_FILE.tmp and renamed into place:
--   collection-feed 1
--   name <id> <text>            (text has no tabs or newlines; they are replaced by spaces)
--   row <type> <a> <b> <c>      (REFLUX rows, in order)
local sf_path, var, out_path = arg[1], arg[2], arg[3]
local function die(msg) io.stderr:write("sf_feed_read: " .. msg .. "\n"); os.exit(2) end
if not (sf_path and var and out_path) then die("usage: sf_feed_read.lua SF_LUA VAR OUT_FILE SAVEDVARS_FILE...") end
if not var:match("^[A-Za-z_][A-Za-z0-9_]*$") then die("bad variable name '" .. var .. "'") end

-- The compiled module dispatches into REFLUX on records; the feed never records, but give it a host to stand on.
RefluxHost = RefluxHost or { dispatch = function() end }
local S = dofile(sf_path)

local feed = S.sfFeedNew()
local files = 0
for i = 4, #arg do
    local chunk, err = loadfile(arg[i])
    if not chunk then die("cannot read " .. arg[i] .. ": " .. tostring(err)) end
    local env = {}
    setfenv(chunk, env)
    local ok, e = pcall(chunk)
    if not ok then die("cannot load " .. arg[i] .. ": " .. tostring(e)) end
    if type(env[var]) == "table" then S.sfFeedMerge(feed, env[var]); files = files + 1 end
end
S.sfFeedSummaryRow(feed)

local tmp = out_path .. ".tmp"
local f = io.open(tmp, "wb")
if not f then die("cannot write " .. tmp) end
f:write("collection-feed 1\n")
for id = 1, feed.nnames do
    f:write("name ", id, " ", (tostring(feed.names[id]):gsub("[\t\r\n]", " ")), "\n")
end
for n = 1, feed.nrows do
    local r = feed.rows[n]
    f:write(string.format("row %d %d %d %d\n", r[1], r[2] or 0, r[3] or 0, r[4] or 0))
end
f:close()
os.remove(out_path)
assert(os.rename(tmp, out_path))
io.stderr:write(string.format("sf_feed_read: %d account file(s), %d characters (%d self-found), %d vendors -> %s\n",
    files, feed.chars, feed.sf, feed.vendors, out_path))
