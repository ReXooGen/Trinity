-- Find the player's moveOwner by AOB-searching for the position triple from
-- Trinity's own log (Y is the up axis), then dump the surrounding object.
local out = {}
local function fmt(s, ...) out[#out+1] = string.format(s, ...) end

local function rd(p, t)
  local ok, v = pcall(readInteger, p, t or vtDword)
  if ok then return v end
  return nil
end
local function rq(p)
  local ok, v = pcall(readQword, p)
  if ok then return v end
  return nil
end
local function rf(p)
  local ok, v = pcall(readFloat, p)
  if ok then return v end
  return nil
end

-- Candidate positions from Trinity.log (engine units)
local cands = {
  {-625.63, 620.37, -505.82},
  {-613.61, 624.19, -497.01},
  {-616.91, 649.62, -492.44},
}

local function floatPattern(x, y, z, step)
  -- build "?? ?? ?? ?? xx xx xx xx ?? ?? ?? ?? yy ... zz" is complex; instead
  -- scan for the exact 12 bytes at +0x90 and require the +0x90 alignment is not
  -- knowable, so search the raw triple with a wildcard scan.
  local function le(f)
    local b = {}
    local sign = 1
    if f < 0 then sign = -1 f = -f end
    -- IEEE754 single
    local m, e = math.frexp(f)
    e = e + 126
    local mant = math.floor((m * 2 - 1) * 8388608 + 0.5)
    local v = e * 8388608 + mant
    if f == 0 then v = 0 end
    local out = {}
    for i = 1, 4 do out[i] = v % 256; v = math.floor(v / 256) end
    if sign < 0 then out[4] = out[4] + 128 end
    return out
  end
  local b = le(x)
  local b2 = le(y)
  local b3 = le(z)
  local parts = {}
  for i = 1, 4 do parts[#parts+1] = string.format("%02X", b[i]) end
  for i = 1, 4 do parts[#parts+1] = string.format("%02X", b2[i]) end
  for i = 1, 4 do parts[#parts+1] = string.format("%02X", b3[i]) end
  return table.concat(parts, " ")
end

local function scan(pat, what)
  local s = AOBScan(pat, "+W-C")
  if not s then s = AOBScan(pat) end
  if not s or s.Count == 0 then
    fmt("scan %s: no match", what)
    return nil
  end
  local res = {}
  for i = 0, math.min(s.Count, 20) - 1 do res[#res+1] = tonumber(s.getString(i), 16) end
  s.destroy()
  fmt("scan %s: %d match(es)", what, #res)
  return res
end

for _, c in ipairs(cands) do
  local pat = floatPattern(c[1], c[2], c[3])
  fmt("--- candidate (%.2f, %.2f, %.2f) pattern=%s", c[1], c[2], c[3], pat)
  local hits = scan(pat, "pos triple")
  if hits then
    for _, h in ipairs(hits) do
      local owner = h - 0x90
      fmt("  hit=%X  owner(candidate)=%X", h, owner)
      -- verify: read back the triple
      fmt("    pos@hit = (%.3f, %.3f, %.3f)", rf(h) or 0, rf(h+4) or 0, rf(h+8) or 0)
      -- dump owner header
      for off = 0, 0x60, 8 do
        fmt("      owner+%03X = %016X", off, rq(owner+off) or 0)
      end
    end
  end
end

return table.concat(out, "\n")
