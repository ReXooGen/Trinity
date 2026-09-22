local out = {}
local function fmt(s, ...) out[#out+1] = string.format(s, ...) end
local function rq(p)
  local ok, v = pcall(readQword, p)
  if ok then return v end
  return nil
end
local function rd(p, t)
  local ok, v = pcall(readInteger, p, t or vtDword)
  if ok then return v end
  return nil
end
local function rw(p)
  local ok, v = pcall(readInteger, p, vtWord)
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

-- Real module base from CE
local modBase = getAddressSafe("CrimsonDesert.exe")
fmt("module base (CE) = %X", modBase or 0)
fmt("MZ @base: %s", hex(modBase, 4))

local e_lfanew = rd(modBase + 0x3C)
fmt("e_lfanew = %X", e_lfanew or 0)
local nt = modBase + (e_lfanew or 0)
fmt("NT sig: %s", hex(nt, 6))
local machine = rw(nt + 4)
local numSec = rw(nt + 6)
local optSize = rw(nt + 0x14)
fmt("machine=%s numSec=%s optHdrSize=%s", tostring(machine), tostring(numSec), tostring(optSize))

if numSec and numSec > 0 and numSec < 32 and optSize and optSize > 0 and optSize < 0x400 then
  local secTab = nt + 0x18 + optSize
  for i = 0, numSec - 1 do
    local s = secTab + i * 40
    fmt("  sec %-9s VA=%08X VSize=%08X Raw=%08X RawSize=%08X",
        ascii(s, 8), rd(s + 0x0C) or 0, rd(s + 8) or 0, rd(s + 0x14) or 0, rd(s + 0x10) or 0)
  end
end

fmt("image size @opt+0x38 = %X", rd(modBase + (e_lfanew or 0) + 0x50) or 0)

-- Does the BN-recommended address for the char-manager global hold a pointer?
fmt("cand1 0x1461830F8 -> %X", rq(0x1461830F8) or 0)
fmt("cand2 0x146D696D8 -> %X", rq(0x146D696D8) or 0)

-- Look for the string "hknpCharacterProxy" by both candidate bases
fmt("AOB of locostepper prologue (@14369FF60): %s", hex(0x14369FF60 - 0, 12))

return table.concat(out, "\n")
