-- runtime/wow/forge/forge.lua -- the plugin forge: see and plan each character's macro kit, outside the game, then bake
-- the plans into MacroSync (founder, 2026-11-14: "a server we can run local with full cli interface for the plugin
-- forge -- i want to be able to see and plan kits for characters"). Plain Lua 5.1, run by scripts/forge.sh, which
-- finds the files and compiles the PARENA modules; forge_server.c runs it for the web page.
--
-- A kit is the set of macros MacroSync's /ascend writes for one character: its class set (stdlib/collection/macros.prn)
-- changed by that character's plan (stdlib/collection/kits.prn says how). Plans live in <FORGE_DIR>/kits.lua, outside
-- every repository (spec dispute D-8: the founder's data stays local). Characters are read from the Character Board's
-- SavedVariables (name, class, level, xp, gold...), Fafnir's Bed's (gold, gear, bags, bank, mail) and MacroSync's
-- per-character SavedVariables (what /ascend last wrote).
--
-- usage: lua5.1 forge.lua --lib <dir> --dir <FORGE_DIR> [--board FILE]... [--fafnir FILE]... [--ms FILE CHARACTER]...
--                         [--json] COMMAND ARGS
--        lua5.1 forge.lua --lib <dir> --dir <FORGE_DIR> ... --argfile FILE      (the arguments, one per line, escaped)
--
-- Commands (CHARACTER is Name or Name-Realm, any case; a unique start of the name is enough):
--   characters                      every character the forge knows, its class, level, gold, and how its kit is planned
--   show CHARACTER                  its level, xp, gold, gear by slot, bags, bank and mail (Fafnir's Bed), and its kit
--   history [CHARACTER]             gold by day across the account, or one character's level and gold by day
--   find ITEM                       every item whose name holds ITEM (or whose id it is): who has it, and where
--   gear CHARACTER                  worn gear by slot, and the gear planned for each slot (stdlib/collection/kits.prn)
--   want CHARACTER SLOT ITEM        plan ITEM (an id, or a name Fafnir's Bed has seen) for SLOT
--   unwant CHARACTER SLOT           stop planning SLOT
--   classes                         the classes that have a macro set
--   set CLASS                       a class's set, as MacroSync ships it
--   kit CHARACTER                   the character's planned kit, macro by macro, and what /ascend last wrote
--   add CHARACTER NAME BODY...      add a macro of your own (BODY: the rest of the line; \n starts a new line)
--   edit CHARACTER NAME BODY...     change a macro's body (a class macro, or one you added)
--   drop CHARACTER NAME             leave a macro out (a class one is dropped; one you added is removed)
--   restore CHARACTER NAME          put a class macro back the way the class set has it
--   reset CHARACTER                 forget the character's whole plan
--   copy FROM TO                    give TO the same plan as FROM (TO keeps its own class)
--   class CHARACTER CLASS           plan for a character the Character Board has not seen yet, as this class
--   export                          the plans as MacroSync's kits.lua (what `forge build` bakes into the addon)
-- --json prints one JSON value instead of text (forge_server.c uses it). Exit codes: 0 done, 1 refused, 2 usage.

local opts = { board = {}, ms = {}, fafnir = {} }
local args = {}
do
    local i = 1
    while i <= #arg do
        local a = arg[i]
        if a == "--lib" then opts.lib = arg[i + 1]; i = i + 2
        elseif a == "--dir" then opts.dir = arg[i + 1]; i = i + 2
        elseif a == "--board" then opts.board[#opts.board + 1] = arg[i + 1]; i = i + 2
        elseif a == "--fafnir" then opts.fafnir[#opts.fafnir + 1] = arg[i + 1]; i = i + 2
        elseif a == "--ms" then opts.ms[#opts.ms + 1] = { arg[i + 1], arg[i + 2] }; i = i + 3
        elseif a == "--json" then opts.json = true; i = i + 1
        elseif a == "--argfile" then opts.argfile = arg[i + 1]; i = i + 2
        else args[#args + 1] = a; i = i + 1 end
    end
end

local function die(msg) io.stderr:write("forge: " .. msg .. "\n"); os.exit(2) end
if not (opts.lib and opts.dir) then die("--lib and --dir are required (run it through scripts/forge.sh)") end

-- An argument file holds one argument per line, with "\\" and "\n" escaped, so a macro body can carry newlines and
-- the server never puts a visitor's text on a command line.
if opts.argfile then
    local f = io.open(opts.argfile, "rb") or die("cannot read " .. opts.argfile)
    for line in f:lines() do
        line = line:gsub("\r$", "")
        args[#args + 1] = (line:gsub("\\(.)", function(c) if c == "n" then return "\n" end return c end))
    end
    f:close()
end

local K = dofile(opts.lib .. "/kits.lua")
local S = dofile(opts.lib .. "/macros.lua")
local R = dofile(opts.lib .. "/journal_reader.lua")   -- the sandboxed SavedVariables loader

local BOARD_VAR = os.getenv("BOARD_VAR") or "CharacterBoardDB"
local MS_VAR = os.getenv("MACROSYNC_VAR") or "MacroSyncDB"
local FAFNIR_VAR = os.getenv("FAFNIR_VAR") or "FafnirsBedDB"
local ITEM_NAMES = {}
local ROWS = {}            -- every Character Board snapshot ever taken, every account: the progression series
local PLANS = opts.dir .. "/kits.lua"

-- JSON --------------------------------------------------------------------------------------------------------------
-- A Lua table cannot say whether an empty one is a list or a map; lists made with arr() are always JSON arrays.
local ARRAY = {}
local function arr(t) return setmetatable(t or {}, ARRAY) end
local function is_array(t)
    local n = 0
    for _ in pairs(t) do n = n + 1 end
    for i = 1, n do if t[i] == nil then return false end end
    return true
end
local function json(v)
    local kind = type(v)
    if kind == "nil" then return "null"
    elseif kind == "boolean" then return v and "true" or "false"
    elseif kind == "number" then return (v == math.floor(v) and v < 1e15 and v > -1e15) and string.format("%d", v) or string.format("%.14g", v)
    elseif kind == "string" then return R.json_string(v)
    elseif kind == "table" then
        local out = {}
        if getmetatable(v) == ARRAY or (next(v) ~= nil and is_array(v)) then
            for i = 1, #v do out[i] = json(v[i]) end
            return "[" .. table.concat(out, ",") .. "]"
        end
        local keys = {}
        for k in pairs(v) do keys[#keys + 1] = tostring(k) end
        table.sort(keys)
        for _, k in ipairs(keys) do out[#out + 1] = R.json_string(k) .. ":" .. json(v[k]) end
        return "{" .. table.concat(out, ",") .. "}"
    end
    return "null"
end

-- Plans: read with no globals; written as a plain table, atomically. ------------------------------------------------
local function read_all(path)
    local f = io.open(path, "rb")
    if not f then return nil end
    local t = f:read("*a"); f:close()
    return t
end

local function load_plans()
    local text = read_all(PLANS)
    if not text then return {} end
    local chunk, err = loadstring(text, "=" .. PLANS)
    if not chunk then die("kits.lua is damaged: " .. err) end
    setfenv(chunk, {})
    local ok, t = pcall(chunk)
    if not ok or type(t) ~= "table" then die("kits.lua is damaged: " .. tostring(t)) end
    for _, p in pairs(t) do          -- a gear plan is keyed by slot number; an older file may have written "16"
        if type(p) == "table" and type(p.gear) == "table" then
            local g = {}
            for k, v in pairs(p.gear) do g[tonumber(k) or k] = v end
            p.gear = g
        end
    end
    return t
end

local function lua_lit(v, indent)
    if type(v) == "string" then return string.format("%q", v):gsub("\\\n", "\\n") end
    if type(v) == "number" then return tostring(v) end
    if type(v) ~= "table" then return "nil" end
    indent = indent or ""
    local inner = indent .. "  "
    local out = {}
    if next(v) ~= nil and is_array(v) then
        for i = 1, #v do out[#out + 1] = inner .. lua_lit(v[i], inner) .. "," end
    else
        local keys = {}
        for k in pairs(v) do keys[#keys + 1] = k end
        table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
        for _, k in ipairs(keys) do
            local key = type(k) == "number" and tostring(k) or lua_lit(tostring(k))     -- slots and item ids stay numbers
            out[#out + 1] = inner .. "[" .. key .. "] = " .. lua_lit(v[k], inner) .. ","
        end
    end
    if #out == 0 then return "{}" end
    return "{\n" .. table.concat(out, "\n") .. "\n" .. indent .. "}"
end

local function save_plans(plans)
    local tmp = PLANS .. ".tmp"
    local f = io.open(tmp, "wb") or die("cannot write " .. tmp .. " (does FORGE_DIR exist?)")
    f:write("-- the plugin forge's kit plans (runtime/wow/forge/forge.lua). Edited by the forge; safe to read.\n")
    f:write("return " .. lua_lit(plans) .. "\n")
    f:close()
    os.remove(PLANS)                       -- Windows' rename will not replace a file
    assert(os.rename(tmp, PLANS))
end

-- Characters: the Character Board's latest snapshots, MacroSync's per-character files, and every planned one. -------
local function load_db(path, var)
    local db = R.load_saved_variables(read_all(path), var, "=" .. path)
    return type(db) == "table" and db or nil
end

local function characters(plans)
    local chars = {}
    for _, path in ipairs(opts.board) do
        local db = load_db(path, BOARD_VAR)
        if db and type(db.rows) == "table" then
            for _, r in ipairs(db.rows) do if type(r) == "table" and r.character and r.t then ROWS[#ROWS + 1] = r end end
        end
        if db and type(db.chars) == "table" then
            for who, s in pairs(db.chars) do
                local c = chars[who] or {}
                if not c.seen or (s.t or 0) >= c.seen then
                    c.class, c.level, c.race, c.zone, c.seen = s.class, s.level, s.race, s.zone, s.t or 0
                    c.xp, c.xpmax, c.rested, c.ilvl, c.played, c.profs, c.gold = s.xp, s.xpmax, s.rested, s.ilvl, s.played, s.profs, s.gold
                end
                c.on_board = true
                chars[who] = c
            end
        end
    end
    for _, path in ipairs(opts.fafnir) do
        local db = load_db(path, FAFNIR_VAR)
        if db and type(db.chars) == "table" then
            for id, n in pairs(db.names or {}) do ITEM_NAMES[id] = n end
            for who, h in pairs(db.chars) do
                local c = chars[who] or {}
                c.hold = h
                chars[who] = c
            end
        end
    end
    for _, pair in ipairs(opts.ms) do
        local db = load_db(pair[1], MS_VAR)
        if db then
            local c = chars[pair[2]] or {}
            local live = {}
            for name in pairs(db.managed or {}) do live[#live + 1] = name end
            table.sort(live)
            c.live, c.ascends = live, db.ascends
            chars[pair[2]] = c
        end
    end
    for who, p in pairs(plans) do
        local c = chars[who] or {}
        if (c.class or "") == "" and p.class then c.class = p.class end
        chars[who] = c
    end
    return chars
end

-- "emily" / "Emily-Forever" / "em" -> the one character it names, or nil and why.
local function resolve(chars, q)
    if not q or q == "" then return nil, "which character?" end
    local lq = q:lower()
    for who in pairs(chars) do if who:lower() == lq then return who end end
    local hits = {}
    for who in pairs(chars) do
        local name = who:match("^([^-]+)") or who
        if name:lower() == lq then hits[#hits + 1] = who end
    end
    if #hits == 0 then
        for who in pairs(chars) do if who:lower():sub(1, #lq) == lq then hits[#hits + 1] = who end end
    end
    table.sort(hits)
    if #hits == 1 then return hits[1] end
    if #hits == 0 then return nil, "no character called '" .. q .. "' (see `forge characters`; `forge class NAME CLASS` plans for a new one)" end
    return nil, "'" .. q .. "' could be " .. table.concat(hits, ", ")
end

local REASON = {
    [1] = "the name is empty", [2] = "the name is over 16 characters (the macro frame's limit)",
    [3] = "the body is empty", [5] = "the kit already has a macro with that name (edit it instead)",
    [6] = "the kit has no macro with that name", [7] = "it is already the way the class set has it",
}

local ORIGIN = { [0] = "class", [1] = "edited", [2] = "added" }

-- Output ------------------------------------------------------------------------------------------------------------
local function out(value, text)
    if opts.json then io.write(json(value), "\n") else io.write(text, "\n") end
end
local function refuse(msg)
    if opts.json then io.write(json({ error = msg }), "\n") else io.stderr:write("forge: " .. msg .. "\n") end
    os.exit(1)
end

local function one_line(body) return (body:gsub("\n", " \\n ")) end

local function kit_view(who, c, plan)
    local set = S.macroSet(c.class or "")
    local kit = K.kitApply(set, plan or {})
    local live = {}
    for _, n in ipairs(c.live or {}) do live[n] = true end
    local macros = arr()
    for i, m in ipairs(kit) do
        local check = K.kitCheck(m[1], m[2])
        macros[i] = { name = m[1], body = m[2], origin = ORIGIN[m[3]], live = live[m[1]] == true,
                      long = check == 4 }
    end
    local dropped = arr()
    for name in pairs((plan or {}).drop or {}) do dropped[#dropped + 1] = name end
    table.sort(dropped)
    local stale = arr()              -- written by /ascend last time, not in the planned kit: the next /ascend prunes it
    local inkit = {}
    for _, m in ipairs(kit) do inkit[m[1]] = true end
    for _, n in ipairs(c.live or {}) do if not inkit[n] then stale[#stale + 1] = n end end
    return { character = who, class = c.class or "", level = c.level, class_set = #set, changes = plan and K.kitChanges(plan) or 0,
             macros = macros, dropped = dropped, ascended = c.ascends ~= nil, will_prune = stale }
end

-- Gold, gear and bags (Fafnir's Bed), as the in-game /chars screen shows them. --------------------------------------
local SLOTS = { "Head", "Neck", "Shoulder", "Shirt", "Chest", "Waist", "Legs", "Feet", "Wrist", "Hands", "Finger", "Finger",
                "Trinket", "Trinket", "Back", "Main hand", "Off hand", "Ranged", "Tabard" }

local LOCS = { "bags", "bank", "mail", "equipped" }

local function gold_of(c)
    if c.hold and c.hold.gold then return c.hold.gold end
    return c.gold or 0
end

local function money(copper)
    copper = math.floor(tonumber(copper) or 0)
    return string.format("%dg %ds %dc", math.floor(copper / 10000), math.floor(copper / 100) % 100, copper % 100)
end

local function item_name(id) return ITEM_NAMES[id] or ITEM_NAMES[tonumber(id)] or ("item " .. tostring(id)) end

local function holdings(c)
    local h = c.hold or {}
    local gear = arr()
    for slot = 1, 19 do
        local id = h.gear and (h.gear[slot] or h.gear[tostring(slot)])
        if id then gear[#gear + 1] = { slot = slot, slot_name = SLOTS[slot], id = id, name = item_name(id) } end
    end
    local locs = {}
    for _, loc in ipairs({ "bags", "bank", "mail" }) do
        local t = h.loc and h.loc[loc]
        local rows = arr()
        for id, n in pairs(t or {}) do rows[#rows + 1] = { id = tonumber(id) or id, name = item_name(id), count = n } end
        table.sort(rows, function(a, b) if a.name ~= b.name then return a.name < b.name end return tostring(a.id) < tostring(b.id) end)
        locs[loc] = { read = t ~= nil, items = rows }
    end
    return gear, locs
end

-- History (the Character Board's snapshots) and search (Fafnir's Bed's names and holdings). -----------------------
local function day(t) return os.date("!%Y-%m-%d", t) end

-- One character by day: the last snapshot of each day.
local function char_history(who)
    local rows = {}
    for _, r in ipairs(ROWS) do if r.character == who then rows[#rows + 1] = r end end
    table.sort(rows, function(a, b) return a.t < b.t end)
    local days, last = arr(), nil
    for _, r in ipairs(rows) do
        local d = day(r.t)
        local entry = { date = d, level = r.level, gold = r.gold or 0, zone = r.zone,
                        xp_pct = (r.xpmax and r.xpmax > 0) and math.floor((r.xp or 0) * 100 / r.xpmax) or nil }
        if last and last.date == d then days[#days] = entry else days[#days + 1] = entry end
        last = entry
    end
    return days
end

-- The account by day: every character's gold as last seen by the end of that day, added up.
local function account_history()
    local rows = {}
    for _, r in ipairs(ROWS) do rows[#rows + 1] = r end
    table.sort(rows, function(a, b) return a.t < b.t end)
    local gold, days = {}, arr()
    local function close(d)
        local total, n = 0, 0
        for _, g in pairs(gold) do total = total + g; n = n + 1 end
        days[#days + 1] = { date = d, gold = total, characters = n }
    end
    local current
    for _, r in ipairs(rows) do
        local d = day(r.t)
        if current and d ~= current then close(current) end
        current = d
        gold[r.character] = r.gold or 0
    end
    if current then close(current) end
    return days
end

-- Items whose name holds the query (or whose id it is), and who holds them where.
-- Who holds item `id`, where: an array of {character, bags, bank, mail, equipped, total}, most first; and the total.
local function holders_of(chars, id)
    local holders, total = arr(), 0
    for who, c in pairs(chars) do
        local loc = c.hold and c.hold.loc
        if loc then
            local h, n = { character = who }, 0
            for _, l in ipairs(LOCS) do
                local k = loc[l] and (loc[l][id] or loc[l][tostring(id)]) or 0
                if k > 0 then h[l] = k; n = n + k end
            end
            if n > 0 then h.total = n; holders[#holders + 1] = h; total = total + n end
        end
    end
    table.sort(holders, function(a, b) if a.total ~= b.total then return a.total > b.total end return a.character < b.character end)
    return holders, total
end

local function find_items(chars, q)
    local lq, id_q = q:lower(), tonumber(q)
    local ids = {}
    for id, name in pairs(ITEM_NAMES) do
        if tonumber(id) == id_q or tostring(name):lower():find(lq, 1, true) then ids[#ids + 1] = tonumber(id) or id end
    end
    local items = arr()
    for _, id in ipairs(ids) do
        local holders, total = holders_of(chars, id)
        items[#items + 1] = { id = id, name = item_name(id), total = total, holders = holders }
    end
    table.sort(items, function(a, b) if a.total ~= b.total then return a.total > b.total end return a.name < b.name end)
    while #items > 50 do table.remove(items) end
    return items
end

-- Gear plans. A slot is its number (1-19) or its name: head, neck, shoulder, shirt, chest, waist, legs, feet, wrist,
-- hands, finger1, finger2, trinket1, trinket2, back, mainhand, offhand, ranged, tabard (spaces and case ignored).
local SLOT_KEYS = { "head", "neck", "shoulder", "shirt", "chest", "waist", "legs", "feet", "wrist", "hands", "finger1",
                    "finger2", "trinket1", "trinket2", "back", "mainhand", "offhand", "ranged", "tabard" }
local function slot_of(q)
    local n = tonumber(q)
    if n then return n end
    local k = (q or ""):lower():gsub("[%s_%-]", "")
    if k == "finger" or k == "ring" then k = "finger1" elseif k == "trinket" then k = "trinket1"
    elseif k == "shoulders" then k = "shoulder" elseif k == "weapon" then k = "mainhand" elseif k == "cloak" then k = "back" end
    for i, name in ipairs(SLOT_KEYS) do if name == k then return i end end
    return nil
end

-- An item: its id, or a name (an exact name first, else the one name that holds the text).
local function item_of(q)
    local n = tonumber(q)
    if n then return n end
    local lq, hits = (q or ""):lower(), {}
    for id, name in pairs(ITEM_NAMES) do
        local ln = tostring(name):lower()
        if ln == lq then return tonumber(id) or id end
        if ln:find(lq, 1, true) then hits[#hits + 1] = { id = tonumber(id) or id, name = tostring(name) } end
    end
    if #hits == 1 then return hits[1].id end
    if #hits == 0 then return nil, "no item named like '" .. q .. "' has been seen (use its item id)" end
    table.sort(hits, function(a, b) return a.name < b.name end)
    local names = {}
    for i = 1, math.min(5, #hits) do names[i] = hits[i].name end
    return nil, "'" .. q .. "' could be " .. table.concat(names, ", ") .. (#hits > 5 and ", ..." or "")
end

local STATUS = { [0] = "none", [1] = "worn", [2] = "on the account", [3] = "not on the account" }

local function gear_plan_view(chars, who, plan)
    local c = chars[who]
    local h = c.hold or {}
    local wanted = (plan and plan.gear) or {}
    local rows = arr()
    for slot = 1, 19 do
        local worn = h.gear and (h.gear[slot] or h.gear[tostring(slot)]) or nil
        local want = wanted[slot] or wanted[tostring(slot)]
        if worn or want then
            local row = { slot = slot, slot_name = SLOTS[slot] }
            if worn then row.worn = { id = worn, name = item_name(worn) } end
            if want then
                local holders, total = holders_of(chars, want)
                local held = total - ((worn == want) and 1 or 0)
                row.wanted = { id = want, name = item_name(want) }
                row.status = STATUS[K.kitGearStatus(want, worn or 0, held)]
                local where = arr()
                for _, hd in ipairs(holders) do
                    for _, l in ipairs(LOCS) do
                        local k = hd[l]
                        if hd.character == who and l == "equipped" and worn == want then k = (k or 0) - 1 end
                        if k and k > 0 then where[#where + 1] = { character = hd.character, location = l, count = k } end
                    end
                end
                row.where = where
            end
            rows[#rows + 1] = row
        end
    end
    return rows
end

-- Commands ----------------------------------------------------------------------------------------------------------
local cmd = table.remove(args, 1)
local plans = load_plans()
local chars = characters(plans)

local function char_arg(i)
    local who, why = resolve(chars, args[i])
    if not who then refuse(why) end
    return who
end

local function body_from(i)
    local parts = {}
    for j = i, #args do parts[#parts + 1] = args[j] end
    local body = table.concat(parts, " ")
    if not opts.argfile then body = body:gsub("\\n", "\n") end
    return body
end

local function needs_class(who)
    local c = chars[who]
    if (c.class or "") == "" then refuse(who .. " has no class yet: `forge class " .. who .. " CLASS` first") end
    return c
end

local function change(who, fn, done)
    local c = needs_class(who)
    plans[who] = plans[who] or {}
    plans[who].class = plans[who].class or c.class
    local code = fn(S.macroSet(c.class), plans[who])
    if code ~= 0 then refuse(REASON[code] or ("refused (" .. code .. ")")) end
    save_plans(plans)
    local view = kit_view(who, c, plans[who])
    out({ ok = true, message = done, kit = view }, done)
end

local USAGE = [[
forge characters                  every character, its class, level and gold, and how its kit is planned
forge show CHARACTER              level, xp, gold, professions, everything equipped, bags, bank and mail
forge history [CHARACTER]         gold (and level) by day: the account's, or one character's
forge find ITEM                   who has it, and where (part of a name, or an item id)
forge gear CHARACTER              what is worn in each slot, and the gear planned for it (worn / on the account / not yet)
forge want CHARACTER SLOT ITEM    plan an item for a slot (head ... mainhand, offhand, ranged, or 1-19; ITEM: id or name)
forge unwant CHARACTER SLOT       stop planning that slot
forge classes                     the classes that have a macro set
forge set CLASS                   a class's set, as MacroSync ships it
forge kit CHARACTER               the planned kit, macro by macro (* = on the character since its last /ascend)
forge add CHARACTER NAME BODY     add a macro of your own (\n in BODY starts a new line)
forge edit CHARACTER NAME BODY    change a macro's body (a class macro, or one you added)
forge drop CHARACTER NAME         leave a macro out of the kit
forge restore CHARACTER NAME      put a class macro back the way the class set has it
forge reset CHARACTER             forget the character's whole plan
forge copy FROM TO                give TO the same plan as FROM (same class)
forge class CHARACTER CLASS       plan for a character the Character Board has not seen yet
forge export                      the plans as MacroSync's kits.lua
forge build                       bake the plans into the addon package (scripts/forge.sh)
forge serve [PORT]                the same on a page at http://127.0.0.1:PORT (scripts/forge.sh)
CHARACTER is Name or Name-Realm, any case; a unique start of the name is enough.]]

if cmd == nil or cmd == "help" then
    out({ usage = USAGE }, USAGE)
    os.exit(cmd == nil and 2 or 0)

elseif cmd == "characters" or cmd == "chars" then
    local list = arr()
    for who, c in pairs(chars) do
        local p = plans[who]
        list[#list + 1] = { character = who, class = c.class or "", level = c.level, zone = c.zone, gold = gold_of(c),
                            planned = p ~= nil and K.kitChanges(p) or 0, kit = #K.kitApply(S.macroSet(c.class or ""), p or {}),
                            ascended = c.ascends ~= nil }
    end
    table.sort(list, function(a, b) return a.character < b.character end)
    local lines = {}
    for _, r in ipairs(list) do
        lines[#lines + 1] = string.format("%-24s %-8s %3s  %14s  kit %2d macros%s", r.character, r.class, r.level and tostring(r.level) or "?", money(r.gold),
            r.kit, r.planned > 0 and string.format(", %d planned change%s", r.planned, r.planned == 1 and "" or "s") or "")
    end
    if #lines == 0 then lines[1] = "no characters yet: log in and out once with the Character Board, or `forge class NAME CLASS`" end
    out({ characters = list }, table.concat(lines, "\n"))

elseif cmd == "classes" then
    local list = S.macroClasses()
    local rows = arr()
    for i, cl in ipairs(list) do rows[i] = { class = cl, macros = #S.macroSet(cl) } end
    local lines = {}
    for _, r in ipairs(rows) do lines[#lines + 1] = string.format("%-8s %d macros", r.class, r.macros) end
    out({ classes = rows }, table.concat(lines, "\n"))

elseif cmd == "set" then
    local cl = (args[1] or ""):upper()
    local set = S.macroSet(cl)
    if #set == 0 then refuse("no macro set for '" .. (args[1] or "") .. "' (see `forge classes`)") end
    local rows, lines = arr(), {}
    for i, m in ipairs(set) do rows[i] = { name = m[1], body = m[2] }; lines[i] = string.format("%-16s %s", m[1], one_line(m[2])) end
    out({ class = cl, macros = rows }, table.concat(lines, "\n"))

elseif cmd == "kit" then
    local who = char_arg(1)
    local v = kit_view(who, chars[who], plans[who])
    local lines = { string.format("%s -- %s%s, %d macros (%d from the class set, %d planned change%s)", who,
        v.class ~= "" and v.class or "no class", v.level and (" " .. v.level) or "", #v.macros, v.class_set, v.changes, v.changes == 1 and "" or "s") }
    for _, m in ipairs(v.macros) do
        local tag = m.origin == "class" and "      " or string.format("%-6s", m.origin)
        lines[#lines + 1] = string.format("  %s %s %-16s %s%s", tag, m.live and "*" or " ", m.name, one_line(m.body), m.long and "   (over 255: the client may cut it)" or "")
    end
    if #v.dropped > 0 then lines[#lines + 1] = "  dropped: " .. table.concat(v.dropped, ", ") end
    if v.ascended then
        lines[#lines + 1] = "  * = on the character now (from its last /ascend)"
        if #v.will_prune > 0 then lines[#lines + 1] = "  the next /ascend prunes: " .. table.concat(v.will_prune, ", ") end
    else
        lines[#lines + 1] = "  /ascend has not run on this character yet (or it has not logged out since)"
    end
    if v.class_set == 0 and v.class ~= "" then lines[#lines + 1] = "  " .. v.class .. " has no class set yet: the kit is only what you add" end
    out(v, table.concat(lines, "\n"))

elseif cmd == "show" then
    local who = char_arg(1)
    local c = chars[who]
    local gear, locs = holdings(c)
    local v = { character = who, class = c.class or "", race = c.race, level = c.level, zone = c.zone, seen = c.seen,
                xp = c.xp, xpmax = c.xpmax, rested = c.rested, ilvl = c.ilvl, played = c.played, profs = c.profs,
                gold = gold_of(c), gear = gear, bags = locs.bags, bank = locs.bank, mail = locs.mail,
                kit = kit_view(who, c, plans[who]), gear_plan = gear_plan_view(chars, who, plans[who]) }
    local lines = { string.format("%s -- level %s %s %s", who, tostring(c.level or "?"), tostring(c.race or ""), v.class) }
    if c.xpmax and c.xpmax > 0 then
        lines[#lines + 1] = string.format("  xp %d%% of the level, %d%% rested   item level %s", math.floor(c.xp * 100 / c.xpmax),
            math.floor((c.rested or 0) * 100 / c.xpmax), tostring(c.ilvl or "?"))
    end
    if c.zone then lines[#lines + 1] = "  in " .. c.zone .. (c.seen and c.seen > 0 and ("   last seen " .. os.date("!%Y-%m-%d %H:%M", c.seen) .. " UTC") or "") end
    if c.profs and c.profs ~= "" then lines[#lines + 1] = "  professions: " .. c.profs end
    lines[#lines + 1] = "  gold: " .. money(v.gold)
    lines[#lines + 1] = "equipped"
    if #gear == 0 then lines[#lines + 1] = "  not read yet (log in with this character once)" end
    for _, g in ipairs(gear) do lines[#lines + 1] = string.format("  %-10s %s", g.slot_name, g.name) end
    for _, loc in ipairs({ "bags", "bank", "mail" }) do
        local l = locs[loc]
        local total = 0
        for _, r in ipairs(l.items) do total = total + r.count end
        lines[#lines + 1] = l.read and string.format("%s (%d kinds, %d items)", loc, #l.items, total) or (loc .. ": not read yet")
        for _, r in ipairs(l.items) do lines[#lines + 1] = string.format("  %s%s", r.name, r.count > 1 and ("  x" .. r.count) or "") end
    end
    lines[#lines + 1] = string.format("kit: %d macros, %d planned changes (forge kit %s)", #v.kit.macros, v.kit.changes, who)
    out(v, table.concat(lines, "\n"))

elseif cmd == "history" then
    if args[1] then
        local who = char_arg(1)
        local days = char_history(who)
        local lines = { who .. ": level and gold by day (the last snapshot of each day)" }
        local prev
        for _, d in ipairs(days) do
            local delta = prev and (d.gold - prev) or 0
            lines[#lines + 1] = string.format("  %s  level %-3s %-6s %16s %s  %s", d.date, tostring(d.level or "?"),
                d.xp_pct and (d.xp_pct .. "%") or "", money(d.gold), prev and string.format("(%s%s)", delta >= 0 and "+" or "-", money(math.abs(delta))) or "",
                d.zone or "")
            prev = d.gold
        end
        if #days == 0 then lines[#lines + 1] = "  no snapshots yet: the Character Board takes one at every login, level and logout" end
        out({ character = who, days = days }, table.concat(lines, "\n"))
    else
        local days = account_history()
        local lines = { "the account's gold by day (every character's latest by the end of the day)" }
        local prev
        for _, d in ipairs(days) do
            local delta = prev and (d.gold - prev) or 0
            lines[#lines + 1] = string.format("  %s  %16s %s  %d characters", d.date, money(d.gold),
                prev and string.format("(%s%s)", delta >= 0 and "+" or "-", money(math.abs(delta))) or "", d.characters)
            prev = d.gold
        end
        if #days == 0 then lines[#lines + 1] = "  no snapshots yet" end
        out({ days = days }, table.concat(lines, "\n"))
    end

elseif cmd == "find" then
    local q = body_from(1)
    if q == "" then refuse("find what? (part of an item's name, or its id)") end
    local items = find_items(chars, q)
    local lines = {}
    for _, it in ipairs(items) do
        lines[#lines + 1] = string.format("%s (%s): %d on the account", it.name, tostring(it.id), it.total)
        for _, h in ipairs(it.holders) do
            local where = {}
            for _, l in ipairs(LOCS) do if h[l] then where[#where + 1] = l .. " " .. h[l] end end
            lines[#lines + 1] = string.format("  %-24s %d  (%s)", h.character, h.total, table.concat(where, ", "))
        end
    end
    if #items == 0 then lines[1] = "nothing seen named like '" .. q .. "' (Fafnir's Bed learns names as items pass through bags, bank, mail and gear)" end
    out({ query = q, items = items }, table.concat(lines, "\n"))

elseif cmd == "gear" then
    local who = char_arg(1)
    local rows = gear_plan_view(chars, who, plans[who])
    local lines = { who .. ": what is worn, and what is planned for each slot" }
    for _, r in ipairs(rows) do
        local line = string.format("  %-10s %-28s", r.slot_name, r.worn and r.worn.name or "-")
        if r.wanted then
            line = line .. "  want " .. r.wanted.name .. "  [" .. r.status .. "]"
            local where = {}
            for _, w in ipairs(r.where) do where[#where + 1] = string.format("%s %s %d", w.character, w.location, w.count) end
            if r.status == "on the account" and #where > 0 then line = line .. "  (" .. table.concat(where, "; ") .. ")" end
        end
        lines[#lines + 1] = line
    end
    if #rows == 0 then lines[#lines + 1] = "  nothing worn has been read and nothing is planned (forge want " .. who .. " SLOT ITEM)" end
    out({ character = who, gear = rows }, table.concat(lines, "\n"))

elseif cmd == "want" then
    local who = char_arg(1)
    local slot = slot_of(args[2])
    if not slot then refuse("no slot called '" .. tostring(args[2]) .. "' (head, neck, shoulder, ... mainhand, offhand, ranged, or 1-19)") end
    local id, why = item_of(body_from(3))
    if not id then refuse(why) end
    plans[who] = plans[who] or {}
    if chars[who].class and not plans[who].class then plans[who].class = chars[who].class end
    local code = K.kitWant_(plans[who], slot, id)
    if code ~= 0 then refuse(code == 8 and "slots are 1-19" or "that is not an item id") end
    save_plans(plans)
    local msg = string.format("%s wants %s in %s", who, item_name(id), SLOTS[slot])
    out({ ok = true, message = msg, gear = gear_plan_view(chars, who, plans[who]) }, msg)

elseif cmd == "unwant" then
    local who = char_arg(1)
    local slot = slot_of(args[2])
    if not slot then refuse("no slot called '" .. tostring(args[2]) .. "'") end
    plans[who] = plans[who] or {}
    local code = K.kitUnwant_(plans[who], slot)
    if code ~= 0 then refuse(code == 8 and "slots are 1-19" or ("nothing is planned for " .. SLOTS[slot])) end
    save_plans(plans)
    local msg = string.format("%s: nothing planned for %s now", who, SLOTS[slot])
    out({ ok = true, message = msg, gear = gear_plan_view(chars, who, plans[who]) }, msg)

elseif cmd == "add" then
    local who, name = char_arg(1), args[2] or ""
    local body = body_from(3)
    change(who, function(set, p) return K.kitAdd_(set, p, name, body) end, "added " .. name .. " to " .. who .. "'s kit")

elseif cmd == "edit" then
    local who, name = char_arg(1), args[2] or ""
    local body = body_from(3)
    change(who, function(set, p) return K.kitEdit_(set, p, name, body) end, "changed " .. name .. " in " .. who .. "'s kit")

elseif cmd == "drop" then
    local who, name = char_arg(1), args[2] or ""
    change(who, function(set, p) return K.kitDrop_(set, p, name) end, "dropped " .. name .. " from " .. who .. "'s kit")

elseif cmd == "restore" then
    local who, name = char_arg(1), args[2] or ""
    change(who, function(set, p) return K.kitRestore_(set, p, name) end, "restored " .. name .. " in " .. who .. "'s kit")

elseif cmd == "reset" then
    local who = char_arg(1)
    local cl = plans[who] and plans[who].class
    plans[who] = nil
    if not chars[who].on_board and cl then plans[who] = { class = cl } end   -- keep the class of a character only the forge knows
    save_plans(plans)
    out({ ok = true, message = who .. "'s kit is the class set again" }, who .. "'s kit is the class set again")

elseif cmd == "copy" then
    local from, to = char_arg(1), char_arg(2)
    if from == to then refuse("that is the same character") end
    local p = plans[from]
    if not p or K.kitChanges(p) == 0 then refuse(from .. " has no planned changes to copy") end
    local tc = chars[to].class or ""
    if tc ~= "" and tc ~= (chars[from].class or "") then refuse(from .. " is a " .. (chars[from].class or "?") .. ", " .. to .. " a " .. tc .. ": a plan only copies within a class") end
    local copy = assert(loadstring("return " .. lua_lit(p)))()
    copy.class = chars[from].class
    plans[to] = copy
    save_plans(plans)
    out({ ok = true, message = "copied " .. from .. "'s plan to " .. to }, "copied " .. from .. "'s plan to " .. to)

elseif cmd == "class" then
    local q, cl = args[1] or "", (args[2] or ""):upper()
    if q == "" or cl == "" then refuse("usage: forge class NAME-Realm CLASS") end
    if not cl:match("^[A-Z]+$") then refuse("a class is one word, e.g. PALADIN") end
    local who = resolve(chars, q) or q
    if chars[who] and chars[who].on_board and (chars[who].class or "") ~= "" then
        refuse(who .. " is already a " .. chars[who].class .. " on the Character Board")
    end
    plans[who] = plans[who] or {}
    plans[who].class = cl
    save_plans(plans)
    out({ ok = true, message = "planning " .. who .. " as a " .. cl }, "planning " .. who .. " as a " .. cl .. (#S.macroSet(cl) == 0 and " (no class set: add its macros yourself)" or ""))

elseif cmd == "export" then
    -- MacroSync's kits.lua: only what /ascend needs, keyed exactly as the addon builds its key (Name-Realm).
    local baked = {}
    for who, p in pairs(plans) do
        if K.kitChanges(p) > 0 or K.kitGearCount(p) > 0 then
            baked[who] = { drop = p.drop, edit = p.edit, add = p.add, gear = (K.kitGearCount(p) > 0) and p.gear or nil }
        end
    end
    io.write("-- kits.lua -- the macro kits planned in the plugin forge (runtime/wow/forge/forge.lua). Generated; do not edit.\n")
    io.write("local _, ns = ...\nns.kits = " .. lua_lit(baked) .. "\n")

else
    refuse("unknown command '" .. tostring(cmd) .. "' (forge help)")
end
