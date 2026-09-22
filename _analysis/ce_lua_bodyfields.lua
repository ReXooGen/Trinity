-- Dump the exact dword layout around the moveOwner's body-handle / manager
-- region, where the collision filter info is expected to live.
local OWNER = 0x3EF702AD700
local out = {}
local function fmt(s, ...) out[#out+1] = string.format(s, ...) end
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
local function rq(p)
  local ok, v = pcall(readQword, p)
  if ok then return v end
  return nil
end

fmt("== dword map of owner+0x30 .. +0x60 ==")
for off = 0x30, 0x60, 4 do
  local v = rd(OWNER + off)
  local f = rf(OWNER + off)
  fmt("  +%03X: %08X   f=%s", off, v or 0, f and string.format("%.4f", f) or "-")
end

fmt("\n== qwords ==")
for off = 0x30, 0x60, 8 do
  fmt("  +%03X: %016X", off, rq(OWNER + off) or 0)
end

-- is the manager pointer stable / shared? compare with a second read later
fmt("\nmanager=%X  owner+0x50=%X", rq(OWNER + 0x48) or 0, rq(OWNER + 0x50) or 0)
fmt("owner+0x48 == owner+0x50 ? %s", tostring(rq(OWNER+0x48) == rq(OWNER+0x50)))

-- The manager is in .didata (static), so dump its neighbours for a vtable search
local MGR = rq(OWNER + 0x48)
if MGR then
  fmt("\n== manager neighbours ==")
  for off = -0x20, 0x20, 8 do
    fmt("  mgr%+04X: %016X", off, rq(MGR + off) or 0)
  end
end

return table.concat(out, "\n")
