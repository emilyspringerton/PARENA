-- runtime/wow/locales/board.lua -- the Total Character Board's catalogue (spec addon 1). Data; enUS fallback.
local _, ns = ...
local enUS = {
    NAME = "Character Board",
    HEAD = "%s: %d characters, %d snapshots recorded (export them with collection-csv.sh after logging out)",
    ROW = "  %s -- level %d (%.1f%%), rested %d%%, %s, %s, last seen %s",
    SNAPPED = "%s: snapshot taken (%d in the record).",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
