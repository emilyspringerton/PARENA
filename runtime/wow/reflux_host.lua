-- runtime/wow/reflux_host.lua -- REFLUX's host runtime for World of Warcraft (Lua 5.1).
--
-- The WoW counterpart of SHANKPIT/packages/reflux/reflux_runtime.c: the one shared, per-session action
-- log that stdlib/reflux/reflux.prn and stdlib/reflux/cursor.prn reach through their `:lua` #target keys.
-- Every PARENA-built addon in the Collection talks to every other one through this log and nothing else
-- (a dispatcher never knows who listens). Same ABI as the C hosts, row for row:
--
--   ring of REFLUX_LOG_CAPACITY rows (action_type, a, b, c), oldest overwritten first
--   log_size()          retained rows, SATURATES at capacity -- never a cursor
--   total_dispatched()  every dispatch ever
--   oldest_abs()        absolute number of the oldest retained row
--   index_for_abs(abs)  absolute row -> retained index, -1 if overwritten or not dispatched yet
--   action_*_at(i)      i = 0 oldest .. log_size()-1 newest; type -1 / payload 0 when out of range
--
-- Every addon in one WoW session shares one global environment, so the log is the global `RefluxHost`.
-- RefluxCore (the addon) loads this file first; any copy loaded later keeps the existing log rather than
-- splitting it in two. Payloads are integers by contract (VS0's scalar ABI); they are truncated toward
-- zero so a fractional value from hand-written Lua cannot leak into a row.

if RefluxHost then return end

local CAPACITY = 256 -- REFLUX_LOG_CAPACITY in every C host; keep them equal

local types, as, bs, cs = {}, {}, {}, {}
local total = 0

local function int(v)
    v = tonumber(v) or 0
    if v < 0 then return math.ceil(v) end
    return math.floor(v)
end

local function length()
    if total < CAPACITY then return total end
    return CAPACITY
end

-- retained index -> ring slot, or nil when out of range (reflux_log_at)
local function slot_at(index)
    index = int(index)
    if index < 0 or index >= length() then return nil end
    if total <= CAPACITY then return index end
    return (total % CAPACITY + index) % CAPACITY
end

local H = { CAPACITY = CAPACITY }

function H.reset()
    types, as, bs, cs = {}, {}, {}, {}
    total = 0
end

function H.dispatch(action_type, a, b, c)
    local s = total % CAPACITY
    types[s], as[s], bs[s], cs[s] = int(action_type), int(a), int(b), int(c)
    total = total + 1
end

function H.log_size() return length() end
function H.total_dispatched() return total end
function H.oldest_abs() return total - length() end

function H.index_for_abs(abs_index)
    abs_index = int(abs_index)
    local oldest = total - length()
    if abs_index < 0 or abs_index >= total then return -1 end
    if abs_index < oldest then return -1 end
    return abs_index - oldest
end

function H.action_type_at(index)
    local s = slot_at(index)
    if s == nil then return -1 end
    return types[s]
end

function H.action_a_at(index) local s = slot_at(index) if s == nil then return 0 end return as[s] end
function H.action_b_at(index) local s = slot_at(index) if s == nil then return 0 end return bs[s] end
function H.action_c_at(index) local s = slot_at(index) if s == nil then return 0 end return cs[s] end

RefluxHost = H
