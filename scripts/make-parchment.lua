-- scripts/make-parchment.lua -- writes the Old Map's parchment: a 256 x 256, 24-bit, uncompressed TGA (the client's
-- own texture format, power-of-two sides), deterministic from a fixed seed so the file never changes by itself.
--   lua5.1 scripts/make-parchment.lua runtime/wow/assets/parchment.tga
local W, H = 256, 256
local out = assert(io.open(arg[1] or "runtime/wow/assets/parchment.tga", "wb"))
out:write(string.char(0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, W % 256, math.floor(W / 256), H % 256, math.floor(H / 256), 24, 0x20))
local seed = 12345
local function rnd() seed = (seed * 1103515245 + 12345) % 2147483648; return seed / 2147483648 end
local function grid(n) local g = {} for y = 0, n do g[y] = {} for x = 0, n do g[y][x] = rnd() end end return g end
local coarse, fine = grid(8), grid(32)
local function sample(g, n, x, y)                 -- value noise: a random lattice, bilinear between its points
    local fx, fy = x / W * n, y / H * n
    local x0, y0 = math.floor(fx), math.floor(fy)
    local tx, ty = fx - x0, fy - y0
    local a, b, c, d = g[y0][x0], g[y0][x0 + 1], g[y0 + 1][x0], g[y0 + 1][x0 + 1]
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty
end
local rows = {}
for y = 0, H - 1 do
    local row = {}
    for x = 0, W - 1 do
        local n = sample(coarse, 8, x, y) * 0.6 + sample(fine, 32, x, y) * 0.3 + rnd() * 0.1
        local dx, dy = (x - W / 2) / (W / 2), (y - H / 2) / (H / 2)
        local v = 1 - 0.22 * (dx * dx + dy * dy)  -- darker toward the edges, like a sheet kept rolled
        local base = (0.74 + 0.22 * n) * v
        local r, g, b = base * 0.97, base * 0.86, base * 0.64
        row[#row + 1] = string.char(math.floor(b * 255 + 0.5), math.floor(g * 255 + 0.5), math.floor(r * 255 + 0.5))
    end
    rows[#rows + 1] = table.concat(row)
end
out:write(table.concat(rows))
out:close()
