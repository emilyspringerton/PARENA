-- runtime/wow/collection_hub.lua -- the Collection hub: one window with the settings and status of every addon in
-- this package (founder, 2026-11-14: "give like some interfaces to see the settings for the different components"),
-- and its control panel (founder, 2026-11-14: "a control surface inside of wow, some kind of window, as I discover
-- the slash commands"). What it reports and what a button does are decided in PARENA (stdlib/collection/hub.prn);
-- this file only makes the client calls.
--
-- /collection        open or close the window
-- /collection list   the same, in chat, with every command
--
-- One block per addon from registry.lua (written by the packager from each addon's own .toc and code, so it never
-- drifts from what was built): loaded or not, where it saves and how much, and a button for every command and
-- subcommand the addon documents (scripts/wow-slash-actions.awk). Hovering a button shows the full command and
-- what it does. A button runs its command at once; one that needs something only the player can type (a name, a
-- riddle: `<...>` in its usage) puts the command in the chat box instead, ready to finish. An addon that is not
-- loaded shows no buttons. A header shows the build's settings (version, interface, the journal's cap and poll)
-- and the REFLUX bus. Shown by /collection, and as the "Collection" category of the client's options window when
-- there is one.
--
-- Client calls (feature-tested; none probed on Forever yet): CreateFrame, UIParent, C_AddOns.IsAddOnLoaded or
-- IsAddOnLoaded, Settings.RegisterCanvasLayoutCategory + Settings.RegisterAddOnCategory (retail) or
-- InterfaceOptions_AddCategory (older clients), UISpecialFrames (Escape closes the window), UIPanelButtonTemplate,
-- GameTooltip (the hover text), ChatFrame_OpenChat or DEFAULT_CHAT_FRAME.editBox (a command left to finish; with
-- neither, the usage is printed instead).

local ADDON = "@ADDON@"
local VAR = "@VAR@"
local _, ns = ...

local H = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/hub"]

local function L(key)
    local c = ns and ns.catalogue
    if not c then return key end
    local t = c[GetLocale and GetLocale() or "enUS"]
    return (t and t[key]) or (c.enUS and c.enUS[key]) or key
end

if not (H and ns.addons and ns.build) then
    print("|cffff5555" .. string.format(L("MISSING"), ADDON) .. "|r")
    return
end

local GOLD, GREY, GREEN = "|cffd8b25e", "|cff9d9d9d", "|cff7fd17f"

local function is_loaded(a)
    if C_AddOns and C_AddOns.IsAddOnLoaded then return C_AddOns.IsAddOnLoaded(a.folder) and 1 or 0 end
    if IsAddOnLoaded then return IsAddOnLoaded(a.folder) and 1 or 0 end
    if a.sv ~= "" and _G[a.sv] ~= nil then return 1 end
    if a.key ~= "" and SlashCmdList and SlashCmdList[a.key] then return 1 end
    return 0
end

local function saved(a)
    local db = a.sv ~= "" and _G[a.sv] or nil
    if type(db) ~= "table" then db = nil end
    return H.hubSize(db), db
end

