-- Resolve the player's moveOwner via the char manager (no Trinity needed).
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
local function rb(p, n)
  local ok, v = pcall(readBytes, p, n, true)
  if ok and v then return v end
  return nil
end
local function rf(p)
  local ok, v = pcall(readFloat, p)
  if ok then return v end
  return nil
end

local BASE = 0x140000000

-- 1) char manager global via anchor A
local function aobFind(pat)
  local s = AOBScan(pat, "+X")
  if not s then s = AOBScan(pat) end
  if not s or s.Count == 0 then return nil, 0 end
  local n = s.Count
  local a = tonumber(s.getString(0), 16)
  if s.destroy then s.destroy() end
  return a, n
end

local anchor, n = aobFind("4D 8B 00 49 C1 E8 20 48 8D 54 24 78 48 8B 0D ?? ?? ?? ?? 48 8B 09 E8")
fmt("anchorA: %X (%d match)", anchor or 0, n)
if not anchor then return table.concat(out, "\n") .. "\nNO ANCHOR" end

local movAt = anchor + 0x0C
local rip = rd(movAt + 3)
local glob = movAt + 7 + rip
local slot = rq(glob)          -- *glob
local mgr = slot and rq(slot) or nil   -- *(*glob)
fmt("global=%X slot=%X mgr=%X", glob, slot or 0, mgr or 0)

if not mgr then return table.concat(out, "\n") .. "\nNO MGR" end

local data = rq(mgr + 0xB8)
local count = rd(mgr + 0xC0)
fmt("charlist data=%X count=%d", data or 0, count or -1)

if not data or not count or count <= 0 or count > 8192 then
  return table.concat(out, "\n") .. "\nBAD CHARLIST"
end

-- 2) find player char: tag (ch+0x88+1) in {1,9} AND possessor round-trip
local player = nil
local playerPoss = nil
for i = 0, count - 1 do
  local ch = rq(data + i * 8)
  if ch then
    local td = rq(ch + 0x88)
    local tag = td and rb(td + 1, 1) and rb(td + 1, 1)[1] or 0
    local poss = rq(ch + 0xA0)
    local back = poss and rq(poss + 0xD0) or nil
    if (tag == 1 or tag == 9) and poss and back == ch then
      player = ch
      playerPoss = poss
      fmt("player char @%X  tag=%d poss=%X", ch, tag, poss or 0)
      break
    end
  end
end

if not player then
  fmt("no possessor round-trip match; listing first 8 chars")
  for i = 0, math.min(count, 8) - 1 do
    local ch = rq(data + i * 8)
    if ch then
      local td = rq(ch + 0x88)
      local tag = td and rb(td + 1, 1) and rb(td + 1, 1)[1] or 0
      local poss = rq(ch + 0xA0)
      local back = poss and rq(poss + 0xD0) or nil
      fmt("  i=%d ch=%X tag=%d poss=%X back=%X roundtrip=%s", i, ch, tag, poss or 0, back or 0, tostring(back == ch))
    end
  end
  return table.concat(out, "\n")
end

-- 3) scan the player char for a pointer to the moveOwner
--    moveOwner signature: heap object with [obj+0x48] == 0x146A50FF0
local PROXYMGR = 0x146A50FF0
local function isMoveOwner(p)
  if not p or p < 0x10000 or p > 0x7FFFFFFFFFFF then return false end
  if p >= BASE and p <= BASE + 0x17FCD000 then return false end
  return rq(p + 0x48) == PROXYMGR
end

fmt("scanning player char for moveOwner pointers...")
local found = {}
for off = 0x00, 0x800, 8 do
  local p = rq(player + off)
  if isMoveOwner(p) then
    found[#found+1] = { type = "direct moveOwner", charOff = off, p = p }
  elseif p and p > 0x10000 and p < 0x7FFFFFFFFFFF then
    -- maybe a component: check [p+0x2C0]
    local mo = rq(p + 0x2C0)
    if isMoveOwner(mo) then
      found[#found+1] = { type = "component->moveOwner", charOff = off, p = p, mo = mo }
    end
  end
end

fmt("found %d moveOwner link(s):", #found)
for _, f in ipairs(found) do
  local mo = f.mo or f.p
  fmt("  [%s] char+%X -> %X  pos=(%.2f,%.2f,%.2f) filter=0x%08X",
      f.type, f.charOff, mo,
      rf(mo+0x90) or 0, rf(mo+0x94) or 0, rf(mo+0x98) or 0,
      rd(mo+0x40) or 0)
end

return table.concat(out, "\n")
