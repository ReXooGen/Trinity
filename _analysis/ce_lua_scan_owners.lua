-- Find ALL moveOwners via the static proxy-manager signature, then identify the
-- player by matching position/body id against the player char.
local out = {}
local function fmt(s, ...) out[#out+1] = string.format(s, ...) end
local function rq(p)
  if not p or p < 0x10000 or p > 0x7FFFFFFFFFFF then return nil end
  local ok, v = pcall(readQword, p)
  if ok and v ~= nil then return v end
  return nil
end
local function rd(p)
  local ok, v = pcall(readInteger, p, 4)
  if ok then return v end
  return nil
end
local function rf(p)
  local ok, v = pcall(readFloat, p)
  if ok then return v end
  return nil
end

local BASE = 0x140000000
local PROXYMGR = 0x146A50FF0

-- verify the proxy manager is still there
fmt("proxy mgr [0x146A50FF0] -> vtable %X", rq(PROXYMGR) or 0)

-- AOB scan for the proxy manager pointer (8-byte LE)
local pat = string.format("%02X %02X %02X %02X %02X %02X %02X %02X",
  PROXYMGR & 0xFF, (PROXYMGR>>8)&0xFF, (PROXYMGR>>16)&0xFF, (PROXYMGR>>24)&0xFF,
  (PROXYMGR>>32)&0xFF, (PROXYMGR>>40)&0xFF, (PROXYMGR>>48)&0xFF, (PROXYMGR>>56)&0xFF)
fmt("scan pattern: %s", pat)
local s = AOBScan(pat, "+W-C")
if not s then s = AOBScan(pat) end
if not s or s.Count == 0 then
  return table.concat(out, "\n") .. "\nNO MOVEOWNER SIGNATURE FOUND"
end

fmt("raw hits: %d", s.Count)
local owners = {}
local seen = {}
for i = 0, math.min(s.Count, 4000) - 1 do
  local a = tonumber(s.getString(i), 16)
  if a then
    local owner = a - 0x48   -- the hit is at [moveOwner+0x48], so moveOwner = hit - 0x48
    if not seen[owner] then
      seen[owner] = true
      local bid = rq(owner + 0x38)
      local x, y, z = rf(owner+0x90), rf(owner+0x94), rf(owner+0x98)
      local filt = rd(owner + 0x40)
      owners[#owners+1] = { owner = owner, bid = bid, x = x, y = y, z = z, filt = filt }
    end
  end
end
s.destroy()

fmt("unique moveOwner candidates: %d", #owners)
for _, o in ipairs(owners) do
  local bid = o.bid or 0
  -- mark player-like: filter 0x401B and position magnitude reasonable
  local marker = ""
  if o.filt == 0x401B and o.x and math.abs(o.x) < 10000 and o.y and o.y > 1 and o.z and math.abs(o.z) < 10000 then
    marker = "  <-- PLAYER-LIKE"
  end
  fmt("  owner=%X bid=%X filt=0x%08X pos=(%.1f,%.1f,%.1f)%s",
      o.owner, bid, o.filt or 0, o.x or 0, o.y or 0, o.z or 0, marker)
end

return table.concat(out, "\n")