local function sorted()
    local list = {}
    for _, a in ipairs(ns.addons) do list[#list + 1] = a end
    table.sort(list, function(x, y) return x.title < y.title end)
    return list
end

-- The two lines of one addon's row, as plain text and with colour.
local function describe(a)
    local loaded = is_loaded(a)
    local size, db = saved(a)
    local state = H.hubState(loaded, size)
    local status = state == 0 and (GREY .. L("NOTLOADED") .. "|r") or (GREEN .. L("LOADED") .. "|r")
    local where
    if a.sv == "" then where = L("NOSAVE")
    else
        where = string.format(a.svkind == "SavedVariablesPerCharacter" and L("PERCHAR") or L("ACCOUNT"), a.sv)
        if size < 0 then where = where .. " -- " .. L("NOTHING")
        elseif db and db.nextSeq then where = where .. " -- " .. string.format(L("ROWS"), size)
        else where = where .. " -- " .. string.format(L("ENTRIES"), size) end
    end
    local cmds = #a.cmds > 0 and table.concat(a.cmds, "  ") or L("NOCMD")
    return GOLD .. a.title .. "|r  " .. status, cmds .. "   " .. GREY .. where .. "|r", state
end

-- An addon's buttons: what it documents, or, when it documents nothing, its first command on its own.
local function actions(a)
    if a.actions and #a.actions > 0 then return a.actions end
    if a.key ~= "" and a.cmds[1] then return { { cmd = a.cmds[1], sub = "", args = "", desc = "" } } end
    return {}
end

local function needs_input(act) return act.args:find("<", 1, true) and 1 or 0 end

-- The command as it would be typed, with and without its arguments.
local function typed(act) return act.cmd .. (act.sub ~= "" and (" " .. act.sub) or "") end
local function usage(act) return typed(act) .. (act.args ~= "" and (" " .. act.args) or "") end

-- A button's label: the subcommand, else the bare command; "..." when the player still has something to type.
local function label(act)
    local t = act.sub ~= "" and act.sub or (act.args ~= "" and act.args or act.cmd)
    return needs_input(act) == 1 and (t .. "...") or t
end

local function chat_prefill(text)
    if ChatFrame_OpenChat then ChatFrame_OpenChat(text); return true end
    local box = DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.editBox
    if box and box.Show and box.SetText then
        box:Show(); box:SetText(text)
        if box.SetFocus then box:SetFocus() end
        return true
    end
    return false
end

-- What a button does, decided by hub-action-mode: 1 runs the command, 2 leaves it in the chat box.
local function press(a, act)
    local mode = H.hubActionMode(is_loaded(a), needs_input(act))
    if mode == 1 then
        local fn = a.key ~= "" and SlashCmdList and SlashCmdList[a.key]
        if fn then fn(act.sub) end
    elseif mode == 2 then
        if not chat_prefill(typed(act) .. " ") then print(string.format(L("TYPEIT"), usage(act))) end
    end
    return mode
end

local function header()
    local b = ns.build
    local line1 = string.format(L("BUILD"), tostring(b.version), tostring(b.interface), b.keep, b.poll)
    local line2 = ""
    if RefluxHost and RefluxHost.total_dispatched then
        line2 = string.format(L("BUS"), RefluxHost.total_dispatched(), RefluxHost.log_size())
    end
    return line1, line2
end

local function list_in_chat()
    local h1, h2 = header()
    print(GOLD .. L("TITLE") .. "|r  " .. h1)
    if h2 ~= "" then print("  " .. h2) end
    for _, a in ipairs(sorted()) do
        local one, two = describe(a)
        print(one)
        print("    " .. two)
        for _, act in ipairs(actions(a)) do
            print("      " .. GOLD .. usage(act) .. "|r" .. (act.desc ~= "" and ("  " .. GREY .. act.desc .. "|r") or ""))
        end
    end
end

-- Fill `parent` with the header and one block per addon -- its two lines, then its command buttons, wrapped to the
-- width; returns a function that refreshes every text and shows the buttons of the addons that are loaded.
local LINE, BTN_H, BTN_GAP, BLOCK_GAP = 17, 22, 4, 10
local function button_width(text) return math.max(56, 16 + 7 * string.len(text)) end

local function fill(parent, width)
    local function text(p, template, x, y, w)
        local fs = p:CreateFontString(nil, "OVERLAY", template)
        fs:SetPoint("TOPLEFT", p, "TOPLEFT", x, -y)
        if fs.SetWidth then fs:SetWidth(w) end
        if fs.SetJustifyH then fs:SetJustifyH("LEFT") end
        return fs
    end
    local title = text(parent, "GameFontNormalLarge", 16, 14, width - 32)
    title:SetText(L("TITLE"))
    local h1 = text(parent, "GameFontHighlightSmall", 16, 40, width - 32)
    local h2 = text(parent, "GameFontHighlightSmall", 16, 56, width - 32)

    -- the blocks scroll: a plain ScrollFrame with the mouse wheel, so no template is needed
    local top = 80
    local scroll = CreateFrame("ScrollFrame", nil, parent)
    scroll:SetPoint("TOPLEFT", parent, "TOPLEFT", 8, -top)
    scroll:SetPoint("BOTTOMRIGHT", parent, "BOTTOMRIGHT", -8, 8)
    local body = CreateFrame("Frame", nil, scroll)
    scroll:SetScrollChild(body)

    local inner = width - 16
    local rows, y = {}, 4
    for _, a in ipairs(sorted()) do
        local r = { addon = a, buttons = {} }
        r.one = text(body, "GameFontHighlight", 8, y, inner - 16)
        r.two = text(body, "GameFontHighlightSmall", 8, y + LINE, inner - 16)
        y = y + 2 * LINE + 2
        local x = 8
        local acts = actions(a)
        for _, act in ipairs(acts) do
            local w = button_width(label(act))
            if x > 8 and x + w > inner - 8 then x = 8; y = y + BTN_H + BTN_GAP end
            local ok, b = pcall(CreateFrame, "Button", nil, body, "UIPanelButtonTemplate")
            if not ok then b = CreateFrame("Button", nil, body) end
            b:SetSize(w, BTN_H)
            b:SetPoint("TOPLEFT", body, "TOPLEFT", x, -y)
            b:SetText(label(act))
            b.action = act
            b:SetScript("OnClick", function() press(a, act) end)
            b:SetScript("OnEnter", function(self)
                if not (GameTooltip and GameTooltip.SetOwner) then return end
                GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
                GameTooltip:SetText(usage(act))
                if act.desc ~= "" then GameTooltip:AddLine(act.desc, 1, 1, 1, true) end
                if needs_input(act) == 1 then GameTooltip:AddLine(L("FINISH"), 0.62, 0.62, 0.62, true) end
                GameTooltip:Show()
            end)
            b:SetScript("OnLeave", function() if GameTooltip and GameTooltip.Hide then GameTooltip:Hide() end end)
            b:Hide()                         -- shown by the refresh, once it knows the addon is loaded
            r.buttons[#r.buttons + 1] = b
            x = x + w + BTN_GAP
        end
        if #acts > 0 then y = y + BTN_H end
        y = y + BLOCK_GAP
        rows[#rows + 1] = r
    end
    local total = y + 4
    body:SetSize(inner, total)
    if scroll.EnableMouseWheel then
        scroll:EnableMouseWheel(true)
        scroll:SetScript("OnMouseWheel", function(self, delta)
            local max = math.max(0, total - (self.GetHeight and self:GetHeight() or 0))
            local v = math.min(max, math.max(0, (self:GetVerticalScroll() or 0) - delta * 60))
            self:SetVerticalScroll(v)
        end)
    end

    return function()
        local a1, a2 = header()
        h1:SetText(a1)
        h2:SetText(a2)
        for _, r in ipairs(rows) do
            local one, two, state = describe(r.addon)
            r.one:SetText(one)
            r.two:SetText(two)
            for _, b in ipairs(r.buttons) do if state == 0 then b:Hide() else b:Show() end end
        end
    end
end

local window
local function make_window()
    if window or not (CreateFrame and UIParent) then return window end
    local f = CreateFrame("Frame", "CollectionHubFrame", UIParent)
    f:SetSize(640, 560)
    f:SetPoint("CENTER", UIParent, "CENTER", 0, 40)
    if f.SetFrameStrata then f:SetFrameStrata("DIALOG") end
    local bg = f:CreateTexture(nil, "BACKGROUND")
    bg:SetAllPoints(f)
    if bg.SetColorTexture then bg:SetColorTexture(0.06, 0.05, 0.04, 0.92) else bg:SetTexture(0.06, 0.05, 0.04, 0.92) end
    f:EnableMouse(true)
    f:SetMovable(true)
    f:RegisterForDrag("LeftButton")
    f:SetScript("OnDragStart", function(self) self:StartMoving() end)
    f:SetScript("OnDragStop", function(self) self:StopMovingOrSizing() end)
    local ok, close = pcall(CreateFrame, "Button", nil, f, "UIPanelCloseButton")
    if not ok then
        close = CreateFrame("Button", nil, f)
        close:SetSize(60, 20)
        close:SetText(L("CLOSE"))
    end
    close:SetPoint("TOPRIGHT", f, "TOPRIGHT", -4, -4)
    close:SetScript("OnClick", function() f:Hide() end)
    local refresh = fill(f, 640)
    f:SetScript("OnShow", refresh)
    if UISpecialFrames then table.insert(UISpecialFrames, "CollectionHubFrame") end
    f:Hide()
    window = f
    return f
end

-- The client's options window: a "Collection" category with the same rows.
local function register_options()
    if not (CreateFrame and UIParent) then return "none" end
    local panel = CreateFrame("Frame", nil, UIParent)
    panel.name = L("TITLE")
    panel:SetSize(640, 520)
    local refresh = fill(panel, 620)
    panel:SetScript("OnShow", refresh)
    panel:Hide()
    if Settings and Settings.RegisterCanvasLayoutCategory and Settings.RegisterAddOnCategory then
        local category = Settings.RegisterCanvasLayoutCategory(panel, L("TITLE"))
        Settings.RegisterAddOnCategory(category)
        return "settings"
    elseif InterfaceOptions_AddCategory then
        InterfaceOptions_AddCategory(panel)
        return "interfaceoptions"
    end
    return "none"
end

local f = CreateFrame and CreateFrame("Frame")
if f then
    f:RegisterEvent("ADDON_LOADED")
    f:SetScript("OnEvent", function(_, _, name)
        if name == ADDON then
            _G[VAR] = type(_G[VAR]) == "table" and _G[VAR] or {}
            _G[VAR].options = register_options()
        end
    end)
end

SLASH_COLLECTIONHUB1 = "/collection"
SLASH_COLLECTIONHUB2 = "/coll"
SlashCmdList["COLLECTIONHUB"] = function(msg)
    msg = (msg or ""):match("^%s*(.-)%s*$"):lower()
    if msg == "list" then return list_in_chat() end
    if msg ~= "" then print(L("HELP")); return end
    local w = make_window()
    if not w then
        print(string.format(L("NOFRAMES"), ADDON))
        return list_in_chat()
    end
    if w:IsShown() then w:Hide() else w:Show() end
end

-- for tests and other addons
ns.describe, ns.list_in_chat, ns.actions, ns.press = describe, list_in_chat, actions, press
