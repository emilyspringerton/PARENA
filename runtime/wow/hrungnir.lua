-- runtime/wow/hrungnir.lua -- Hrungnir's host glue (spec addon 70, Pet Rock).
--
-- The rock is PARENA (stdlib/collection/hrungnir.prn, RefluxCore.modules["collection/hrungnir"]): points, levels,
-- mood and which line it says. This file feeds it the moments that matter, draws the tiny frame and prints its
-- remarks. Every line of text comes from the catalogue (runtime/wow/locales/hrungnir.lua).
--
-- Client calls used: CreateFrame (+ CreateFontString, SetPoint, SetSize, Show/Hide, and the drag calls when the
-- frame has them), RegisterEvent/SetScript, SlashCmdList, GetLocale, time, print. Events: ADDON_LOADED,
-- PLAYER_LOGIN, QUEST_TURNED_IN, PLAYER_LEVEL_UP, PLAYER_DEAD. All retail-lineage; none probed on Forever yet
-- beyond what the Collection client already uses.

local ADDON = "@ADDON@"
local VAR = "@VAR@"
local _, ns = ...

local R = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/hrungnir"]
if not R then
    print("|cffff5555" .. ADDON .. ": RefluxCore's collection/hrungnir module is missing; the rock stays home.|r")
    return
end

local function L(key)
    local c = ns and ns.catalogue
    if not c then return key end
    local t = c[GetLocale and GetLocale() or "enUS"]
    return (t and t[key]) or (c.enUS and c.enUS[key]) or key
end

local db = nil
local face, label = nil, nil
local box = nil

local function say(moment)
    if not db then return end
    local i = R.rockComment(moment, time() + db.points)
    if i > 0 then print(string.format(L("SAYS"), L("NAME"), L("C" .. moment .. "_" .. i))) end
end

local function redraw()
    if not (db and face and label) then return end
    face:SetText(L("FACE_" .. R.rockMood(db, time())))
    label:SetText(string.format(L("LEVEL"), db.level))
end

local function gained(level_up)
    if level_up and level_up > 0 then
        print(string.format(L("LEVEL_UP"), L("NAME"), level_up))
        say(1)
    end
    redraw()
end

local function build_frame()
    if box or not CreateFrame then return end
    box = CreateFrame("Frame", ADDON .. "Frame", UIParent)
    if box.SetSize then box:SetSize(64, 40) end
    if box.SetPoint then box:SetPoint("CENTER", UIParent, "CENTER", 0, -180) end
    if box.SetMovable then
        box:SetMovable(true)
        box:EnableMouse(true)
        box:RegisterForDrag("LeftButton")
        box:SetScript("OnDragStart", box.StartMoving)
        box:SetScript("OnDragStop", box.StopMovingOrSizing)
    end
    if box.SetScript then
        box:SetScript("OnMouseUp", function(_, button)
            if button == "RightButton" and SlashCmdList.HRUNGNIR then SlashCmdList.HRUNGNIR("pat") end
        end)
    end
    if box.CreateFontString then
        face = box:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
        face:SetPoint("TOP", box, "TOP", 0, 0)
        label = box:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        label:SetPoint("BOTTOM", box, "BOTTOM", 0, 0)
    end
    if db and db.hidden and box.Hide then box:Hide() end
    redraw()
end

local events = CreateFrame("Frame")
events:RegisterEvent("ADDON_LOADED")
events:RegisterEvent("PLAYER_LOGIN")
events:RegisterEvent("QUEST_TURNED_IN")
events:RegisterEvent("PLAYER_LEVEL_UP")
events:RegisterEvent("PLAYER_DEAD")
events:SetScript("OnEvent", function(_, event, arg1)
    if event == "ADDON_LOADED" then
        if arg1 == ADDON then
            _G[VAR] = R.rockInit(_G[VAR] or {}, time())
            db = _G[VAR]
        end
    elseif not db then
        return
    elseif event == "PLAYER_LOGIN" then
        db.lastidle = time()
        build_frame()
        say(6)
    elseif event == "QUEST_TURNED_IN" then
        gained(R.rockGain(db, 2, 1, time()))
    elseif event == "PLAYER_LEVEL_UP" then
        gained(R.rockGain(db, 3, 1, time()))
    elseif event == "PLAYER_DEAD" then
        R.rockDied(db, time())
        say(4)
        redraw()
    end
end)

-- a minute online is a pebble point; every 15 minutes the rock may remark on things
local acc = 0
events:SetScript("OnUpdate", function(_, elapsed)
    if not db then return end
    acc = acc + (elapsed or 0)
    if acc >= 60 then
        local minutes = math.floor(acc / 60)
        acc = acc - minutes * 60
        gained(R.rockGain(db, 1, minutes, time()))
        if R.rockIdleDue(db, time()) == 1 then say(5) end
    end
end)

SLASH_HRUNGNIR1 = "/rock"
SLASH_HRUNGNIR2 = "/hrungnir"
SlashCmdList["HRUNGNIR"] = function(msg)
    if not db then print(ADDON .. ": not loaded yet."); return end
    msg = (msg or ""):match("^%s*(.-)%s*$"):lower()
    if msg == "pat" then
        local r = R.rockPat(db, time())
        if r < 0 then say(3) else say(2); gained(r) end
    elseif msg == "hide" then
        db.hidden = true
        if box and box.Hide then box:Hide() end
        print(L("HIDDEN"))
    elseif msg == "show" then
        db.hidden = nil
        build_frame()
        if box and box.Show then box:Show() end
    else
        local p = R.rockProgress(db)
        print(string.format(L("STATUS"), L("NAME"), p[1], p[2], p[4] - p[2], db.pats, db.deaths))
        print("  " .. L("MOOD_" .. R.rockMood(db, time())))
        print("  " .. L("HELP"))
    end
end
