-- Wall test: disable collision (group 22) for a window; user pushes into a wall.
-- We watch X/Z for forward motion through the wall.
local out = {}
local function fmt(s, ...) out[#out+1] = string.format(s, ...) end
local function rq(p)
  if not p or p < 0x10000 or p > 0x7FFFFFFFFFFF then return nil end
  local ok, v = pcall(readQword, p)
  if ok and v ~= nil then return v end
  return nil
end
local function rd(p)
  local ok, v = pcall(readInteger, p, 4)
  if ok then return v end
  return nil
end
local function rf(p)
  local ok, v = pcall(readFloat, p)
  if ok then return v end
  return nil
end
local function rb(p, n)
  local ok, v = pcall(readBytes, p, n, true)
  if ok and v then return v end
  return nil
end
local function writeU32(p, v)
  if not p or not v then return false end
  local b = { v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF }
  return pcall(writeBytes, p, b)
end

local MOD = getAddressSafe("Trinity.asi")
if not MOD then return "no module" end
local MOD_SIZE = 2224128

local function findOwner()
  local targets = {}
  local addr = MOD
  while addr < MOD + MOD_SIZE do
    local b = rb(addr, 0x1000)
    if b then
      for i = 1, #b - 7 do
        if b[i] >= 0x48 and b[i] <= 0x4F and b[i+1] == 0x8B then
          local modrm = b[i+2]
          if modrm and math.floor(modrm/64) == 0 and (modrm % 8) == 5 then
            local d = b[i+3] + b[i+4]*256 + b[i+5]*65536 + b[i+6]*16777216
            if d >= 0x80000000 then d = d - 0x100000000 end
            local ia = addr + i - 1
            local tgt = ia + 7 + d
            if tgt >= MOD + 0xFA000 and tgt < MOD + MOD_SIZE then targets[tgt] = true end
          end
        end
      end
    end
    addr = addr + 0x1000
  end
  for tgt in pairs(targets) do
    local v = rq(tgt)
    if v and v > 0x10000 and v < 0x7FFFFFFFFFFF then
      if not (v >= 0x140000000 and v <= 0x157FCE020) then
        local vt = rq(v)
        if vt and vt >= 0x140000000 and vt <= 0x157FCE020 then
          local bidb = rb(v + 0x38, 8)
          local top = bidb and bidb[8] or 0
          if (top & 0x80) ~= 0 then
            local x, y, z = rf(v+0x90), rf(v+0x94), rf(v+0x98)
            if x and y and z and x == x and y == y and z == z then
              if math.abs(x) > 5 and math.abs(y) > 5 and math.abs(z) > 5 then return v end
            end
          end
        end
      end
    end
  end
  return nil
end

fmt("get ready: stand at a wall, facing it. 3s pause...")
sleep(3000)

local owner = findOwner()
if not owner then return "owner not found" end
fmt("owner=%X", owner)

local x0, y0, z0 = rf(owner+0x90), rf(owner+0x94), rf(owner+0x98)
fmt("start pos=(%.2f, %.2f, %.2f) filter=0x%08X", x0, y0, z0, rd(owner+0x40) or 0)

-- disable collision for the window
writeU32(owner + 0x40, 0x00000016)

local samples = {}
local xmin, xmax, zmin, zmax = x0, x0, z0, z0
for i = 1, 30 do
  sleep(200)
  local x, y, z = rf(owner+0x90), rf(owner+0x94), rf(owner+0x98)
  x = x or x0; z = z or z0
  if x < xmin then xmin = x end
  if x > xmax then xmax = x end
  if z < zmin then zmin = z end
  if z > zmax then zmax = z end
  if (i % 5) == 0 then
    samples[#samples+1] = string.format("(%.2f,%.2f,%.2f)", x, y or 0, z)
  end
end

-- restore
writeU32(owner + 0x40, 0x401B)
sleep(300)

local x1, y1, z1 = rf(owner+0x90), rf(owner+0x94), rf(owner+0x98)
fmt("end pos=(%.2f, %.2f, %.2f)", x1, y1, z1)
fmt("horizontal travel: dX=%.2f  dZ=%.2f  (total horiz %.2f)",
    (x1 or 0) - (x0 or 0), (z1 or 0) - (z0 or 0),
    math.sqrt(((x1 or 0)-(x0 or 0))^2 + ((z1 or 0)-(z0 or 0))^2))
fmt("X range %.2f..%.2f  Z range %.2f..%.2f", xmin, xmax, zmin, zmax)
fmt("samples: %s", table.concat(samples, "  "))
fmt("final filter=0x%08X", rd(owner+0x40) or 0)

return table.concat(out, "\n")
