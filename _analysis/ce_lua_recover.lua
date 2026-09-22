-- Emergency recovery: restore collision filter and lift the character above
-- ground so they land back on the surface.
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
local function writeF32(p, f)
  -- write a float bit pattern via string.pack (Lua 5.3)
  local ok, s = pcall(string.pack, "<f", f)
  if ok then
    local b = { string.byte(s, 1), string.byte(s, 2), string.byte(s, 3), string.byte(s, 4) }
    return pcall(writeBytes, p, b)
  end
  return false
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
            return v  -- accept first valid heap owner with a body id
          end
        end
      end
    end
  end
  return nil
end

local owner = findOwner()
if not owner then return "owner not found" end

local x, y, z = rf(owner+0x90), rf(owner+0x94), rf(owner+0x98)
fmt("before: pos=(%.2f, %.2f, %.2f) filter=0x%08X", x, y, z, rd(owner+0x40) or 0)

-- 1) restore collision: normal group 27 (0x401B) on both proxy and body
writeU32(owner + 0x40, 0x401B)
local bidb = rb(owner + 0x38, 8)
local low32 = bidb and (bidb[1] + bidb[2]*256 + bidb[3]*65536 + bidb[4]*16777216) or 0
local WORLD = 0x146c8d0f8
local world = rq(WORLD)
local worldSub = world and (rq(world + 0x60) + 0x20) or nil
local slot = low32 % 0x1000000
local chunkArray = worldSub and rq(worldSub + 0x20) or nil
local chunk = chunkArray and rq(chunkArray + math.floor(slot / 42) * 8) or nil
local body = chunk and (chunk + (slot % 42) * 0xC0) or nil
if body then writeU32(body + 0x90, 0x401B) end

-- 2) lift the character: set Y high above ground (600 is below the ~660 ground),
--    so they fall back onto the surface with collision restored.
local liftY = 800.0
writeF32(owner + 0x90 + 4, liftY)   -- +0x94 = Y
writeF32(owner + 0x1A0 + 4, liftY)  -- +0x1A4 = secondary dest Y
-- zero velocity
for _, off in ipairs({0xC0, 0xC4, 0xC8, 0xD0, 0xD4, 0xD8}) do
  writeF32(owner + off, 0.0)
end

sleep(500)
local x2, y2, z2 = rf(owner+0x90), rf(owner+0x94), rf(owner+0x98)
fmt("after:  pos=(%.2f, %.2f, %.2f) filter=0x%08X", x2, y2, z2, rd(owner+0x40) or 0)

return table.concat(out, "\n")
