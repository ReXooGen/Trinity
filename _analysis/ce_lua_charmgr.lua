-- Locate the char-manager global by scanning the live image for the AOB anchors
-- recorded in src/game/offsets.h, then dump the char manager and its list.
local out = {}
local function fmt(s, ...) out[#out+1] = string.format(s, ...) end

local BASE = 0x140000000
local function rq(p)
  if not p or p < 0x10000 or p > 0x7FFFFFFFFFFF then return nil end
  local ok, v = pcall(readQword, p)
  if ok then return v end
  return nil
end
local function rd(p, t)
  if not p then return nil end
  local ok, v = pcall(readInteger, p, t or vtDword)
  if ok then return v end
  return nil
end
local function rf(p)
  local ok, v = pcall(readFloat, p)
  if ok then return v end
  return nil
end
local function rb(p)
  local ok, v = pcall(readBytes, p, 1, true)
  if ok and v then return v[1] end
  return nil
end
local function hexq(p) if not p then return "nil" end return string.format("%X", p) end

local function scan(pat, what)
  local s = AOBScan(pat, "+X")
  if not s or s.Count == 0 then
    fmt("scan %s: NO MATCH", what)
    return nil
  end
  fmt("scan %s: %d match(es)", what, s.Count)
  local res = {}
  for i = 0, math.min(s.Count, 8) - 1 do
    res[#res+1] = tonumber(s.getString(i), 16)
  end
  s.destroy()
  return res
end

-- Anchor A: "4D 8B 00 49 C1 E8 20 48 8D 54 24 78 48 8B 0D ?? ?? ?? ?? 48 8B 09 E8"
local a = scan("4D 8B 00 49 C1 E8 20 48 8D 54 24 78 48 8B 0D ?? ?? ?? ?? 48 8B 09 E8", "anchorA")
if a then
  for _, addr in ipairs(a) do
    local movAt = addr + 0x0C
    local rip = rd(movAt + 3, vtDword)
    local glob = movAt + 7 + (rip or 0)
    fmt("  A: match=%X movAt=%X rip=%X -> global=%X value=%X", addr, movAt, rip or 0, glob, rq(glob) or 0)
    -- dump the manager
    local mgr = rq(glob)
    if mgr and mgr > 0x10000 then
      fmt("   mgr=%X dump:", mgr)
      for off = 0, 0x100, 8 do
        fmt("     +%03X = %016X", off, rq(mgr+off) or 0)
      end
      break
    end
  end
end

-- Anchor B: "48 8B 05 ?? ?? ?? ?? 48 8B 08 4D 8B 00 49 C1 E8 20"
local b = scan("48 8B 05 ?? ?? ?? ?? 48 8B 08 4D 8B 00 49 C1 E8 20", "anchorB")
if b then
  for _, addr in ipairs(b) do
    local rip = rd(addr + 3, vtDword)
    local glob = addr + 7 + (rip or 0)
    fmt("  B: match=%X -> global=%X value=%X", addr, glob, rq(glob) or 0)
  end
end

return table.concat(out, "\n")
