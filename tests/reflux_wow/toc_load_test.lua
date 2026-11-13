-- tests/reflux_wow/toc_load_test.lua -- load a packaged RefluxCore the way the WoW client does: every file
-- the .toc lists, in order, each called with (addonName, addonTable). Then talk through the registered
-- modules. usage: lua5.1 toc_load_test.lua <path/to/RefluxCore>
local dir = assert(arg[1], "usage: toc_load_test.lua <RefluxCore dir>")
local toc = assert(io.open(dir .. "/RefluxCore.toc"), "no RefluxCore.toc")
local ns, files, interface = {}, {}, nil
for line in toc:lines() do
    line = line:gsub("\r$", "")
    local n = line:match("^## Interface: (%d+)$")
    if n then interface = n end
    if line ~= "" and not line:match("^##") then files[#files + 1] = line end
end
toc:close()

local passed, failed = 0, 0
local function check(c, what) if c then passed = passed + 1 else failed = failed + 1; print("FAIL: " .. what) end end

check(interface ~= nil, "the toc declares a numeric ## Interface")
check(#files == 6 and files[1] == "reflux_host.lua", "the host loads before the modules that call it")
check(files[4] == "tips.lua" and files[5] == "locale.lua" and files[6] == "reflux_tips.lua",
      "the tips rules and catalogue load before the tips glue")
for _, f in ipairs(files) do
    local chunk = assert(loadfile(dir .. "/" .. f))
    chunk("RefluxCore", ns)
end
local R = RefluxCore and RefluxCore.modules["reflux/reflux"]
local C = RefluxCore and RefluxCore.modules["reflux/cursor"]
check(R ~= nil and C ~= nil, "both modules registered in RefluxCore.modules")
if R and C then
    -- RefluxCore published its own tips while loading: one 3060 row (a = no spec number, b = its tip count)
    check(R.refluxLogSize() == 1 and R.refluxActionTypeAt(0) == 3060 and R.refluxActionAAt(0) == 0,
          "RefluxCore's own tips were published through REFLUX 3060")
    check(RefluxTips ~= nil and RefluxTips.count() == 1 and RefluxTips.at(1).addon == "RefluxCore"
          and #RefluxTips.at(1).keys == R.refluxActionBAt(0), "the tips registry holds RefluxCore's tips")
    R.refluxDispatch(42, 1, 2, 3)
    check(R.refluxLogSize() == 2 and R.refluxActionTypeAt(1) == 42, "a dispatch through the packaged addon lands")
    check(C.refluxTotalDispatched() == 2, "the cursor module sees the same log")
end
print(string.format("%d passed, %d failed", passed, failed))
if failed > 0 then os.exit(1) end
