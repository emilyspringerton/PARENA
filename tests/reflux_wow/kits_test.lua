-- tests/reflux_wow/kits_test.lua -- the macro-kit rules (stdlib/collection/kits.prn) under real Lua 5.1, against the real
-- class sets (stdlib/collection/macros.prn). usage: lua5.1 kits_test.lua <kits.lua> <macros.lua>
assert(_VERSION == "Lua 5.1", "must run under Lua 5.1")
local K = assert(dofile(assert(arg[1])))
local S = assert(dofile(assert(arg[2])))

local passed, failed = 0, 0
local function eq(got, want, what)
    if got == want then passed = passed + 1
    else failed = failed + 1; print("FAIL: " .. what .. " (got " .. tostring(got) .. ", want " .. tostring(want) .. ")") end
end
local function names(kit) local t = {}; for _, m in ipairs(kit) do t[#t + 1] = m[1] .. ":" .. m[3] end; return table.concat(t, ",") end

local set = S.macroSet("PALADIN")
eq(#set, 16, "the paladin set has 16 macros")

-- an empty plan is the class set, every macro origin 0
local plan = {}
local kit = K.kitApply(set, plan)
eq(#kit, 16, "an empty plan gives the whole class set")
eq(kit[1][1] .. kit[1][3], "PalJudge0", "in the set's order, from the class")
eq(K.kitChanges(plan), 0, "and makes no changes")
eq(type(plan.drop) == "table" and type(plan.edit) == "table" and type(plan.add) == "table", true, "kitPlan fills the three parts in")

-- checks
eq(K.kitCheck("", "x"), 1, "an empty name is refused")
eq(K.kitCheck("ThisNameIsTooLong", "x"), 2, "a name over 16 characters is refused")
eq(K.kitCheck("PalOk", ""), 3, "an empty body is refused")
eq(K.kitCheck("PalOk", string.rep("x", 256)), 4, "a body over 255 is flagged")
eq(K.kitCheck("PalOk", "/cast Seal"), 0, "a good macro passes")

-- drop, edit, add
eq(K.kitDrop_(set, plan, "PalExo"), 0, "dropping a class macro")
eq(K.kitEdit_(set, plan, "PalHeal", "#showtooltip\n/cast {Holy Light}"), 0, "editing a class macro")
eq(K.kitAdd_(set, plan, "PalSpeed", "/cast [mod:alt] {Divine Steed}"), 0, "adding a macro of my own")
eq(K.kitAdd_(set, plan, "PalJudge", "/cast X"), 5, "adding a name the class set has is refused (edit it instead)")
eq(K.kitAdd_(set, plan, "PalSpeed", "/cast X"), 5, "adding the same name twice is refused")
eq(K.kitAdd_(set, plan, "PalExo", "/cast X"), 5, "a dropped class name is still the class's: refused")
eq(K.kitAdd_(set, plan, "", "/cast X"), 1, "a bad name is refused with its code")
eq(K.kitAdd_(set, plan, "PalLong", string.rep("x", 300)), 0, "a long body is allowed (the client limit is unconfirmed)")
eq(K.kitDrop_(set, plan, "PalLong"), 0, "dropping an added macro removes it")
eq(K.kitDrop_(set, plan, "Nope"), 6, "dropping a macro the kit does not have")
eq(K.kitEdit_(set, plan, "Nope", "/cast X"), 6, "editing one")
eq(K.kitChanges(plan), 3, "one drop, one edit, one add")

kit = K.kitApply(set, plan)
eq(#kit, 16, "16 - 1 dropped + 1 added")
eq(names(kit):find("PalExo", 1, true), nil, "the dropped macro is gone")
local heal, speed
for _, m in ipairs(kit) do if m[1] == "PalHeal" then heal = m end; if m[1] == "PalSpeed" then speed = m end end
eq(heal[3], 1, "the edited macro is marked edited")
eq(heal[2], "#showtooltip\n/cast {Holy Light}", "with the new body")
eq(speed[3], 2, "the added macro is marked added")
eq(kit[#kit][1], "PalSpeed", "added macros come after the class set")

-- editing a dropped class macro brings it back, edited
eq(K.kitEdit_(set, plan, "PalExo", "/cast Exorcism"), 0, "editing a dropped macro")
eq(plan.drop.PalExo, nil, "takes the drop back")
eq(K.kitEdit_(set, plan, "PalSpeed", "/cast {Summon Charger}"), 0, "editing an added macro")
eq(plan.add[1][2], "/cast {Summon Charger}", "changes it in the add list")

-- restore
eq(K.kitRestore_(set, plan, "PalExo"), 0, "restoring an edited class macro")
eq(plan.edit.PalExo, nil, "forgets the edit")
eq(K.kitRestore_(set, plan, "PalExo"), 7, "restoring it again: nothing to restore")
eq(K.kitRestore_(set, plan, "PalSpeed"), 6, "an added macro is not restored (drop it)")

-- a class with no set: a kit made only of added macros
local none = S.macroSet("DEATHKNIGHT")
eq(#none, 0, "no set for this class")
local p2 = {}
eq(K.kitAdd_(none, p2, "DkGrip", "/cast Death Grip"), 0, "adding to an empty class")
eq(names(K.kitApply(none, p2)), "DkGrip:2", "the kit is just what was added")

print(string.format("kits: %d passed, %d failed", passed, failed))
os.exit(failed == 0 and 0 or 1)
