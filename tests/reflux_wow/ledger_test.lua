-- tests/reflux_wow/ledger_test.lua -- the gear ledger (stdlib/collection/ledger.prn) under real Lua 5.1, through its
-- host (runtime/wow/ledger_export.lua) exactly as tools/hoard runs it. Made-up data only (D-8).
-- usage: lua5.1 ledger_test.lua <ledger_export.lua> <journal_reader.lua> <ledger.lua> <workdir> [<lua5.1 to run the host>]
assert(_VERSION == "Lua 5.1", "must run under Lua 5.1, got " .. tostring(_VERSION))
local export, reader, ledger, work = assert(arg[1]), assert(arg[2]), assert(arg[3]), assert(arg[4])
local lua = arg[5] or "lua5.1"

local passed, failed = 0, 0
local function eq(got, want, what)
    if got == want then passed = passed + 1
    else failed = failed + 1; print("FAIL: " .. what .. "\n  got:  " .. tostring(got) .. "\n  want: " .. tostring(want)) end
end
local function has(s, sub, what) eq(type(s) == "string" and s:find(sub, 1, true) ~= nil, true, what) end

local SV = [[
FafnirsBedDB = {
["v"] = 1,
["names"] = { [2589] = "Linen Cloth", [5009] = "Mindbender Loop", [2207] = "Jambiya", [4373] = "Shadowskin Gloves",
              [159] = "Refreshing \"Spring\" Water", [777] = "Salt, Fine", },
["post"] = {
["Bjorn-Test Realm"] = { [1] = { ["from"] = "Astrid-Test Realm", ["at"] = 1794738000, ["copper"] = 250, ["items"] = { [2589] = 5, }, }, },
["Stranger-Test Realm"] = { [2] = { ["from"] = "Astrid-Test Realm", ["at"] = 1794738100, ["copper"] = 0, ["items"] = { [2589] = 1, }, }, },
["Gone-Test Realm"] = { },
},
["chars"] = {
["Astrid-Test Realm"] = {
["seen"] = 1794738458,
["slots"] = { [16] = 2207, [10] = 4373, [11] = 5009, [12] = 5009, },
["loc"] = { ["equipped"] = { [2207] = 1, [4373] = 1, [5009] = 2, }, ["bags"] = { [2589] = 16, [777] = 2, }, ["bank"] = { [2589] = 4, }, },
["gold"] = 100,
},
["Bjorn-Test Realm"] = {
["seen"] = 1794700000,
["loc"] = { ["bags"] = { [5009] = 1, [159] = 3, [2589] = 0, }, },
["gold"] = 289,
},
},
}
]]

local function write(path, text) local f = assert(io.open(path, "wb")); f:write(text); f:close() end
local function run(mode, ...)
    local cmd = { lua, export, reader, ledger, mode, "FafnirsBedDB", "1794740000" }
    for _, a in ipairs({ ... }) do cmd[#cmd + 1] = a end
    for i = 1, #cmd do cmd[i] = '"' .. cmd[i] .. '"' end
    local line = table.concat(cmd, " ")
    if package.config:sub(1, 1) == "\\" then line = '"' .. line .. '"' end   -- cmd /c strips one pair
    local p = assert(io.popen(line, "r"))
    local out = p:read("*a"); p:close()
    return (out:gsub("\r\n", "\n"))
end

local sv = work .. "/FafnirsBed.lua"
write(sv, SV)

-- CSV: worn slots in slot order, then bags/bank/mail, then post to your own characters; mail to Stranger left the hoard
eq(run("csv", "1#1", sv), table.concat({
    "character,realm,account,location,slot,item_id,name,count",
    "Astrid,Test Realm,1#1,equipped,Hands,4373,Shadowskin Gloves,1",
    "Astrid,Test Realm,1#1,equipped,Finger 1,5009,Mindbender Loop,1",
    "Astrid,Test Realm,1#1,equipped,Finger 2,5009,Mindbender Loop,1",
    "Astrid,Test Realm,1#1,equipped,Main hand,2207,Jambiya,1",
    "Astrid,Test Realm,1#1,bags,,2589,Linen Cloth,16",
    'Astrid,Test Realm,1#1,bags,,777,"Salt, Fine",2',
    "Astrid,Test Realm,1#1,bank,,2589,Linen Cloth,4",
    "Bjorn,Test Realm,1#1,bags,,5009,Mindbender Loop,1",
    'Bjorn,Test Realm,1#1,bags,,159,"Refreshing ""Spring"" Water",3',
    "Bjorn,Test Realm,1#1,post,,2589,Linen Cloth,5",
}, "\n") .. "\n", "the CSV, row for row (commas and quotes escaped)")

local json = run("json", "1#1", sv)
has(json, '"savedAt":1794740000', "savedAt is what the caller passed")
has(json, '{"slot":10,"slotName":"Hands","id":4373,"name":"Shadowskin Gloves"}', "gear in slot order, named")
has(json, '"name":"Refreshing \\"Spring\\" Water"', "quotes escaped in JSON")
has(json, '"bank":[],"mail":[],"equipped":[],"gear":[],"bankRead":false,"mailRead":false,"gearRead":false',
    "Bjorn's unread locations are [] and *Read false -- never null, which broke the page")
has(json, '"items":26,"distinct":5', "Astrid: 4 worn + 18 in bags + 4 in the bank, 5 kinds")
has(json, '"copper":639', "account gold: 100 + 289 + 250 in the post to Bjorn (Stranger's parcel left the account)")
has(json, '{"id":2589,"name":"Linen Cloth","total":25,"holders":[{"character":"Astrid","where":"bags","count":16},' ..
          '{"character":"Bjorn","where":"post","count":5},{"character":"Astrid","where":"bank","count":4}]}',
    "Linen Cloth: total and who holds it, most first")
has(json, '{"id":5009,"name":"Mindbender Loop","total":3,"holders":[{"character":"Astrid","where":"equipped","count":2}',
    "a worn ring counts as held")
has(json, '"toName":"Stranger","from":"Astrid-Test Realm","fromName":"Astrid","at":1794738100,"copper":0,' ..
          '"items":[{"id":2589,"name":"Linen Cloth","count":1}],"onAccount":false', "mail to a stranger is listed, off-account")
eq(json:find('"Gone', 1, true), nil, "an empty post box is not a parcel")

-- a file that is not Fafnir's Bed's, and no files at all, are problems the page shows, not crashes
local bad = work .. "/Bad.lua"
write(bad, "SomethingElse = { }\n")
has(run("json", "2#1", bad), '"problems":["' .. bad, "a file without the table is a named problem")
has(run("json"), "no SavedVariables file found", "no files at all says what to do")

print(string.format("%d passed, %d failed", passed, failed))
os.exit(failed == 0 and 0 or 1)
