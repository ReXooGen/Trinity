-- Dump the live local-player moveOwner and everything it points at that looks
-- like engine code/data, so the character-proxy / body layout becomes visible.
local OWNER = 0x3EF702AD700

local out = {}
local function fmt(s, ...) out[#out+1] = string.format(s, ...) end
local function rq(p)
  if not p then return nil end
  local ok, v = pcall(readQword, p)
  if ok then return v end
  return nil
end
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

local function tag(q)
  if not q then return "" end
  if q >= 0x140000000 and q <= 0x157FCE020 then return " IMG" end
  if q > 0x10000 and q < 0x7FFFFFFFFFFF then
    local vt = rq(q)
    if vt and vt >= 0x140000000 and vt <= 0x157FCE020 then
      return string.format(" PTR(vt=%X)", vt)
    end
    return " PTR"
  end
  return ""
end

fmt("=== OWNER %X ===", OWNER)
for off = 0, 0x250, 8 do
  local q = rq(OWNER + off) or 0
  local xs = ""
  local a, b = rf(OWNER + off), rf(OWNER + off + 4)
  if a and b then xs = string.format("  f=(%.3f, %.3f)", a, b) end
  fmt("  +%03X: %016X%s%s", off, q, tag(q), xs)
end

-- resolve owner+0x48 (manager) and owner+0x38 (body handle)
local mgr = rq(OWNER + 0x48)
local handle = rq(OWNER + 0x38)
fmt("\nmanager = %X   bodyHandle = %X", mgr or 0, handle or 0)

if mgr then
  local vt = rq(mgr)
  fmt("manager vtable = %X", vt or 0)
  if vt then
    fmt("manager vtable slots:")
    for i = 0, 0x20 do
      local slot = rq(vt + i * 8)
      if not slot then break end
      if slot < 0x140000000 or slot > 0x157FCE020 then break end
      fmt("   [+%03X] = %X", i * 8, slot)
    end
  end
  fmt("manager dump:")
  for off = 0, 0x80, 8 do
    local q = rq(mgr + off) or 0
    fmt("   +%03X: %016X%s", off, q, tag(q))
  end
end

return table.concat(out, "\n")
