-- runtime/wow/locales/fun_kit.lua -- the party-game kit's catalogue (fun_kit.lua). Data, not code.
--
-- EMOTE_<TOKEN> is a Lua pattern, matched against the lower-cased text of CHAT_MSG_TEXT_EMOTE, that tells which
-- emote a line shows ("Brokkr waves at you.", "You burst into dance."). Stems, so the first person ("You wave")
-- and the third ("waves") both match. Like Ratatoskr Roll's CRITTER_TYPE, these change behaviour, so each
-- locale's patterns must be copied from that locale's client, not translated; until then only English
-- clients' emotes are recognised. Not yet checked against Forever's own emote texts.
local _, ns = ...
local enUS = {
    EMOTE_WAVE = "wave", EMOTE_DANCE = "danc", EMOTE_CHEER = "cheer", EMOTE_BOW = "%f[%a]bow",
    EMOTE_CLAP = "clap", EMOTE_SALUTE = "salute", EMOTE_FLEX = "flex", EMOTE_LAUGH = "laugh",
    EMOTE_KNEEL = "kneel", EMOTE_ROAR = "roar",
    SOLO = "%s: this game needs a group -- invite some friends first.",
    NOPOS = "%s: the client will not say where you are here (an instance, or no map).",
    CRITTER_TYPE = "Critter",
}
local enGB = {}
for k, v in pairs(enUS) do enGB[k] = v end
ns.kit_catalogue = { enUS = enUS, enGB = enGB }
