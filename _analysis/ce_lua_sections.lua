local out = {}
local function fmt(s, ...) out[#out+1] = string.format(s, ...) end
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
local function ascii(p, n)
  local ok, t = pcall(readBytes, p, n, true)
  if not ok or not t then return "READ-FAIL" end
  local s = {}
  for i = 1, #t do
    local c = t[i]
    s[#s+1] = (c >= 32 and c < 127) and string.char(c) or "."
  end
  return table.concat(s)
end

-- The AOB anchor lives 0x6C0000 above where Binary Ninja says it should be.
-- Compare both anchor sites and the string tail.
fmt("BN global @1461830F8 -> %X", rd(0x1461830F8, vtQword) or 0)
fmt("LIVE global @146D696D8 -> %X", rd(0x146D696D8, vtQword) or 0)

-- Compare a known STRING: hknpDisableCollisionFilter @@ 0x145308A90
fmt("str @145308A90: %s", ascii(0x145308A90, 0x1A))
fmt("str @145308A90+0x6C0000: %s", ascii(0x145308A90 + 0x6C0000, 0x1A))

-- Compare the known function prologues
fmt("fn  @144282080: %s", hex(0x144282080, 8))
fmt("fn  @144282080+0x6C0000: %s", hex(0x144282080 + 0x6C0000, 8))
fmt("fn  @14369FF60: %s", hex(0x14369FF60, 8))
fmt("fn  @14369FF60+0x6C0000: %s", hex(0x14369FF60 + 0x6C0000, 8))

-- Section table from the live image header
local e_lfanew = rd(0x140000000 + 0x3C)
fmt("e_lfanew=%X", e_lfanew or 0)
if e_lfanew then
  local nt = 0x140000000 + e_lfanew
  fmt("Signature=%s Machine=%X NumSections=%d SizeOfOptHdr=%X",
      hex(nt, 4), rd(nt+4, vtWord) or 0, rd(nt+6, vtWord) or 0, rd(nt+0x14, vtWord) or 0)
  local numSec = rd(nt+6, vtWord) or 0
  local optSize = rd(nt+0x14, vtWord) or 0
  local secTab = nt + 0x18 + optSize
  for i = 0, numSec - 1 do
    local s = secTab + i * 40
    fmt("  sec %-10s VA=%08X VSize=%08X Raw=%08X RawSize=%08X",
        ascii(s, 8), rd(s+0x0C) or 0, rd(s+8) or 0, rd(s+0x14) or 0, rd(s+0x10) or 0)
  end
end

return table.concat(out, "\n")
