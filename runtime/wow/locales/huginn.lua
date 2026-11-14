-- runtime/wow/locales/huginn.lua -- Huginn's catalogue. Data, not code; enUS fallback.
local _, ns = ...
local enUS = {
    HELP = "/huginn -- show or hide the arrows   /huginn log -- show or hide the mini quest log   "
        .. "/huginn <n> -- quest n's arrow on or off   /huginn list -- your quests in chat   "
        .. "/huginn way <x> <y> [note] -- add a waypoint arrow on this map   /huginn clear -- every arrow off",
    TITLE = "Huginn", COUNT = "%d/%d arrows", HIDE_LOG = "click: hide quests", SHOW_LOG = "click: show quests",
    LIST_HEAD = "%s: your quest log ([x] = arrow on, * = tracked, + = watched):",
    LIST_ROW = "  %d. %s%s -- %s",
    HAS = "has a location", NONE = "no location from the client",
    LIST_EMPTY = "%s: your quest log is empty (or this client does not let addons read it).",
    LIST_HINT = "Turn an arrow on with /huginn <n> or by clicking the quest. A quest with no location can still be reached with /huginn way <x> <y>.",
    ON = "%s: arrow on for %s.", OFF = "%s: arrow off for %s.",
    FULL = "%s: %d arrows is the most at once; turn one off first.",
    CLEARED = "%s: every arrow off.",
    WAY = "%s: waypoint arrow at %.1f, %.1f%s.", WAY_BAD = "%s: /huginn way <x> <y> -- two numbers from 0 to 100, like /huginn way 45.2 60.1",
    NO_POS = "%s: the client is not saying where you are right now (inside an instance?).",
    NO_TARGET = "No arrows on", NO_TARGET_HINT = "click a quest below, or /huginn way <x> <y>",
    THERE = "%s: you are there -- %s.",
    YARDS = "%d yd", UNITS = "%d map units", OTHER_MAP = "on another map", OTHER_CONTINENT = "on another continent",
    WAYPOINT = "Waypoint",
    SIDE_0 = "ahead", SIDE_1 = "ahead, left", SIDE_2 = "left", SIDE_3 = "behind, left",
    SIDE_4 = "behind", SIDE_5 = "behind, right", SIDE_6 = "right", SIDE_7 = "ahead, right",
    COMPASS_0 = "north", COMPASS_1 = "north-west", COMPASS_2 = "west", COMPASS_3 = "south-west",
    COMPASS_4 = "south", COMPASS_5 = "south-east", COMPASS_6 = "east", COMPASS_7 = "north-east",
    ARRIVED = "here",
    MISSING = "%s: RefluxCore's collection/huginn module is missing.",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
