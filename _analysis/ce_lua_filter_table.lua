-- Find the live hknpGroupCollisionFilter singleton(s) and read their collision
-- tables to locate a group that collides with nothing.
local out = {}
local function fmt(s, ...) out[#out+1] = string.format(s, ...) end
local function rq(p)
  if not p or p < 0x10000 then return nil end
  local ok, v = pcall(readQword, p)
  if ok and v ~= nil then return v end
  return nil
end
local function rd(p)
  local ok, v = pcall(readInteger, p, 4)
  if ok then return v end
  return nil
end

-- vtable pointers for the two group filter configs
local vtables = {0x145309EB0, 0x14530AF90, 0x14530AF20, 0x1453089D8}

local function scanFor(vt)
  -- scan writable committed memory for the 8-byte vtable pointer
  local pat = string.format("%02X %02X %02X %02X %02X %02X %02X %02X",
    vt & 0xFF, (vt>>8)&0xFF, (vt>>16)&0xFF, (vt>>24)&0xFF,
    (vt>>32)&0xFF, (vt>>40)&0xFF, (vt>>48)&0xFF, (vt>>56)&0xFF)
  local s = AOBScan(pat, "+W-C")
  if not s then s = AOBScan(pat) end
  local res = {}
  if s and s.Count > 0 then
    for i = 0, math.min(s.Count, 8) - 1 do res[#res+1] = tonumber(s.getString(i), 16) end
    s.destroy()
  end
  return res
end

for _, vt in ipairs(vtables) do
  fmt("scan vtable %X ...", vt)
  local hits = scanFor(vt)
  fmt("  %d hit(s)", #hits)
  for _, h in ipairs(hits) do
    fmt("    filter object candidate @ %X", h)
    -- read collision table at +0x24 (32 dwords)
    local rows = {}
    for g = 0, 31 do
      local v = rd(h + 0x24 + g * 4)
      rows[g] = v or 0
      if v == 0 then fmt("      group %d -> table row 0x%08X (collides with NOTHING)", g, v or 0) end
    end
    -- print a compact summary of the table
    local nonz = {}
    for g = 0, 31 do if rows[g] ~= 0 then nonz[#nonz+1] = string.format("%d:0x%08X", g, rows[g]) end end
    fmt("      non-zero rows: %s", table.concat(nonz, " "))
  end
end

return table.concat(out, "\n")
