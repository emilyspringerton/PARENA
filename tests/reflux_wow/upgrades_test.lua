-- tests/reflux_wow/upgrades_test.lua -- the gear optimizer's rules (stdlib/collection/upgrades.prn) under real Lua 5.1.
-- usage: lua5.1 tests/reflux_wow/upgrades_test.lua <upgrades.lua>
local U = dofile(arg[1])
local passed, failed = 0, 0
local function eq(got, want, what)
    if got == want then passed = passed + 1
    else failed = failed + 1; print(string.format("FAIL: %s (got %s, want %s)", what, tostring(got), tostring(want))) end
end

-- roles
eq(U.upgDefaultRole("PALADIN"), "strength", "a paladin levels on strength")
eq(U.upgDefaultRole("ROGUE"), "agility", "a rogue on agility")
eq(U.upgDefaultRole("HUNTER"), "hunter", "a hunter is its own role")
eq(U.upgDefaultRole("MAGE"), "caster", "a mage is a caster")
eq(U.upgDefaultRole("PRIEST"), "caster", "a priest levels as a caster")
eq(U.upgRoleOk("healer"), 1, "healer is a role")
eq(U.upgRoleOk("dancer"), 0, "dancer is not")

-- who can use what
local plate = { req = 40, loc = "INVTYPE_CHEST", cls = 4, sub = 4, s = { str = 10, sta = 10, armor = 500 } }
eq(U.upgCanUse("PALADIN", 40, plate), 0, "a level 40 paladin wears plate")
eq(U.upgCanUse("PALADIN", 38, plate), 4, "a level 38 paladin is too low for plate (not the wrong class)")
eq(U.upgCanUse("MAGE", 60, plate), 2, "a mage never wears plate")
eq(U.upgCanUse("ROGUE", 60, { loc = "INVTYPE_CHEST", cls = 4, sub = 2 }), 0, "a rogue wears leather")
eq(U.upgCanUse("PRIEST", 60, { loc = "INVTYPE_CHEST", cls = 4, sub = 2 }), 2, "a priest does not")
eq(U.upgCanUse("HUNTER", 39, { loc = "INVTYPE_LEGS", cls = 4, sub = 3 }), 4, "a hunter gets mail at 40")
eq(U.upgCanUse("HUNTER", 40, { loc = "INVTYPE_LEGS", cls = 4, sub = 3 }), 0, "...and wears it then")
eq(U.upgCanUse("WARRIOR", 10, { loc = "INVTYPE_LEGS", cls = 4, sub = 3 }), 0, "a warrior wears mail from the start")
local staff = { req = 10, loc = "INVTYPE_2HWEAPON", cls = 2, sub = 10, s = { int = 10, sp = 5 } }
eq(U.upgCanUse("ROGUE", 60, staff), 3, "a rogue does not use a staff")
eq(U.upgUseWhy(3), "a weapon it does not use", "and says why")
eq(U.upgCanUse("DRUID", 9, staff), 4, "a level 9 druid is under the staff's required level")
eq(U.upgCanUse("MAGE", 60, { loc = "INVTYPE_SHIELD", cls = 4, sub = 6 }), 2, "a mage carries no shield")
eq(U.upgCanUse("SHAMAN", 60, { loc = "INVTYPE_SHIELD", cls = 4, sub = 6 }), 0, "a shaman does")
eq(U.upgCanUse("MAGE", 60, { loc = "", cls = 7, sub = 0 }), 1, "cloth (the trade good) is not gear")
eq(#U.upgSlots("INVTYPE_WEAPON", "ROGUE"), 2, "a rogue holds a one-hander in either hand")
eq(#U.upgSlots("INVTYPE_WEAPON", "PALADIN"), 1, "a paladin in the main hand only")
eq(#U.upgSlots("INVTYPE_WEAPONOFFHAND", "PRIEST"), 0, "an off-hand weapon is no use to a priest")

-- scores and gains
local w = U.upgWeights("caster")
local items = {
    [1] = { loc = "INVTYPE_WEAPON", cls = 2, sub = 7, s = { str = 2, dps = 10 } },
    [2] = { loc = "INVTYPE_SHIELD", cls = 4, sub = 6, s = { sta = 5 } },
    [3] = { loc = "INVTYPE_FINGER", cls = 4, sub = 0, s = { int = 5 } },
    [4] = { loc = "INVTYPE_FINGER", cls = 4, sub = 0, s = { int = 1 } },
    [5] = { loc = "INVTYPE_2HWEAPON", cls = 2, sub = 10, s = { int = 4 } },
    [6] = { loc = "INVTYPE_RANGEDRIGHT", cls = 2, sub = 19, s = { dps = 10 } },
}
local ring = { loc = "INVTYPE_FINGER", cls = 4, sub = 0, s = { int = 3 } }
local b = U.upgBest("MAGE", w, { [16] = 1, [11] = 3, [12] = 4 }, items, ring)
eq(b[1], 12, "a ring replaces the weaker of the two worn")
eq(b[2], 20, "two intellect over it, at 1.0 a point")
eq(b[3], 4, "and names the ring it replaces")
local two = { loc = "INVTYPE_2HWEAPON", cls = 2, sub = 10, s = { int = 10, sp = 10 } }
eq(U.upgScore(w, two, 16), 220, "a caster scores intellect and spell power")
b = U.upgBest("MAGE", w, { [16] = 1, [17] = 2 }, items, two)
eq(b[2], 195, "a two-hander is weighed against the main hand AND the off hand")
local orb = { loc = "INVTYPE_HOLDABLE", cls = 4, sub = 0, s = { int = 9 } }
eq(U.upgBest("MAGE", w, { [16] = 5 }, items, orb), nil, "no off-hand while a two-hander is held")
eq(U.upgScore(w, items[6], 18), 600, "a wand's dps counts in the ranged slot")
eq(U.upgScore(U.upgWeights("strength"), items[1], 16), 1440, "a melee weapon's dps counts for strength")
b = U.upgBest("MAGE", w, { [11] = 99 }, items, ring)
eq(b[4], 1, "an empty ring slot is known")
b = U.upgBest("MAGE", w, { [11] = 99, [12] = 99 }, items, ring)
eq(b[4], 0, "a worn ring with no facts makes the gain a guess")

print(string.format("upgrades: %d passed, %d failed", passed, failed))
os.exit(failed == 0 and 0 or 1)
