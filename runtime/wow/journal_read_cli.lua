-- runtime/wow/journal_read_cli.lua -- command line for journal_reader.lua (COV-6b). Normally run by
-- scripts/collection-journal-read.sh, which finds the files and passes:
--   lua5.1 journal_read_cli.lua <journal_reader.lua> <ids.lua|-> <var> <events.ndjson> <sv.lua> <account> [<sv.lua> <account>]...
-- Exit 0: all read. Exit 1: a file could not be read (it was skipped, the others were still read).
-- Exit 3: all read, but at least one GAP (rows trimmed in game before any read reached them) -- said, not hidden.
assert(_VERSION == "Lua 5.1", "run under Lua 5.1, got " .. tostring(_VERSION))
local R = dofile(assert(arg[1], "journal_reader.lua path"))
local ids_path, var, events = assert(arg[2]), assert(arg[3]), assert(arg[4])

local classify
if ids_path ~= "-" then
    local I = dofile(ids_path)
    classify = I.collectionAddonOf
end

local status = 0
local i = 5
if not arg[i] then io.stderr:write("no SavedVariables files given\n"); os.exit(1) end
while arg[i] do
    local sv, account = arg[i], arg[i + 1]
    local report, err = R.read_file({ sv_path = sv, var = var, events_path = events, account = account, classify = classify })
    if not report then
        io.stderr:write("SKIPPED ", err, "\n")
        status = 1
    else
        print(R.format_report(sv, report))
        if #report.gaps > 0 and status == 0 then status = 3 end
    end
    i = i + 2
end
os.exit(status)
