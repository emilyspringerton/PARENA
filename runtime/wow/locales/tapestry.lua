-- runtime/wow/locales/tapestry.lua -- Hall Tapestry's catalogue (spec addon 69). Data; enUS fallback.
local _, ns = ...
local enUS = {
    NAME = "Hall Tapestry",
    START = "%s: %d shots planned. Everyone on camera!",
    SHOT = "Shot %d of %d: %s -- pose: %s",
    ARR_1 = "front row kneels, back row stands", ARR_2 = "tallest at the back", ARR_3 = "everyone in a line, shoulder to shoulder",
    ARR_4 = "make a circle", ARR_5 = "shortest in the middle", ARR_6 = "everyone face the camera, mounts welcome",
    CLICK = "*click* Got it!",
    NOSHOT = "Hold it... (this client would not take the screenshot -- press your screenshot key!)",
    DONE = "%s: that's a wrap -- %d shots in the tapestry.",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.catalogue = { enUS = enUS, enGB = enGB }
