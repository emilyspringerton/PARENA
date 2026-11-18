-- runtime/wow/catalog_gen.lua -- write the plugin store's catalog.json (docs/PLUGIN_STORE_NORTHSTAR.md).
--
-- Host glue only: every decision about a row (is the channel valid, can it be downloaded, does it need RefluxCore,
-- which badge, how the list sorts) is stdlib/collection/catalog.prn, compiled to Lua and passed in as CATALOG_LUA.
-- This file reads the manifest and the packaged client's .toc files and prints JSON.
--
-- usage: lua5.1 catalog_gen.lua CATALOG_LUA PLUGINS_TSV CLIENT_DIR BUILD VERSION INTERFACE > catalog.json
-- Exit 2 on a bad manifest row, a missing addon, or bad arguments; the message names the line.

local cat_path, tsv_path, client_dir, build, version, interface = arg[1], arg[2], arg[3], arg[4], arg[5], arg[6]

local function die(msg)
  io.stderr:write("catalog_gen: " .. msg .. "\n")
  os.exit(2)
end

if not (cat_path and tsv_path and client_dir and build and version and interface) then
  die("usage: catalog_gen.lua CATALOG_LUA PLUGINS_TSV CLIENT_DIR BUILD VERSION INTERFACE")
end
if not build:match("^[1-9][0-9]*$") then die("BUILD must be a positive integer, got '" .. build .. "'") end
if not interface:match("^[0-9]+$") then die("INTERFACE must be digits, got '" .. interface .. "'") end
if not version:match("^[A-Za-z0-9._-]+$") then die("VERSION has unexpected characters: '" .. version .. "'") end
build = tonumber(build)

local C = dofile(cat_path)

local CHANNEL = { wishlist = C.catWishlist(), early = C.catEarly(), available = C.catAvailable() }
local CHANNEL_NAME = {}
for k, v in pairs(CHANNEL) do CHANNEL_NAME[v] = k end
local KIND = { wow = C.catKindWow(), shankpit = C.catKindShankpit() }

local function read_file(path)
  local f = io.open(path, "rb")
  if not f then return nil end
  local s = f:read("*a")
  f:close()
  return s
end

-- The .toc fields this needs: Title, Notes, Version, Dependencies. Missing fields are nil.
local function read_toc(id)
  local s = read_file(client_dir .. "/AddOns/" .. id .. "/" .. id .. ".toc")
  if not s then return nil end
  local t = {}
  for line in s:gmatch("[^\r\n]+") do
    local k, v = line:match("^##%s*([%w%-]+):%s*(.-)%s*$")
    if k then t[k] = v end
  end
  return t
end

local function json_str(s)
  if s == nil then return "null" end
  s = s:gsub('[%c"\\]', function(c)
    if c == '"' then return '\\"' elseif c == "\\" then return "\\\\"
    elseif c == "\n" then return "\\n" elseif c == "\t" then return "\\t"
    else return string.format("\\u%04x", c:byte()) end
  end)
  return '"' .. s .. '"'
end

local function split_tabs(line)
  local out = {}
  for field in (line .. "\t"):gmatch("([^\t]*)\t") do out[#out + 1] = field end
  return out
end

local f = io.open(tsv_path, "rb")
if not f then die("cannot open " .. tsv_path) end
local rows, seen, lineno = {}, {}, 0
for line in f:lines() do
  lineno = lineno + 1
  line = line:gsub("\r$", "")
  if line ~= "" and not line:match("^#") then
    local c = split_tabs(line)
    if #c ~= 6 then die(tsv_path .. ":" .. lineno .. ": want 6 TAB-separated fields, got " .. #c) end
    local id, kind, channel, spec, title, summary = c[1], c[2], c[3], c[4], c[5], c[6]
    if not id:match("^[A-Za-z][A-Za-z0-9_-]*$") then die(tsv_path .. ":" .. lineno .. ": bad id '" .. id .. "'") end
    if seen[id] then die(tsv_path .. ":" .. lineno .. ": duplicate id " .. id) end
    seen[id] = true
    local k, ch = KIND[kind], CHANNEL[channel]
    if not k or C.catKindValid(k) ~= 1 then die(tsv_path .. ":" .. lineno .. ": bad kind '" .. kind .. "'") end
    if not ch or C.catChannelValid(ch) ~= 1 then die(tsv_path .. ":" .. lineno .. ": bad channel '" .. channel .. "'") end
    if spec ~= "-" and not spec:match("^[1-9][0-9]*$") then die(tsv_path .. ":" .. lineno .. ": bad spec '" .. spec .. "'") end
    local r = { id = id, kind = kind, k = k, ch = ch, spec = spec ~= "-" and tonumber(spec) or nil,
                title = title ~= "-" and title or nil, summary = summary ~= "-" and summary or nil, depends = {} }
    local downloadable = C.catDownloadable(ch) == 1
    if k == KIND.wow and downloadable then
      local toc = read_toc(id)
      if not toc then die(tsv_path .. ":" .. lineno .. ": " .. id .. " is " .. channel .. " but " .. client_dir .. "/AddOns/" .. id .. " has no .toc") end
      r.title = r.title or toc.Title
      r.summary = r.summary or toc.Notes
      r.version = toc.Version
      -- Relative to catalog.json's own URL: the zip sits beside it (a GitHub release's assets are one flat list).
      r.download = id .. ".zip"
      for dep in (toc.Dependencies or ""):gmatch("[^,%s]+") do r.depends[#r.depends + 1] = dep end
      if C.catNeedsCore(k, id == "RefluxCore" and 1 or 0) == 1 then
        local has = false
        for _, d in ipairs(r.depends) do if d == "RefluxCore" then has = true end end
        if not has then die(id .. ": every WoW addon but RefluxCore must list RefluxCore in ## Dependencies") end
      end
    end
    if not r.title then die(tsv_path .. ":" .. lineno .. ": " .. id .. " has no title") end
    rows[#rows + 1] = r
  end
end
f:close()

table.sort(rows, function(a, b)
  local ra, rb = C.catChannelRank(a.ch), C.catChannelRank(b.ch)
  if ra ~= rb then return ra < rb end
  if (a.spec or 0) ~= (b.spec or 0) then return (a.spec or 0) < (b.spec or 0) end
  return a.id < b.id
end)

local out = {}
out[#out + 1] = '{"schema":1,"build":' .. build .. ',"version":' .. json_str(version) ..
  ',"interface":' .. interface .. ',"generated_by":"PARENA","plugins":['
for i, r in ipairs(rows) do
  local deps = {}
  for j, d in ipairs(r.depends) do deps[j] = json_str(d) end
  out[#out + 1] = (i > 1 and "," or "") .. "\n" ..
    '{"id":' .. json_str(r.id) .. ',"kind":' .. json_str(r.kind) .. ',"channel":' .. json_str(CHANNEL_NAME[r.ch]) ..
    ',"badge":' .. C.catBadge(r.ch) .. ',"confirm":' .. (C.catNeedsConfirm(r.ch) == 1 and "true" or "false") ..
    ',"spec":' .. (r.spec and tostring(r.spec) or "null") .. ',"title":' .. json_str(r.title) ..
    ',"summary":' .. json_str(r.summary) .. ',"version":' .. json_str(r.version) ..
    ',"build":' .. (r.download and tostring(build) or "null") .. ',"depends":[' .. table.concat(deps, ",") .. ']' ..
    ',"download":' .. json_str(r.download) .. '}'
end
out[#out + 1] = "\n]}\n"
io.write(table.concat(out))
