-- runtime/wow/csv_export.lua -- the Collection's CSV export (founder, 2026-11-13: "save the whole array of character
-- data and bag data into a csv so we can track our character progression"). Runs outside the game under Lua 5.1,
-- after logging out (the client writes SavedVariables on logout and /reload only).
--
-- Reads, with the journal reader's sandboxed loader (no globals, an instruction budget):
--   every Account/<account>/SavedVariables/<board addon>.lua   the Total Character Board's snapshots (addon 1)
--   every Account/<account>/SavedVariables/<Fafnir addon>.lua  Fafnir's Bed's bags, bank, mail and gold (addon 2)
--   every Account/<account>/<realm>/<character>/SavedVariables/<journal addon>.lua   the Collection Journal: every
--                         REFLUX row carries the map id and x/y where it happened -- the maps on the page
-- Writes, into the output folder:
--   characters.csv        one row per snapshot ever taken: the progression series (rewritten each run)
--   bags.csv              what every character holds now, by location (rewritten each run)
--   bags_history.csv      bags.csv's rows, APPENDED with the export time whenever a character's holdings were
--                         refreshed since the last export -- run it after each session and it becomes a history
--   inventory_net.csv     net count per item across the account, and who holds it
--   positions.csv         every journalled position: character, time, map id and name, x, y, event type
--   collection.html       all of the above as one page that opens straight from disk: characters, inventory
--                         (filter by character, location, search), account totals, and maps of where each
--                         character has been (rewritten each run)
-- The output folder belongs outside every repository (spec dispute D-8: the founder's data stays local).
--
-- usage: lua5.1 csv_export.lua <journal_reader.lua> <out dir> <board var> <fafnir var> <board files...> -- <fafnir files...>
--        [-- <journal files...>]       (the journal's global is COLLECTION_JOURNAL_VAR, default CollectionJournalDB)
local reader_path, out_dir, board_var, fafnir_var = arg[1], arg[2], arg[3], arg[4]
assert(reader_path and out_dir and board_var and fafnir_var, "usage: csv_export.lua <reader> <out> <board var> <fafnir var> <files...> -- <files...> [-- <files...>]")
local journal_var = os.getenv("COLLECTION_JOURNAL_VAR") or "CollectionJournalDB"
local R = dofile(reader_path)

local board_files, fafnir_files, journal_files = {}, {}, {}
local groups, g = { board_files, fafnir_files, journal_files }, 1
for i = 5, #arg do
    if arg[i] == "--" then g = math.min(g + 1, #groups) else local t = groups[g]; t[#t + 1] = arg[i] end
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

-- positions.csv: every journalled row that has a map and x/y ------------------------------------------------
local map_names = {}
for _, s in ipairs(snaps) do
    if s.map and s.zone then map_names[tostring(s.map)] = s.zone end
end
local points, seen_id = {}, {}
for _, p in ipairs(journal_files) do
    local db = load(p, journal_var)
    if db and type(db.rows) == "table" then
        local lo, hi = tonumber(db.firstSeq) or 1, (tonumber(db.nextSeq) or 1) - 1
        for seq = lo, hi do
            local r = db.rows[seq]
            if type(r) == "table" and r.zone and type(r.x) == "number" and type(r.y) == "number"
               and not seen_id[tostring(r.id)] then
                seen_id[tostring(r.id)] = true
                points[#points + 1] = { tostring(r.character), tonumber(r.time) or 0, tostring(r.zone),
                                        math.floor(r.x * 10000 + 0.5) / 10000, math.floor(r.y * 10000 + 0.5) / 10000,
                                        tonumber(r.type) or 0 }
            end
        end
    end
end
table.sort(points, function(a, b)
    if a[1] ~= b[1] then return a[1] < b[1] end
    return a[2] < b[2]
end)
local prow = {}
for _, q in ipairs(points) do
    prow[#prow + 1] = { q[1], utc(q[2]), q[2], q[3], map_names[q[3]], string.format("%.4f", q[4]), string.format("%.4f", q[5]), q[6] }
end
report.positions = write("positions.csv", { "character", "time_utc", "epoch", "map_id", "zone", "x", "y", "event_type" }, prow)

-- collection.html -------------------------------------------------------------------------------------------
-- One self-contained page (founder, 2026-11-14: "where is my local data showing my characters and inventory? it
-- should be a basic webpage"): the same data as the CSVs, embedded as JSON, so it opens straight from disk with
-- no server and nothing fetched. Rewritten each run.
local function json(v)
    local t = type(v)
    if v == nil then return "null"
    elseif t == "number" then
        if v ~= v or v == math.huge or v == -math.huge then return "null" end
        if v == math.floor(v) and math.abs(v) < 2^53 then return string.format("%d", v) end
        return string.format("%.6g", v)
    elseif t == "boolean" then return tostring(v)
    elseif t == "string" then
        -- "<" as <: no item, zone or character name can close the <script> it sits in
        return '"' .. v:gsub('[%c"\\<>&]', function(c) return string.format("\\u%04x", c:byte()) end) .. '"'
    elseif t == "table" then
        local o = {}
        if v[1] ~= nil or next(v) == nil then
            for i = 1, #v do o[i] = json(v[i]) end
            return "[" .. table.concat(o, ",") .. "]"
        end
        local keys = {}
        for k in pairs(v) do keys[#keys + 1] = k end
        table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
        for _, k in ipairs(keys) do o[#o + 1] = json(tostring(k)) .. ":" .. json(v[k]) end
        return "{" .. table.concat(o, ",") .. "}"
    end
    return "null"
end

local latest, first = {}, {}
for _, s in ipairs(snaps) do
    local who = tostring(s.character)
    latest[who] = s
    first[who] = first[who] or s
end
local cards = {}
for who, s in pairs(latest) do
    local f = first[who]
    cards[#cards + 1] = { name = who, class = s.class, race = s.race, level = s.level, xp = s.xp, xpmax = s.xpmax,
        rested = s.rested, gold = s.gold, zone = s.zone, played = s.played, ilvl = s.ilvl, profs = s.profs, t = s.t,
        firstLevel = f.level, firstT = f.t, seen = seen[who] }
end
for who, t in pairs(seen) do   -- characters Fafnir's Bed knows but the Character Board has no snapshot for
    if not latest[who] then cards[#cards + 1] = { name = who, seen = t } end
end
table.sort(cards, function(a, b) return a.name < b.name end)
local items = {}
for _, h in ipairs(held) do items[#items + 1] = { h[1], h[2], h[3], names[h[3]] or "", h[4] } end
local totals = {}
for _, r in ipairs(nrow) do totals[#totals + 1] = { r[1], r[2] or "", r[3], r[4] } end
local data = json({ exported = utc(now), characters = cards, items = items, totals = totals,
                    points = points, mapNames = map_names })

local page = [==[<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Collection</title>
<style>
:root{--bg:#f7f5f0;--card:#fff;--ink:#1d1b16;--mute:#6b665b;--line:#e4dfd3;--gold:#8c6b27;--bar:#d9c79a}
@media (prefers-color-scheme:dark){:root{--bg:#16150f;--card:#211f18;--ink:#ece6d6;--mute:#a39c8a;--line:#36322a;--gold:#d8b25e;--bar:#5a4a26}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 system-ui,sans-serif}
main{max-width:1100px;margin:0 auto;padding:20px 16px 48px}h1{font-size:24px;margin:0 0 2px}h2{font-size:18px;margin:28px 0 10px}
.mute{color:var(--mute)}.cards{display:grid;grid-template-columns:repeat(auto-fill,minmax(250px,1fr));gap:12px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px}
.card h3{margin:0;font-size:16px}.lv{font-size:28px;font-weight:600;color:var(--gold)}
.xp{height:6px;background:var(--line);border-radius:3px;overflow:hidden;margin:6px 0 8px}.xp i{display:block;height:100%;background:var(--bar)}
dl{display:grid;grid-template-columns:auto 1fr;gap:2px 10px;margin:0;font-size:13px}dt{color:var(--mute)}dd{margin:0}
.bar{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:10px}select,input{font:inherit;padding:6px 8px;border:1px solid var(--line);border-radius:6px;background:var(--card);color:var(--ink)}
input{flex:1;min-width:160px}.wrap{overflow-x:auto;background:var(--card);border:1px solid var(--line);border-radius:10px}
table{border-collapse:collapse;width:100%;font-size:14px}th,td{text-align:left;padding:7px 10px;border-bottom:1px solid var(--line);white-space:nowrap}
th{color:var(--mute);font-weight:500}td.n{text-align:right;font-variant-numeric:tabular-nums}tr:last-child td{border-bottom:0}
.mapbox{background:var(--card);border:1px solid var(--line);border-radius:10px;overflow:hidden}svg{display:block;width:100%;height:auto}
.grid{stroke:var(--line);stroke-width:1}.trail{fill:none;stroke-width:2;stroke-linejoin:round;opacity:.55}.ring{fill:none;stroke-width:2.5}
.legend{margin-top:8px;font-size:13px}.legend span{display:inline-flex;align-items:center;gap:6px;margin-right:14px}.legend i{width:10px;height:10px;border-radius:50%}
</style></head><body><main>
<h1>Collection</h1><div class="mute" id="when"></div>
<h2>Characters</h2><div class="cards" id="cards"></div>
<h2>Maps</h2>
<div class="bar"><select id="map"></select><select id="mwho"><option value="">Every character</option></select></div>
<div class="mapbox"><svg id="svg" viewBox="0 0 1000 667" role="img" aria-label="Where each character has been on this map"></svg></div>
<div class="legend" id="legend"></div><div class="mute" id="mapnote"></div>
<h2>Inventory</h2>
<div class="bar"><select id="who"><option value="">Every character</option></select>
<select id="loc"><option value="">Every location</option></select><input id="q" type="search" placeholder="Search items"></div>
<div class="wrap"><table><thead><tr><th>Item</th><th class="n">Count</th><th>Where</th><th>Character</th></tr></thead><tbody id="inv"></tbody></table></div>
<h2>Account totals</h2>
<div class="wrap"><table><thead><tr><th>Item</th><th class="n">Total</th><th>Held by</th></tr></thead><tbody id="tot"></tbody></table></div>
</main>
<script id="data" type="application/json">__DATA__</script>
<script>
var D=JSON.parse(document.getElementById("data").textContent);
function esc(s){return String(s==null?"":s).replace(/[&<>"]/g,function(c){return{"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]})}
function gold(c){if(c==null)return"";c=Math.floor(c);return Math.floor(c/10000)+"g "+Math.floor(c/100)%100+"s "+c%100+"c"}
function dur(s){if(!s)return"";var d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return(d?d+"d ":"")+h+"h "+m+"m"}
function day(t){return t?new Date(t*1000).toLocaleString():""}
function nm(n){return n?n.split("-")[0]:""}
document.getElementById("when").textContent="Exported "+(D.exported?new Date(D.exported).toLocaleString():"")+" · "+D.characters.length+" characters, "+D.items.length+" item stacks, "+(D.points||[]).length+" positions";
var cards=document.getElementById("cards");
if(!D.characters.length)cards.innerHTML='<p class="mute">No characters yet: log in and out once with the Character Board installed, then run the export again.</p>';
D.characters.forEach(function(c){
  var pct=c.xpmax?Math.min(100,c.xp*100/c.xpmax):0;
  var dl=[["Gold",gold(c.gold)],["Zone",c.zone],["Played",dur(c.played)],["Item level",c.ilvl||""],["Professions",c.profs],
    ["Since",c.firstT&&c.firstLevel!=null?"level "+c.firstLevel+" on "+day(c.firstT):""],["Last seen",day(c.t||c.seen)]];
  cards.insertAdjacentHTML("beforeend",'<div class="card"><h3>'+esc(nm(c.name))+'</h3><div class="mute">'+esc([c.race,c.class&&c.class.charAt(0)+c.class.slice(1).toLowerCase()].filter(Boolean).join(" "))+" · "+esc((c.name.split("-")[1])||"")+'</div>'+
    (c.level!=null?'<div class="lv">'+esc(c.level)+'</div><div class="xp" title="'+pct.toFixed(1)+'% to next level"><i style="width:'+pct+'%"></i></div>':'')+
    '<dl>'+dl.filter(function(r){return r[1]!==""&&r[1]!=null}).map(function(r){return"<dt>"+r[0]+"</dt><dd>"+esc(r[1])+"</dd>"}).join("")+'</dl></div>');
});
var who=document.getElementById("who"),loc=document.getElementById("loc"),q=document.getElementById("q");
function opts(sel,vals){vals.forEach(function(v){var o=document.createElement("option");o.value=v;o.textContent=sel===who?nm(v):v;sel.appendChild(o)})}
function uniq(i){var s={};D.items.forEach(function(r){s[r[i]]=1});return Object.keys(s).sort()}
opts(who,uniq(0));opts(loc,uniq(1));
function iname(r){return r[3]||"item #"+r[2]}
function draw(){
  var w=who.value,l=loc.value,s=q.value.trim().toLowerCase();
  var rows=D.items.filter(function(r){return(!w||r[0]===w)&&(!l||r[1]===l)&&(!s||iname(r).toLowerCase().indexOf(s)>=0||String(r[2])===s)});
  rows.sort(function(a,b){return iname(a).localeCompare(iname(b))||a[0].localeCompare(b[0])});
  document.getElementById("inv").innerHTML=rows.length?rows.map(function(r){return"<tr><td>"+esc(iname(r))+'</td><td class="n">'+esc(r[4])+"</td><td>"+esc(r[1])+"</td><td>"+esc(nm(r[0]))+"</td></tr>"}).join(""):'<tr><td colspan="4" class="mute">Nothing matches.</td></tr>';
  var t=D.totals.filter(function(r){return!s||(r[1]||"").toLowerCase().indexOf(s)>=0||String(r[0])===s});
  t.sort(function(a,b){return b[2]-a[2]});
  document.getElementById("tot").innerHTML=t.length?t.map(function(r){return"<tr><td>"+esc(r[1]||"item #"+r[0])+'</td><td class="n">'+esc(r[2])+"</td><td>"+esc(r[3].split("; ").map(function(h){return h.replace(/-[^ ]+/,"")}).join(", "))+"</td></tr>"}).join(""):'<tr><td colspan="3" class="mute">Nothing matches.</td></tr>';
}
[who,loc].forEach(function(e){e.addEventListener("change",draw)});q.addEventListener("input",draw);draw();
// maps: points are [character, epoch, map id, x 0..1, y 0..1, event type]
var P=D.points||[],MN=D.mapNames||{},COL=["#2f7ed8","#d9534f","#3a9d5d","#a26bd1","#e08e0b","#1aa3a3","#c2477e","#7a8b99"];
var mapSel=document.getElementById("map"),mwho=document.getElementById("mwho"),svg=document.getElementById("svg");
var chars=[],ci={};P.forEach(function(p){if(!(p[0]in ci)){ci[p[0]]=chars.length;chars.push(p[0])}});
function colr(c){return COL[ci[c]%COL.length]}
function mname(m){return MN[m]||"Map "+m}
var counts={};P.forEach(function(p){counts[p[2]]=(counts[p[2]]||0)+1});
Object.keys(counts).sort(function(a,b){return counts[b]-counts[a]}).forEach(function(m){var o=document.createElement("option");o.value=m;o.textContent=mname(m)+" ("+counts[m]+")";mapSel.appendChild(o)});
chars.forEach(function(c){var o=document.createElement("option");o.value=c;o.textContent=nm(c);mwho.appendChild(o)});
function drawMap(){
  var m=mapSel.value,w=mwho.value,h=[],W=1000,H=667,i;
  for(i=1;i<10;i++){h.push('<line class="grid" x1="'+i*W/10+'" y1="0" x2="'+i*W/10+'" y2="'+H+'"/><line class="grid" x1="0" y1="'+i*H/10+'" x2="'+W+'" y2="'+i*H/10+'"/>')}
  var pts=P.filter(function(p){return p[2]===m&&(!w||p[0]===w)}),by={};
  pts.forEach(function(p){(by[p[0]]=by[p[0]]||[]).push(p)});
  Object.keys(by).forEach(function(c){
    var a=by[c],seg=[],col=colr(c);
    function flush(){if(seg.length>1)h.push('<polyline class="trail" stroke="'+col+'" points="'+seg.join(" ")+'"/>');seg=[]}
    a.forEach(function(p,k){if(k&&p[1]-a[k-1][1]>600)flush();seg.push((p[3]*W).toFixed(1)+","+(p[4]*H).toFixed(1))});flush();
    a.forEach(function(p,k){var last=k===a.length-1;
      h.push('<circle cx="'+(p[3]*W).toFixed(1)+'" cy="'+(p[4]*H).toFixed(1)+'" r="'+(last?6:3.5)+'" fill="'+col+'"><title>'+esc(nm(c))+" · "+esc(day(p[1]))+" · "+(p[3]*100).toFixed(1)+", "+(p[4]*100).toFixed(1)+" · event "+esc(p[5])+'</title></circle>'+(last?'<circle class="ring" cx="'+(p[3]*W).toFixed(1)+'" cy="'+(p[4]*H).toFixed(1)+'" r="10" stroke="'+col+'"/>':""))});
  });
  svg.innerHTML=h.join("");
  document.getElementById("legend").innerHTML=Object.keys(by).map(function(c){return'<span><i style="background:'+colr(c)+'"></i>'+esc(nm(c))+" · "+by[c].length+" points</span>"}).join("");
  document.getElementById("mapnote").textContent=P.length?"Coordinates are the in-game map's 0–100 grid. The ringed dot is the most recent position.":"No positions yet. The Collection Journal records where you are with every event: play a session, log out, and run the export again.";
}
[mapSel,mwho].forEach(function(e){e.addEventListener("change",drawMap)});drawMap();
</script></body></html>
]==]
do
    local f = assert(io.open(out_dir .. "/collection.html", "wb"))
    f:write((page:gsub("__DATA__", function() return data end)))
    f:close()
end

print(string.format("csv-export: %d character snapshots, %d bag rows, %d history rows appended, %d items net, %d positions -> %s (open collection.html)",
    report.characters, report.bags, report.history, report.net, report.positions, out_dir))
os.exit(problems == 0 and 0 or 1)
