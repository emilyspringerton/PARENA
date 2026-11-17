-- tests/iduna/discover_test.lua -- the Lua target of stdlib/iduna/discover.prn against the shared parity fixture.
--   lua5.1 tests/iduna/discover_test.lua <generated discover.lua> <stdlib/iduna/discover_parity.txt>
-- Prints "iduna discover (Lua): N passed, M failed" and exits non-zero on any failure. Loads in Lua 5.1 (WoW's dialect).
local mod_path, fixture = arg[1], arg[2]
if not mod_path or not fixture then
  io.stderr:write("usage: lua5.1 discover_test.lua <discover.lua> <discover_parity.txt>\n")
  os.exit(2)
end
local M = dofile(mod_path)

-- the Lua target camelCases names: probe-verdict -> probeVerdict
local function camel(name)
  return (name:gsub("%-(%w)", function(c) return c:upper() end))
end

local pass, fail, rows = 0, 0, 0
for line in io.lines(fixture) do
  line = line:gsub("\r$", "")
  if line ~= "" and line:sub(1, 1) ~= "#" then
    rows = rows + 1
    local words = {}
    for w in line:gmatch("%S+") do words[#words + 1] = w end
    local fn, args, want, seen_eq = words[1], {}, nil, false
    for i = 2, #words do
      if words[i] == "=" then seen_eq = true
      elseif seen_eq then want = tonumber(words[i])
      else args[#args + 1] = tonumber(words[i]) end
    end
    local f = M[camel(fn)]
    local got = f and f(unpack(args))
    if f and want ~= nil and got == want then
      pass = pass + 1
    else
      fail = fail + 1
      print(("FAIL parity: %s (got %s)"):format(line, tostring(got)))
    end
  end
end
if rows >= 40 then pass = pass + 1 else fail = fail + 1; print("FAIL parity: the fixture has its rows") end
print(("iduna discover (Lua): %d passed, %d failed"):format(pass, fail))
os.exit(fail == 0 and 0 or 1)
