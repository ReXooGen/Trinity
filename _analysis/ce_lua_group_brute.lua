-- Brute-force: test each 5-bit collision group (0..31) in the proxy filter info
-- and detect which one makes the character lose ground support (Y drops).
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

-- find owner (heap, valid body id top bit, plausible coords)
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

local owner = findOwner()
if not owner then return "no owner" end
fmt("owner=%X", owner)

-- body resolution (for syncing body+0x90)
local bidb = rb(owner + 0x38, 8)
local low32 = bidb and (bidb[1] + bidb[2]*256 + bidb[3]*65536 + bidb[4]*16777216) or 0
local WORLD = 0x146c8d0f8
local world = rq(WORLD)
local worldSub = world and (rq(world + 0x60) + 0x20) or nil
local slot = low32 % 0x1000000
local chunkArray = worldSub and rq(worldSub + 0x20) or nil
local chunk = chunkArray and rq(chunkArray + math.floor(slot / 42) * 8) or nil
local body = chunk and (chunk + (slot % 42) * 0xC0) or nil

local Fproxy = rd(owner + 0x40)
local Fbody = body and rd(body + 0x90) or nil
fmt("orig proxy=0x%08X body=0x%08X", Fproxy or 0, Fbody or 0)

local function posY() return rf(owner + 0x94) end

local results = {}
for g = 0, 31 do
  local y0 = posY() or 0
  writeU32(owner + 0x40, g)         -- group g, everything else 0
  if body then writeU32(body + 0x90, g) end
  local ymin = y0
  for i = 1, 3 do
    sleep(80)
    local y = posY() or 0
    if y < ymin then ymin = y end
  end
  local drop = y0 - ymin
  -- restore
  writeU32(owner + 0x40, Fproxy)
  if body then writeU32(body + 0x90, Fbody) end
  sleep(60)
  if drop > 0.5 then
    fmt("group %2d: DROP %.2f  <<< candidate", g, drop)
    results[#results+1] = g
  elseif drop > 0.05 then
    fmt("group %2d: slight drop %.2f", g, drop)
  end
end

fmt("final proxy=0x%08X body=0x%08X", rd(owner+0x40) or 0, body and rd(body+0x90) or 0)
fmt("candidates with drop>0.5: %s", #results > 0 and table.concat(results, ",") or "none")

return table.concat(out, "\n")
