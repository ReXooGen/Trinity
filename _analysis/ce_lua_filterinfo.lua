-- Read the player body's collision filter info (property 0xF001) by replicating
-- getBodyPropertyImpl with pure reads.
local out = {}
local function fmt(s, ...) out[#out+1] = string.format(s, ...) end
local function rq(p)
  if not p or p < 0x10000 or p > 0x7FFFFFFFFFFF then return nil end
  local ok, v = pcall(readQword, p)
  if ok and v ~= nil then return v end
  return nil
end
local function rd(p, t)
  local ok, v = pcall(readInteger, p, t or vtDword)
  if ok then return v end
  return nil
end
local function rw(p)
  -- read 2 bytes directly (CE Lua 'vtWord' may not be defined in this env)
  local ok, v = pcall(readInteger, p, 2)
  if ok and v then return (v & 0xFFFF) end
  return nil
end

-- world sub-object (client realm)
local WORLD = 0x146c8d0f8
local world = rq(WORLD)
local worldSub = world and (rq(world + 0x60) + 0x20) or nil
fmt("world=%X worldSub=%X", world or 0, worldSub or 0)

-- body slot (from getBody: slot = low24 of low32 of body id, negative path)
local bodyId = 0x800000000100001A
local slot = (bodyId % 0x100000000) % 0x1000000
fmt("body slot = %d", slot)

-- property map at worldSub + 0x4e8
local pmap = worldSub + 0x4e8
local entries = rq(pmap + 0x00)
local size = rd(pmap + 0x0C)
fmt("property map: entries=%X size(mask)=%X", entries or 0, size or 0)

local function lookupProp(key16)
  -- hash = bswap(key * 0x9e3779b1) & mask
  local h = (key16 * 0x9e3779b1) % 0x100000000
  -- bswap
  h = ((h & 0xFF) << 24) | ((h & 0xFF00) << 8) | ((h & 0xFF0000) >> 8) | ((h >> 24) & 0xFF)
  local idx = h & size
  local key = key16 & 0xFFFF
  local idx0 = idx
  while true do
    local e = entries + idx * 0x10
    local k = rw(e)
    if k == nil then return nil end
    if k == key then return e end
    if k == 0xFFFF then return nil end  -- empty
    idx = (idx + 1) & size
    if idx == idx0 then return nil end
  end
end

-- list a few property keys present in the map for context
fmt("=== scanning property map for keys ===")
local foundKeys = {}
if entries and size and size > 0 and size < 0x10000 then
  for i = 0, size do
    local e = entries + i * 0x10
    local k = rw(e)
    if k and k ~= 0xFFFF and k ~= 0 then
      foundKeys[#foundKeys+1] = k
    end
  end
  fmt("found %d non-empty property keys", #foundKeys)
  for _, k in ipairs(foundKeys) do
    fmt("  key=0x%04X", k)
  end
end

-- read property 0xF001 value for the body
local entry = lookupProp(0xF001)
fmt("entry for 0xF001 = %X", entry or 0)
if entry then
  local storage = rq(entry + 8)
  fmt("property storage = %X", storage or 0)
  if storage then
    local count = rd(storage + 0x18)
    local bitmask = rq(storage + 8)
    local valarray = rq(storage + 0x20)
    fmt("count=%d bitmask=%X valarray=%X", count or -1, bitmask or 0, valarray or 0)
    if bitmask and valarray and slot < (count or 0) then
      local wordIdx = math.floor(slot / 32)
      local bitIdx = slot % 32
      local mask = rd(bitmask + wordIdx * 4) or 0
      local present = (mask >> bitIdx) & 1
      fmt("present bit = %d", present)
      if present == 1 then
        local vaddr = valarray + slot * 8
        local v = rq(vaddr)
        fmt(">>> property 0xF001 value (collisionFilterInfo) @%X = %016X", vaddr, v or 0)
        fmt("    as two dwords: %08X %08X", (v or 0) % 0x100000000, math.floor((v or 0) / 0x100000000))
      end
    end
  end
end

return table.concat(out, "\n")
