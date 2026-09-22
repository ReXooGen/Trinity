-- Locate g_playerMoveOwner (or an equivalent) by scanning Trinity.asi for
-- RIP-relative LOADS and validating each candidate address as a move owner,
-- then dump the owner so the character-proxy layout is visible.
local out = {}
local function fmt(s, ...) out[#out+1] = string.format(s, ...) end

local MOD = getAddressSafe("Trinity.asi")
local MOD_SIZE = 2224128
if not MOD then return "no module" end

local function rb(p, n)
  local ok, v = pcall(readBytes, p, n, true)
  if ok and v then return v end
  return nil
end
local function rq(p)
  if not p then return nil end
  local ok, v = pcall(readQword, p)
  if ok then return v end
  return nil
end
local function rf(p)
  local ok, v = pcall(readFloat, p)
  if ok then return v end
  return nil
end

-- 1) collect all RIP-relative data targets in the module
local targets = {}
local addr = MOD
while addr < MOD + MOD_SIZE do
  local buf = rb(addr, 0x1000)
  if buf then
    for i = 1, #buf - 7 do
      local b1 = buf[i]
      if b1 >= 0x48 and b1 <= 0x4F then
        local op = buf[i+1]
        if op == 0x8B or op == 0x8D or op == 0x89 then
          local modrm = buf[i+2]
          if modrm and math.floor(modrm / 64) == 0 and (modrm % 8) == 5 then
            local d = buf[i+3] + buf[i+4]*256 + buf[i+5]*65536 + buf[i+6]*16777216
            if d >= 0x80000000 then d = d - 0x100000000 end
            local instrAddr = addr + i - 1
            local tgt = instrAddr + 7 + d
            if tgt >= MOD + 0xFA000 and tgt < MOD + MOD_SIZE then
              targets[tgt] = true
            end
          end
        end
      end
    end
    addr = addr + 0x1000 - 8
  else
    addr = addr + 0x1000
  end
end

-- 2) validate each target's current value as a move-owner candidate
local function looksLikeOwner(o)
  if not o or o < 0x10000 or o > 0x7FFFFFFFFFFF then return false end
  local vt = rq(o)
  if not vt or vt < 0x140000000 or vt > 0x157FCE020 then return false end
  -- position at +0x90 must be finite floats
  local x, y, z = rf(o + 0x90), rf(o + 0x94), rf(o + 0x98)
  if not (x and y and z) then return false end
  if x ~= x or y ~= y or z ~= z then return false end
  local mag = math.abs(x) + math.abs(y) + math.abs(z)
  if mag < 1 or mag > 1e9 then return false end
  return true, vt, x, y, z
end

local found = {}
for tgt in pairs(targets) do
  local v = rq(tgt)
  local ok, vt, x, y, z = looksLikeOwner(v)
  if ok then
    found[#found+1] = { tgt = tgt, val = v, vt = vt, x = x, y = y, z = z }
  end
end

fmt("targets=%d candidates=%d", (function() local n=0 for _ in pairs(targets) do n=n+1 end return n end)(), #found)
for _, f in ipairs(found) do
  fmt("  global=%X -> owner=%X vt=%X pos=(%.2f, %.2f, %.2f)", f.tgt, f.val, f.vt, f.x, f.y, f.z)
end

-- 3) dump the first candidate
if found[1] then
  local o = found[1].val
  fmt("=== OWNER DUMP %X ===", o)
  for off = 0, 0x1B0, 8 do
    local q = rq(o + off) or 0
    local tag = ""
    if q >= 0x140000000 and q <= 0x157FCE020 then tag = " IMG"
    elseif q > 0x10000 and q < 0x7FFFFFFFFFFF then tag = " PTR" end
    fmt("  +%03X: %016X%s", off, q, tag)
  end
end

return table.concat(out, "\n")
