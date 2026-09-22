-- From the player char, find the movement component and moveOwner.
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
local function rb(p, n)
  local ok, v = pcall(readBytes, p, n, true)
  if ok and v then return v end
  return nil
end

local BASE = 0x140000000
local function isHeap(p)
  return p and p > 0x10000 and p < 0x7FFFFFFFFFFF and not (p >= BASE and p <= BASE + 0x17FCD000)
end

-- find player char (reuse char manager)
local anchor, n = AOBScan("4D 8B 00 49 C1 E8 20 48 8D 54 24 78 48 8B 0D ?? ?? ?? ?? 48 8B 09 E8", "+X")
if not anchor or anchor.Count == 0 then return "no anchor" end
local movAt = tonumber(anchor.getString(0), 16) + 0x0C
local rip = rd(movAt + 3)
local glob = movAt + 7 + rip
local slot = rq(glob)
local mgr = slot and rq(slot) or nil
anchor.destroy()
if not mgr then return "no mgr" end
local data = rq(mgr + 0xB8)
local count = rd(mgr + 0xC0)
local player = nil
for i = 0, count - 1 do
  local ch = rq(data + i * 8)
  if ch then
    local td = rq(ch + 0x88)
    local tag = td and rb(td + 1, 1) and rb(td + 1, 1)[1] or 0
    local poss = rq(ch + 0xA0)
    local back = poss and rq(poss + 0xD0) or nil
    if (tag == 1 or tag == 9) and poss and back == ch then player = ch break end
  end
end
if not player then return "no player" end
fmt("player=%X", player)

local actor = rq(player + 0x68)
fmt("actor=%X", actor or 0)

-- scan actor fields for: (a) moveOwner, (b) component ([obj+0x2C0]=moveOwner)
local PROXYMGR = 0x146A50FF0
local function moveOwnerAt(p)
  if not isHeap(p) then return nil end
  if rq(p + 0x48) == PROXYMGR then return p end
  return nil
end

-- Also scan a wide range of the player char itself (deeper than before)
fmt("scanning player char +0x0..0x2000 for component/moveOwner...")
local hits = {}
for off = 0x00, 0x2000, 8 do
  local p = rq(player + off)
  if p then
    if moveOwnerAt(p) then
      hits[#hits+1] = string.format("char+%X -> moveOwner %X pos=(%.1f,%.1f,%.1f)", off, p, rf(p+0x90), rf(p+0x94), rf(p+0x98))
    elseif isHeap(p) then
      local mo = rq(p + 0x2C0)
      if moveOwnerAt(mo) then
        hits[#hits+1] = string.format("char+%X -> comp %X -> moveOwner %X", off, p, mo)
      end
    end
  end
end

-- also scan the actor
if actor then
  fmt("scanning actor +0x0..0x1000...")
  for off = 0x00, 0x1000, 8 do
    local p = rq(actor + off)
    if p then
      if moveOwnerAt(p) then
        hits[#hits+1] = string.format("actor+%X -> moveOwner %X pos=(%.1f,%.1f,%.1f)", off, p, rf(p+0x90), rf(p+0x94), rf(p+0x98))
      elseif isHeap(p) then
        local mo = rq(p + 0x2C0)
        if moveOwnerAt(mo) then
          hits[#hits+1] = string.format("actor+%X -> comp %X -> moveOwner %X", off, p, mo)
        end
      end
    end
  end
end

fmt("hits: %d", #hits)
for _, h in ipairs(hits) do fmt("  %s", h) end
if #hits == 0 then
  fmt("no direct link found. dumping player char header:")
  for off = 0x00, 0x100, 8 do
    local q = rq(player + off) or 0
    local tag = ""
    if isHeap(q) then tag = " PTR" elseif q >= BASE and q <= BASE+0x17FCD000 then tag = " IMG" end
    fmt("  +%03X: %016X%s", off, q, tag)
  end
end

return table.concat(out, "\n")
