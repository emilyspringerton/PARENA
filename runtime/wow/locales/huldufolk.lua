-- runtime/wow/locales/huldufolk.lua -- Huldufólk Hunt's catalogue (spec addon 54). Data; enUS fallback (see hrungnir.lua).
local _, ns = ...
local enUS = {
    NAME = "Huldufólk Hunt",
    HELP = "/hunt start [seconds] -- you seek, the group hides (60 s head start)   /hunt stop -- end the round",
    HIDE = "%s: %s is the seeker! Everyone else: you have %d seconds to hide. Go!",
    SEEK = "%s: ready or not, here I come! %d seconds on the clock.",
    FOUND = "%s: found %s! %d still hidden.",
    HINT = "%s: %s",
    HOT_1 = "BURNING -- they are right here!", HOT_2 = "Hot!", HOT_3 = "Warm.", HOT_4 = "Cold.", HOT_5 = "Freezing.",
    OVER = "%s: round over -- %d of %d found.",
    ROW = "  %d. %s, found after %d s",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
