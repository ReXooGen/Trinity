-- Follow castShape's inner object ([world_sub+0xb70]) and scan it + world_sub
-- for a pointer to a group-filter vtable, then read the collision table.
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
local worldSub = world and (rq(world + 0x60) + 0x20) or nil
fmt("world=%X worldSub=%X", world or 0, worldSub or 0)

local filterVTs = {0x145309EB0, 0x14530AF20, 0x14530AF90, 0x1453089D8, 0x1453083D0, 0x145308718}

-- scan a set of objects for pointers to filter vtables
local function scanObject(base, name, maxoff)
  if not base then return end
  for off = 0, maxoff, 8 do
    local p = rq(base + off)
    if p then
      local vt = rq(p)
      if vt then
        for _, fvt in ipairs(filterVTs) do
          if vt == fvt then
            fmt("FOUND filter: %s+%X -> obj=%X vt=%X", name, off, p, vt)
            -- read collision table at +0x24
            local rows = {}
            for g = 0, 31 do rows[g] = rd(p + 0x24 + g*4) or 0 end
            for g = 0, 31 do
              if rows[g] == 0 then fmt("  group %d -> 0x00000000  <<< NO COLLISION", g) end
            end
            local nonz = {}
            for g = 0, 31 do if rows[g] ~= 0 then nonz[#nonz+1] = string.format("%d:%08X", g, rows[g]) end end
            fmt("  rows: %s", table.concat(nonz, " "))
          end
        end
      end
    end
  end
end

scanObject(worldSub, "worldSub", 0x2000)

-- follow castShape inner object
local castObj = rq(worldSub + 0xb70)
fmt("castObj = [worldSub+0xb70] = %X", castObj or 0)
scanObject(castObj, "castObj", 0x3000)

-- also scan the world root itself
scanObject(world, "world", 0x3000)

return table.concat(out, "\n")
