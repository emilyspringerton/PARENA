-- runtime/wow/ledger_export.lua -- the gear ledger's host glue, outside the game under Lua 5.1. The model, the
-- sorting, the JSON and the CSV are PARENA (stdlib/collection/ledger.prn, compiled to ledger.lua); this file only
-- reads files and writes stdout, the two things a pure module cannot do.
--
-- Each SavedVariables file is loaded with the journal reader's sandboxed loader (no globals, an instruction
-- budget): the file is data the game wrote, never code to trust.
--
-- usage: lua5.1 ledger_export.lua <journal_reader.lua> <ledger.lua> <json|csv> <var> <savedAt> [<account> <file>]...
--   savedAt  the newest file's modification time (unix seconds): Lua 5.1 cannot stat, so the caller passes it
local reader_path, ledger_path, mode, var, saved_at = arg[1], arg[2], arg[3], arg[4], arg[5]
assert(reader_path and ledger_path and (mode == "json" or mode == "csv") and var and saved_at,
    "usage: ledger_export.lua <journal_reader.lua> <ledger.lua> <json|csv> <var> <savedAt> [<account> <file>]...")
local R = dofile(reader_path)
local L = dofile(ledger_path)

local acc = L.ledgerNew()
local files = 0
for i = 6, #arg, 2 do
    local account, path = arg[i], arg[i + 1]
    local f = path and io.open(path, "rb")
    if not f then
        L.ledgerProblem(acc, tostring(path) .. ": cannot open")
    else
        local text = f:read("*a"); f:close()
        local db, err = R.load_saved_variables(text, var, "=" .. path)
        if db then
            L.ledgerAdd(acc, account, path, db); files = files + 1
        else
            L.ledgerProblem(acc, path .. ": " .. tostring(err))
        end
    end
end
if files == 0 and #arg < 6 then
    L.ledgerProblem(acc, "no SavedVariables file found: log in once with Fafnir's Bed enabled, then /reload")
end

local v = L.ledgerView(acc, tonumber(saved_at) or 0)
io.write(mode == "csv" and L.ledgerCsv(v) or L.ledgerJson(v))
