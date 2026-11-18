-- runtime/wow/locales/reflux.lua -- REFLUX Core's catalogue: the lines the tips API itself prints, and RefluxCore's
-- own tips (TIP_n). Data, not code; enUS fallback (see hrungnir.lua).
local _, ns = ...
local enUS = {
    NAME = "REFLUX Core",
    TIP_LINE = "  Tip (%s): %s",
    ALONE = "%s: no other Collection addon here has a tip to tell.",
    TIP_1 = "Every Collection addon depends on REFLUX Core: without the RefluxCore folder in Interface/AddOns, the client will not load any of them.",
    TIP_2 = "Update the Collection as a set: copy every folder from the new AddOns/ over the old ones, REFLUX Core included, then restart the client or /reload.",
    TIP_3 = "Collection addons never call each other: each writes its events to one shared REFLUX log, and Collection Journal copies that log into your saved journal.",
    TIP_4 = "/rumor tells you a true tip about another of your Collection addons, and it works through every tip before it repeats one.",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
