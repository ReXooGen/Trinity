-- Safe reversible test: write collision-filter-info candidates to the player
-- body's +0x90, watch the proxy Y for a drop (collision off), restore original.
local out = {}
local function fmt(s, ...) out[#out+1] = string.format(s, ...) end

local MOD = getAddressSafe("Trinity.asi")
if not MOD then return "no Trinity module" end

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
local function writeU32(p, v)
  if not p or not v then return false end
  local b = { v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF }
  local ok = pcall(writeBytes, p, b)
  return ok
end

-- 1) find moveOwner via Trinity global scan
local MOD_SIZE = 2224128
local function rb(p, n)
  local ok, v = pcall(readBytes, p, n, true)
  if ok and v then return v end
  return nil
end
local function findOwner()
  local targets = {}
  local addr = MOD
  while addr < MOD + MOD_SIZE do
    local b = rb(addr, 0x1000)
    if b then
      for i = 1, #b - 7 do
        if b[i] >= 0x48 and b[i] <= 0x4F and b[i+1] == 0x8B then
          local modrm = b[i+2]
          if modrm and math.floor(modrm / 64) == 0 and (modrm % 8) == 5 then
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
      -- must be a heap object (NOT inside the game image range)
      if v >= 0x140000000 and v <= 0x157FCE020 then
        -- skip static image objects
      else
        local vt = rq(v)
        if vt and vt >= 0x140000000 and vt <= 0x157FCE020 then
          -- require a valid body id at +0x38: read 8 bytes, top byte bit7 set
          local bidb = rb(v + 0x38, 8)
          local top = bidb and bidb[8] or 0
          if (top & 0x80) ~= 0 then
            local x, y, z = rf(v+0x90), rf(v+0x94), rf(v+0x98)
            if x and y and z and x == x and y == y and z == z then
              -- plausible gameplay coords: all three non-trivial
              if math.abs(x) > 5 and math.abs(y) > 5 and math.abs(z) > 5 then
                return v
              end
            end
          end
        end
      end
    end
  end
  return nil
end

local owner = findOwner()
if not owner then return "no owner" end
fmt("owner=%X", owner)

-- bodyId low 32 bits read exactly from bytes (avoids Lua 64-bit precision loss)
local bidb = rb(owner + 0x38, 8)
local bodyIdLow32 = bidb and (bidb[1] + bidb[2]*256 + bidb[3]*65536 + bidb[4]*16777216) or 0
fmt("bodyId low32=%08X", bodyIdLow32)

-- 2) replicate getBody
local WORLD = 0x146c8d0f8
local world = rq(WORLD)
local worldSub = world and (rq(world + 0x60) + 0x20) or nil
local slot = bodyIdLow32 % 0x1000000
local chunkArray = worldSub and rq(worldSub + 0x20) or nil
local chunk = chunkArray and rq(chunkArray + math.floor(slot / 42) * 8) or nil
local body = chunk and (chunk + (slot % 42) * 0xC0) or nil
fmt("body=%X slot=%d", body or 0, slot)

-- The character proxy's collision filter info is what castShape consumes, and
-- the integrator passes it from [moveOwner+0x40]. The body's +0x90 is a mirror.
local F_proxy = rd(owner + 0x40)
local F_body  = body and rd(body + 0x90) or nil
fmt("proxy filter (owner+0x40) = 0x%08X", F_proxy or 0)
fmt("body  filter (body +0x90) = 0x%08X", F_body or 0)

if not F_proxy then
  fmt("WARNING: proxy filter read failed at owner+0x40. Aborting.")
  return table.concat(out, "\n")
end

local function posY()
  return rf(owner + 0x94)
end

local candidates = {0x00000000, 0x0000001F, 0x80000000, 0xFFFFFFFF}

for _, C in ipairs(candidates) do
  local y0 = posY()
  writeU32(owner + 0x40, C)
  if body then writeU32(body + 0x90, C) end
  local samples = {}
  for i = 1, 6 do
    sleep(100)
    local y = posY()
    local fp = rd(owner + 0x40)
    local fb = body and rd(body + 0x90) or 0
    samples[i] = string.format("Y=%.2f", y or 0)
    samples[i] = samples[i] .. string.format("(P=%08X B=%08X)", fp or 0, fb or 0)
  end
  local y1 = posY()
  -- restore both
  writeU32(owner + 0x40, F_proxy)
  if body then writeU32(body + 0x90, F_body) end
  sleep(400)
  local yAfter = posY()
  fmt("candidate 0x%08X: y0=%.2f  y1=%.2f  yAfterRestore=%.2f", C, y0 or 0, y1 or 0, yAfter or 0)
  fmt("   samples: %s", table.concat(samples, "  "))
end

fmt("final: proxy=0x%08X body=0x%08X", rd(owner+0x40) or 0, body and rd(body+0x90) or 0)

return table.concat(out, "\n")
