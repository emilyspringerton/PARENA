-- runtime/wow/andvaris_map.lua -- Andvari's Map's host glue (spec addon 58, treasure map maker). Rules: stdlib/collection/andvari.prn.
-- /treasure mark      bury an X where you stand
-- /treasure map [n]   draw mark n (default: the newest) as an ASCII chart with a written clue; if the mailbox's
--                     send-mail page is open, the chart goes into the body (you press Send yourself)
-- Every 2 s the host checks whether you stand on an unfound X.
-- Our own maps (spec 43) and the Pixel Mail Studio (spec 48) are not built yet: the chart is 24 x 10 characters
-- of the current zone's frame, which is what a mail body can carry.
local ADDON, VAR = "@ADDON@", "@VAR@"
local _, ns = ...
local K = ns.kit
local G = RefluxCore and RefluxCore.modules and RefluxCore.modules["collection/andvari"]
if not G then print(ADDON .. ": RefluxCore's collection/andvari module is missing.") return end
K.saved(VAR, G.andvariInit)

local W, H = 24, 10

local function chart(m)
    local cell = G.andvariCell(m.x, m.y, W, H)
    local rows = { "+" .. string.rep("-", W) .. "+" }
    for r = 1, H do
        local line = {}
        for c = 1, W do line[c] = (c == cell[1] and r == cell[2]) and "X" or ((c + r) % 7 == 0 and "~" or " ") end
        rows[#rows + 1] = "|" .. table.concat(line) .. "|"
    end
    rows[#rows + 1] = "+" .. string.rep("-", W) .. "+"
    local heading, band = G.andvariHeading(m.x, m.y), G.andvariBand(m.x, m.y)
    local clue = heading == 0 and K.L("CLUE_MIDDLE") or K.f("CLUE", K.L("BAND_" .. band), K.L("DIR_" .. heading))
    rows[#rows + 1] = clue
    return rows
end

K.on("PLAYER_LOGIN", function()
    K.every(2, function()
        local map, x, y = K.pos("player")
        if map then
            local n = G.andvariCheck_(K.db, map, x, y, K.now())
            if n > 0 then K.notice(K.f("FOUND", K.L("NAME"), n)) end
        end
    end)
end)

SLASH_ANDVARISMAP1 = "/treasure"
SlashCmdList["ANDVARISMAP"] = function(msg)
    if not K.db then return end
    local cmd, n = (msg or ""):match("^%s*(%S*)%s*(%d*)")
    if cmd == "mark" then
        local map, x, y = K.pos("player")
        if not map then print(K.f("NOPOS", K.L("NAME"))) return end
        print(K.f("MARKED", K.L("NAME"), G.andvariMark_(K.db, map, x, y, K.now())))
    elseif cmd == "map" then
        local m = K.db.marks[tonumber(n) or #K.db.marks]
        if not m then print(K.f("NOMARK", K.L("NAME"))) return end
        local rows = chart(m)
        for _, r in ipairs(rows) do print(r) end
        if SendMailFrame and SendMailFrame.IsShown and SendMailFrame:IsShown() and SendMailBodyEditBox then
            SendMailBodyEditBox:SetText(table.concat(rows, "\n"))
            if SendMailSubjectEditBox then SendMailSubjectEditBox:SetText(K.L("SUBJECT")) end
            print(K.f("INMAIL", K.L("NAME")))
        end
    else
        print(K.L("HELP"))
    end
end
