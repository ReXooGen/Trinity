-- Focused test: write group 21 (and 1) for a longer window, watch Y trajectory.
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

for _, g in ipairs({21, 1, 22}) do
  local y0 = posY() or 0
  writeU32(owner + 0x40, g)
  if body then writeU32(body + 0x90, g) end
  local ys = {}
  for i = 1, 10 do
    sleep(100)
    ys[i] = posY() or 0
  end
  writeU32(owner + 0x40, Fproxy)
  if body then writeU32(body + 0x90, Fbody) end
  sleep(300)
  local parts = {}
  for i, y in ipairs(ys) do parts[i] = string.format("%.2f", y) end
  fmt("group %d: y0=%.2f  trajectory=[%s]  totalDrop=%.2f", g, y0, table.concat(parts, " "), y0 - ys[#ys])
end

fmt("final proxy=0x%08X", rd(owner+0x40) or 0)
return table.concat(out, "\n")
