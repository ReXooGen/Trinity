-- Find the player moveOwner in the fresh process, replicate getBody with pure
-- reads, and dump the body struct looking for collisionFilterInfo.
local out = {}
local function fmt(s, ...) out[#out+1] = string.format(s, ...) end

local MOD = getAddressSafe("Trinity.asi")
if not MOD then return "no Trinity module" end

local function rb(p, n)
  local ok, v = pcall(readBytes, p, n, true)
  if ok and v then return v end
  return nil
end
local function rq(p)
  if not p or p < 0x10000 then return nil end
  local ok, v = pcall(readQword, p)
  if ok and v ~= nil then return v end
  return nil
end
local function rd(p, t)
  local ok, v = pcall(readInteger, p, t or vtDword)
  if ok then return v end
  return nil
end
local function rf(p)
  local ok, v = pcall(readFloat, p)
  if ok then return v end
  return nil
end

-- 1) find Trinity global holding a move-owner candidate
local MOD_SIZE = 2224128
local targets = {}
local addr = MOD
while addr < MOD + MOD_SIZE do
  local buf = rb(addr, 0x1000)
  if buf then
    for i = 1, #buf - 7 do
      local b1 = buf[i]
      if b1 >= 0x48 and b1 <= 0x4F then
        local op = buf[i+1]
        if op == 0x8B then
          local modrm = buf[i+2]
          if modrm and math.floor(modrm / 64) == 0 and (modrm % 8) == 5 then
            local d = buf[i+3] + buf[i+4]*256 + buf[i+5]*65536 + buf[i+6]*16777216
            if d >= 0x80000000 then d = d - 0x100000000 end
            local ia = addr + i - 1
            local tgt = ia + 7 + d
            if tgt >= MOD + 0xFA000 and tgt < MOD + MOD_SIZE then targets[tgt] = true end
          end
        end
      end
    end
    addr = addr + 0x1000 - 8
  else
    addr = addr + 0x1000
  end
end

local function looksOwner(o)
  if not o or o < 0x10000 or o > 0x7FFFFFFFFFFF then return false end
  local vt = rq(o)
  if not vt or vt < 0x140000000 or vt > 0x157FCE020 then return false end
  local x, y, z = rf(o + 0x90), rf(o + 0x94), rf(o + 0x98)
  if not (x and y and z) then return false end
  if x ~= x or y ~= y or z ~= z then return false end
  local mag = math.abs(x) + math.abs(y) + math.abs(z)
  if mag < 1 or mag > 1e9 then return false end
  return true
end

local owner = nil
for tgt in pairs(targets) do
  local v = rq(tgt)
  if looksOwner(v) then
    fmt("owner candidate: global=%X -> %X pos=(%.2f,%.2f,%.2f)", tgt, v, rf(v+0x90), rf(v+0x94), rf(v+0x98))
    owner = v
  end
end

if not owner then return table.concat(out, "\n") .. "\nNO OWNER FOUND" end

-- 2) body id
local bodyId = rq(owner + 0x38)
fmt("bodyId = %X", bodyId or 0)

-- 3) replicate getBody
local WORLD = 0x146c8d0f8
local world = rq(WORLD)
fmt("world = %X", world or 0)
local worldSub = nil
if world then
  worldSub = rq(world + 0x60)
  worldSub = worldSub and (worldSub + 0x20) or nil
end
fmt("world_sub(+0x60) = %X", worldSub or 0)

if bodyId and worldSub then
  -- slot from low 24 bits of low 32 (negative-id path skips lookup)
  local low = bodyId % 0x100000000
  local slot = low % 0x1000000
  fmt("low32=%X slot=%d", low, slot)
  local chunkIdx = math.floor(slot / 42)
  local elem = slot % 42
  local chunkArray = rq(worldSub + 0x20)
  fmt("chunkArray=%X chunkIdx=%d elem=%d", chunkArray or 0, chunkIdx, elem)
  if chunkArray then
    local chunk = rq(chunkArray + chunkIdx * 8)
    fmt("chunk=%X", chunk or 0)
    if chunk then
      local body = chunk + elem * 0xC0
      fmt("BODY = %X", body)
      -- dump body struct
      fmt("=== BODY DUMP ===")
      for off = 0, 0x140, 4 do
        local v = rd(body + off)
        local f = rf(body + off)
        local fs = (f and f == f) and string.format("%.4f", f) or "-"
        fmt("  +%03X: %08X  f=%s", off, v or 0, fs)
      end
      fmt("=== BODY qwords ===")
      for off = 0, 0xC0, 8 do
        fmt("  +%03X: %016X", off, rq(body + off) or 0)
      end
    end
  end
end

return table.concat(out, "\n")
