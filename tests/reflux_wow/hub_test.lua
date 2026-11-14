-- tests/reflux_wow/hub_test.lua -- the Collection hub (/collection) in the packaged client, under real Lua 5.1, against
-- a stubbed client with frames, font strings and buttons. usage: lua5.1 hub_test.lua <client dir>
assert(_VERSION == "Lua 5.1", "must run under Lua 5.1")
local client = assert(arg[1])

local passed, failed = 0, 0
local function eq(got, want, what)
    if got == want then passed = passed + 1
    else failed = failed + 1; print("FAIL: " .. what .. " (got " .. tostring(got) .. ", want " .. tostring(want) .. ")") end
end
local function has(s, sub, what) eq(type(s) == "string" and s:find(sub, 1, true) ~= nil, true, what .. " [" .. tostring(s):sub(1, 300) .. "]") end

local function toc_files(dir, name)
    local files = {}
    for line in io.lines(dir .. "/" .. name .. ".toc") do
        if line ~= "" and not line:match("^#") then files[#files + 1] = line end
    end
    return files
end

-- a widget that records what it is told; any method the hub calls that the stub does not model is a no-op
local function widget(kind, all)
    local w = { kind = kind, shown = true, scripts = {}, events = {} }
    function w:SetText(s) rawset(self, "text", s) end
    function w:GetText() return rawget(self, "text") end
    function w:SetScript(k, fn) self.scripts[k] = fn end
    function w:Show() rawset(self, "shown", true); if self.scripts.OnShow then self.scripts.OnShow(self) end end
    function w:Hide() rawset(self, "shown", false) end
    function w:IsShown() return rawget(self, "shown") end
    function w:RegisterEvent(e) self.events[e] = true end
    function w:GetHeight() return 400 end
    function w:GetVerticalScroll() return rawget(self, "scroll") or 0 end
    function w:SetVerticalScroll(v) rawset(self, "scroll", v) end
    function w:CreateFontString() local f = widget("FontString", all); all[#all + 1] = f; return f end
    function w:CreateTexture() return widget("Texture", all) end
    function w:Click() if self.scripts.OnClick then self.scripts.OnClick(self) end end
    function w:SetPoint(_, _, _, x, y) rawset(self, "x", x); rawset(self, "y", y) end
    function w:SetSize(wd, ht) rawset(self, "w", wd); rawset(self, "h", ht) end
    return setmetatable(w, { __index = function() return function() end end })
end

local function session(opts)
    local out, all, frames = {}, {}, {}
    local env = { print = function(...)
        local t = {}
        for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
        out[#out + 1] = table.concat(t, " ")
    end }
    for _, k in ipairs({ "pairs", "ipairs", "type", "tostring", "tonumber", "select", "string", "table", "math",
                         "next", "error", "assert", "pcall", "unpack", "setmetatable", "getmetatable", "rawget", "rawset" }) do env[k] = _G[k] end
    env._G = env
    env.time = function() return 1763000000 end
    env.GetLocale = function() return "enUS" end
    env.UnitName = function() return "Emily" end
    env.GetRealmName = function() return "Forever" end
    env.UnitClass = function() return "Paladin", "PALADIN" end
    env.InCombatLockdown = function() return false end
    env.MAX_CHARACTER_MACROS = 18
    env.C_Spell = { GetSpellInfo = function(name) if name == "Judgement" then return { name = name } end end }
    local macros = {}
    env.GetNumMacros = function() return 0, #macros end
    env.GetMacroIndexByName = function(n) for i, m in ipairs(macros) do if m.name == n then return i end end return 0 end
    env.GetMacroBody = function(i) return macros[i] and macros[i].body end
    env.CreateMacro = function(n, _, b) macros[#macros + 1] = { name = n, body = b }; return #macros end
    env.EditMacro = function(i, n, _, b) macros[i].body = b; return i end
    env.DeleteMacro = function(i) table.remove(macros, i) end
    env.C_Map = { GetBestMapForUnit = function() return 1412 end, GetPlayerMapPosition = function() return nil end }
    env.SlashCmdList = {}
    env.UISpecialFrames = {}
    env.UIParent = widget("UIParent", all)
    local tip = { lines = {} }
    if not opts.no_tooltip then
        env.GameTooltip = {
            SetOwner = function(self, owner) self.owner = owner; self.lines = {} end,
            SetText = function(self, t) self.lines = { t } end,
            AddLine = function(self, t) self.lines[#self.lines + 1] = t end,
            Show = function(self) self.shown = true end, Hide = function(self) self.shown = false end }
        tip = env.GameTooltip
    end
    local chat = {}
    if not opts.no_chat then env.ChatFrame_OpenChat = function(text) chat[#chat + 1] = text end end
    env.CreateFrame = function(kind, name, parent, template)
        if opts.no_templates and template then error("no template " .. template) end
        local f = widget(kind, all)
        f.template = template
        frames[#frames + 1] = f
        if name then env[name] = f end
        return f
    end
    local loaded = {}
    env.C_AddOns = { IsAddOnLoaded = function(n) return loaded[n] == true end }
    local registered = {}
    if opts.options == "settings" then
        env.Settings = {
            RegisterCanvasLayoutCategory = function(panel, name) registered.panel, registered.name = panel, name; return { name = name } end,
            RegisterAddOnCategory = function(c) registered.category = c end }
    elseif opts.options == "interfaceoptions" then
        env.InterfaceOptions_AddCategory = function(panel) registered.panel, registered.name = panel, panel.name end
    end
    local s = { env = env, out = out, all = all, frames = frames, registered = registered, macros = macros, tip = tip, chat = chat }
    function s.load(addon)
        local dir = client .. "/AddOns/" .. addon
        local ns = {}
        for _, file in ipairs(toc_files(dir, addon)) do
            local chunk = assert(loadfile(dir .. "/" .. file)); setfenv(chunk, env); chunk(addon, ns)
        end
        loaded[addon] = true
        s.ns = ns
    end
    function s.fire(event, ...) for _, f in ipairs(frames) do if f.events[event] and f.scripts.OnEvent then f.scripts.OnEvent(f, event, ...) end end end
    function s.cmd(msg) for i = #out, 1, -1 do out[i] = nil end; env.SlashCmdList.COLLECTIONHUB(msg or ""); return table.concat(out, "\n") end
    function s.texts() local t = {}; for _, f in ipairs(all) do if f.kind == "FontString" and rawget(f, "text") then t[#t + 1] = rawget(f, "text") end end; return table.concat(t, "\n") end
    function s.buttons() local b = {}; for _, f in ipairs(frames) do if f.kind == "Button" and rawget(f, "text") then b[#b + 1] = f end end; return b end
    -- the window's button for one action: its command and label (the options panel has a second set; this is the
    -- window's, which is built last)
    function s.button(cmd, text)
        local found
        for _, b in ipairs(s.buttons()) do
            if b.action and b.action.cmd == cmd and rawget(b, "text") == text then found = b end
        end
        return found
    end
    local addons = { "RefluxCore", "CollectionJournal", "MacroSync", "FafnirsBed", "CollectionHub" }
    for _, a in ipairs(addons) do s.load(a) end
    for _, a in ipairs(addons) do s.fire("ADDON_LOADED", a) end
    return s
end

-- ===== the registry is the package: every addon built, with its own commands and saved variables ===========
local s = session({ options = "settings" })
local reg = {}
for _, a in ipairs(s.ns.addons) do reg[a.folder] = a end
local dirs = 0
for d in io.popen('ls "' .. client .. '/AddOns"'):lines() do if d ~= "CollectionHub" then dirs = dirs + 1 end end
eq(#s.ns.addons, dirs, "one registry row per addon folder in the package (the hub itself excluded)")
eq(reg.MacroSync and table.concat(reg.MacroSync.cmds, ","), "/ascend", "MacroSync's command read from its code")
eq(reg.FafnirsBed and table.concat(reg.FafnirsBed.cmds, ","), "/fafnir,/hoard", "Fafnir's Bed's two commands")
eq(reg.MacroSync and reg.MacroSync.svkind, "SavedVariablesPerCharacter", "MacroSync saves per character")
eq(reg.RefluxCore and reg.RefluxCore.key, "", "RefluxCore has no command")
eq(s.ns.build.keep, 20000, "the journal cap is in the build settings")
eq(s.ns.build.poll, 5, "and the poll interval")

-- ===== the options window gets a Collection category ======================================================
eq(s.registered.name, "Collection", "registered in the client's Settings as 'Collection'")
eq(s.registered.category ~= nil, true, "and added as an addon category")
eq(s.env.CollectionHubDB.options, "settings", "the hub remembers which options API it used")

-- ===== /collection opens the window, with the build settings and one row per addon =======================
s.cmd("")
eq(s.env.CollectionHubFrame and s.env.CollectionHubFrame:IsShown(), true, "/collection shows the window")
local t = s.texts()
has(t, "journal keeps 20000 rows, polls every 5 s", "the header shows the journal's settings")
has(t, "REFLUX: ", "and the REFLUX bus")
has(t, "MacroSync|r  |cff7fd17floaded", "MacroSync is loaded")
has(t, "/ascend", "with its command")
has(t, "saves per character in MacroSyncDB", "and where it saves")
has(t, "Ægir's Catch|r  |cff9d9d9dnot loaded", "an addon not loaded in this session says so")
has(t, "saves per character in CollectionJournalDB -- ", "the journal row says how much it holds")
eq(s.env.UISpecialFrames[1], "CollectionHubFrame", "Escape closes it")

-- ===== the control panel: a button for every command and subcommand the addons document ====================
local acts = {}
for _, act in ipairs(reg.MacroSync.actions) do acts[#acts + 1] = act.cmd .. "|" .. act.sub .. "|" .. act.args end
eq(table.concat(acts, ","), "/ascend||,/ascend|preview|", "MacroSync's two documented commands are in the registry")
eq(#reg.Huginn.actions >= 6, true, "Huginn documents at least six")
local fafnir_args = {}
for _, act in ipairs(reg.FafnirsBed.actions) do fafnir_args[#fafnir_args + 1] = act.sub .. "|" .. act.args end
eq(table.concat(fafnir_args, ","), "|,|<name or item id>", "a bare command and the same command with an argument are two actions")
local wrapped
for _, act in ipairs(reg.AndvarisMap.actions) do if act.sub == "map" then wrapped = act.desc end end
has(wrapped, "the chart goes into the body", "a description wrapped onto the next comment line is joined")
for _, a in ipairs(s.ns.addons) do
    if a.key ~= "" then eq(#s.ns.actions(a) > 0, true, a.folder .. " gets at least one button") end
end

local ascend, preview = s.button("/ascend", "/ascend"), s.button("/ascend", "preview")
eq(ascend ~= nil and preview ~= nil, true, "MacroSync's block has an /ascend and a preview button")
eq(rawget(ascend, "shown"), true, "shown, because MacroSync is loaded")
local before = #s.macros
local got = {}
local orig = s.env.SlashCmdList.MACROSYNC
s.env.SlashCmdList.MACROSYNC = function(msg) got[#got + 1] = msg; return orig(msg) end
preview:Click()
eq(got[1], "preview", "preview runs /ascend preview")
eq(#s.macros, before, "and preview changes nothing")
ascend:Click()
eq(got[2], "", "the bare button runs /ascend")
eq(#s.macros > before, true, "and /ascend wrote macros")

ascend.scripts.OnEnter(ascend)
eq(s.tip.lines[1], "/ascend", "hovering shows the command")
has(s.tip.lines[2], "load this character's class set", "and what it does")
ascend.scripts.OnLeave(ascend)
eq(s.tip.shown, false, "leaving hides the tooltip")

local who = s.button("/fafnir", "<name or item id>...")
eq(who ~= nil, true, "a command that needs input is labelled with ...")
local fafnir_ran = false
local forig = s.env.SlashCmdList.FAFNIRSBED
s.env.SlashCmdList.FAFNIRSBED = function(msg) fafnir_ran = true; return forig(msg) end
who:Click()
eq(fafnir_ran, false, "it does not run")
eq(s.chat[#s.chat], "/fafnir ", "it puts the command in the chat box to finish")
who.scripts.OnEnter(who)
eq(s.tip.lines[1], "/fafnir <name or item id>", "its tooltip shows the full usage")
has(s.tip.lines[3], "type the rest", "and says to type the rest")

local hidden, shown = 0, 0
for _, b in ipairs(s.buttons()) do
    if b.action and b.action.cmd == "/derby" then if rawget(b, "shown") then shown = shown + 1 else hidden = hidden + 1 end end
end
eq(hidden > 0 and shown == 0, true, "an addon that is not loaded shows no buttons")
local overflow = 0
for _, b in ipairs(s.buttons()) do
    if b.action and (rawget(b, "x") + rawget(b, "w")) > 624 then overflow = overflow + 1 end
end
eq(overflow, 0, "every button fits the window's width (rows wrap)")

s.cmd("")
eq(s.env.CollectionHubFrame:IsShown(), false, "/collection again closes it")

-- ===== /collection list prints the same in chat ===========================================================
local out = s.cmd("list")
has(out, "Collection|r  Build ", "the list starts with the build line")
has(out, "/fafnir  /hoard", "Fafnir's commands are listed")
has(out, "/derby start [minutes]", "every documented command is listed with its arguments")
has(out, "a timed derby for the whole group", "and what it does")
out = s.cmd("help")
has(out, "/collection list", "anything else prints help")

-- ===== older clients: InterfaceOptions; none at all; no templates; no frames ===============================
local o = session({ options = "interfaceoptions" })
eq(o.registered.name, "Collection", "InterfaceOptions_AddCategory gets the panel when Settings is missing")
local n = session({})
eq(n.env.CollectionHubDB.options, "none", "no options API: the hub still loads")
n.cmd("")
eq(n.env.CollectionHubFrame:IsShown(), true, "and /collection still opens the window")
local nt = session({ no_templates = true, options = "settings" })
nt.cmd("")
eq(nt.env.CollectionHubFrame:IsShown(), true, "no button templates: plain buttons instead")
-- no chat-box API: a command that needs input is printed for the player to type
local nc = session({ no_chat = true, no_tooltip = true })
nc.cmd("")
for i = #nc.out, 1, -1 do nc.out[i] = nil end
nc.button("/fafnir", "<name or item id>..."):Click()
has(table.concat(nc.out, "\n"), "Type it in chat: /fafnir <name or item id>", "no chat box: the usage is printed instead")
local b2 = nc.button("/ascend", "/ascend")
b2.scripts.OnEnter(b2)
eq(true, true, "no GameTooltip: hovering does nothing, and does not error")
local nf = session({})
nf.env.CreateFrame = nil          -- no frames when the window is first asked for
out = nf.cmd("")
has(out, "no frames API", "no frames at all: says so")
has(out, "MacroSync", "and prints the list in chat")

print(string.format("hub: %d passed, %d failed", passed, failed))
os.exit(failed == 0 and 0 or 1)
