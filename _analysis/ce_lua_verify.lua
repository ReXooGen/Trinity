-- Verify static offsets against LIVE memory and locate the char manager.
local out = {}
local function fmt(s, ...) out[#out+1] = string.format(s, ...) end

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
local function hex(p, n)
  local ok, t = pcall(readBytes, p, n, true)
  if not ok or not t then return "READ-FAIL" end
  local s = {}
  for i = 1, #t do s[#s+1] = string.format("%02X", t[i]) end
  return table.concat(s, " ")
end
local function rf(p)
  local ok, v = pcall(readFloat, p)
  if ok then return v end
  return nil
end

fmt("integrator @144282080: %s", hex(0x144282080, 16))
fmt("locostepper @14369FF60: %s", hex(0x14369FF60, 16))
fmt("global @1461830F8 -> %X", rq(0x1461830F8) or 0)

local mgr = rq(0x1461830F8)
if mgr and mgr > 0x10000 and mgr < 0x7FFFFFFFFFFF then
  fmt("mgr+0xB8=%X  mgr+0xC0=%s", rq(mgr+0xB8) or 0, tostring(rd(mgr+0xC0)))
end

-- enumerate memory regions to find where the image actually is
local regions = enumMemoryRegions()
fmt("regions=%d", regions and #regions or 0)
if regions then
  for i = 1, math.min(#regions, 25) do
    local r = regions[i]
    fmt("  %X-%X %s %s", r.BaseAddress or 0, (r.BaseAddress or 0)+(r.RegionSize or 0),
        tostring(r.Protect), tostring(r.State))
  end
end

return table.concat(out, "\n")
