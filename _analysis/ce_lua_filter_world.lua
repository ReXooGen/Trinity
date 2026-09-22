-- Find the collision filter object by scanning the world object's fields for a
-- pointer whose target vtable is a group filter vtable.
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

local WORLD = 0x146c8d0f8
local world = rq(WORLD)
fmt("world = %X", world or 0)

local filterVTs = {0x145309EB0, 0x14530AF20, 0x14530AF90, 0x1453089D8, 0x1453083D0}

-- scan world object fields (first 0x4000 bytes, 8-byte stride) for a pointer to a filter
local found = {}
if world then
  for off = 0, 0x4000, 8 do
    local p = rq(world + off)
    if p then
      local vt = rq(p)
      if vt then
        for _, fvt in ipairs(filterVTs) do
          if vt == fvt then
            found[#found+1] = { worldOff = off, obj = p, vt = vt }
          end
        end
      end
    end
  end
end

fmt("filter pointers found in world: %d", #found)
for _, f in ipairs(found) do
  fmt("  world+%X -> obj=%X vt=%X", f.worldOff, f.obj, f.vt)
  fmt("  collision table (obj+0x24, 32 dwords):")
  local rows = {}
  for g = 0, 31 do rows[g] = rd(f.obj + 0x24 + g*4) or 0 end
  for g = 0, 31 do
    if rows[g] == 0 then
      fmt("    group %d -> 0x%08X  <<< collides with NOTHING", g, rows[g])
    end
  end
  local nonz = {}
  for g = 0, 31 do if rows[g] ~= 0 then nonz[#nonz+1] = string.format("%d:%08X", g, rows[g]) end end
  fmt("  non-zero rows: %s", table.concat(nonz, " "))
end

return table.concat(out, "\n")
