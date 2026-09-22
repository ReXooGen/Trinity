-- Find Trinity's g_playerMoveOwner global by scanning Trinity.asi itself: we
-- look for RIP-relative `mov [mem], reg` writes whose target is a writable qword
-- inside the module, then report the candidates.
local out = {}
local function fmt(s, ...) out[#out+1] = string.format(s, ...) end

local MOD_BASE = 0x7FFC156A0000
local MOD_SIZE = 2224128

local mod = getAddressSafe("Trinity.asi")
fmt("Trinity.asi base = %X", mod or 0)
if not mod then return "no module" end
MOD_BASE = mod

local function rd(p, t)
  local ok, v = pcall(readInteger, p, t or vtDword)
  if ok then return v end
  return nil
end
local function rb(p, n)
  local ok, v = pcall(readBytes, p, n, true)
  if ok and v then return v end
  return nil
end

-- Collect region protections once for the module
local regions = enumMemoryRegions()
local function protOf(a)
  for i = 1, #regions do
    local r = regions[i]
    local b, s = r.BaseAddress or 0, r.RegionSize or 0
    if a >= b and a < b + s then return r.Protect or 0, b, s end
  end
  return nil
end

local p0, b0, s0 = protOf(MOD_BASE)
fmt("module first region: base=%X size=%X prot=%X", b0 or 0, s0 or 0, p0 or 0)

-- Walk committed regions overlapping the module
local seen = {}
for i = 1, #regions do
  local r = regions[i]
  local b, s = r.BaseAddress or 0, r.RegionSize or 0
  if b >= MOD_BASE and b < MOD_BASE + MOD_SIZE and (r.State or 0) == 0x1000 then
    fmt("  region %X size=%X prot=%X", b, s, r.Protect or 0)
  end
end

-- Scan the module for `mov [rip+disp32], reg64` (48/4C 89 /x with mod=00 rm=101)
local hits = {}
local addr = MOD_BASE
local enda = MOD_BASE + MOD_SIZE
while addr < enda do
  local buf = rb(addr, 0x1000)
  if buf then
    for i = 1, #buf - 7 do
      local b1, b2 = buf[i], buf[i+1]
      if (b1 == 0x48 or b1 == 0x4C) and b2 == 0x89 then
        local modrm = buf[i+2]
        if modrm and (modrm % 64) >= 0x28 and (modrm % 64) <= 0x2F and math.floor(modrm/64) == 0 then
          -- mod=00, rm=101 -> RIP relative; reg field = (modrm/8)%8
          local d = buf[i+3] + buf[i+4]*256 + buf[i+5]*65536 + buf[i+6]*16777216
          if d >= 0x80000000 then d = d - 0x100000000 end
          local instrAddr = addr + i - 1
          local tgt = instrAddr + 7 + d
          if tgt >= MOD_BASE and tgt < MOD_BASE + MOD_SIZE then
            hits[#hits+1] = { at = instrAddr, tgt = tgt, reg = math.floor(modrm/8) % 8 }
          end
        end
      end
    end
    addr = addr + 0x1000 - 8
  else
    addr = addr + 0x1000
  end
end

fmt("candidate RIP-relative stores into the module: %d", #hits)
-- group by target
local byTgt = {}
for _, h in ipairs(hits) do
  byTgt[h.tgt] = (byTgt[h.tgt] or 0) + 1
end
local list = {}
for t, n in pairs(byTgt) do list[#list+1] = { t = t, n = n } end
table.sort(list, function(a, b) return a.n > b.n end)
for i = 1, math.min(#list, 30) do
  local t = list[i].t
  fmt("  tgt=%X writes=%d  value=%X", t, list[i].n, rd(t, vtQword) or 0)
end

return table.concat(out, "\n")
