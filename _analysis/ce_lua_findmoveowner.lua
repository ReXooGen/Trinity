-- Locate the local player's moveOwner via Trinity's live player-resolve contract,
-- then dump the moveOwner structure (character-proxy layout).
local BASE = 0x140000000

local out = {}
local function fmt(s, ...) out[#out+1] = string.format(s, ...) end

local function rq(p)
  if not p or p < 0x10000 or p > 0x7FFFFFFFFFFF then return nil end
  local ok, v = pcall(readQword, p)
  if ok and v and v ~= 0 then return v end
  return nil
end
local function rq0(p)
  if not p then return nil end
  local ok, v = pcall(readQword, p)
  if ok then return v end
  return nil
end
local function rd(p, t)
  if not p then return nil end
  local ok, v = pcall(readInteger, p, t or vtDword)
  if ok then return v end
  return nil
end
local function rw(p)
  if not p then return nil end
  local ok, v = pcall(readInteger, p, vtWord)
  if ok then return v end
  return nil
end
local function rb(p)
  if not p then return nil end
  local ok, v = pcall(readBytes, p, 1, true)
  if ok and v then return v[1] end
  return nil
end
local function rf(p)
  if not p then return nil end
  local ok, v = pcall(readFloat, p)
  if ok then return v end
  return nil
end

local function aobFind(pat)
  local s = AOBScan(pat, "+W-C")
  if not s then s = AOBScan(pat) end
  if not s or s.Count == 0 then return nil, 0 end
  local n = s.Count
  local a = tonumber(s.getString(0), 16)
  if s.destroy then s.destroy() end
  return a, n
end

local integ, nint = aobFind("48 8B C4 4C 89 48 ?? 48 89 50 ?? 55 41 56")
fmt("AOB integrator: %X (matches=%d)  expect=%X", integ or 0, nint, 0x144282080)
if integ then
  BASE = integ - 0x4282080
  fmt("derived module base = %X", BASE)
end

local G_CharMgr = BASE + 0x61830F8
local mgr = rq(G_CharMgr)
fmt("charmgr global @%X -> %X", G_CharMgr, mgr or 0)

if mgr then
  local data = rq(mgr + 0xB8)
  local count = rd(mgr + 0xC0)
  fmt("char list data=%X count=%d", data or 0, count or -1)
  if data and count and count > 0 and count < 8192 then
    local matches = {}
    for i = 0, count - 1 do
      local ch = rq(data + i * 8)
      if ch then
        local tag = rb(ch + 0x88 + 1)
        local poss = rq(ch + 0xA0)
        local back = nil
        if poss then back = rq(poss + 0xD0) end
        local objType = rd(ch + 0x48)
        if tag and (tag == 1 or tag == 9) and back == ch then
          matches[#matches+1] = ch
          fmt("  MATCH i=%d ch=%X tag=%d objType=%s", i, ch, tag, tostring(objType))
        end
      end
    end
    if #matches == 0 then
      fmt("  no possessor round-trip match; listing first 6 chars' facts")
      for i = 0, math.min(count, 6) - 1 do
        local ch = rq(data + i * 8)
        if ch then
          local poss = rq(ch + 0xA0)
          fmt("   i=%d ch=%X tag=%s obj=%s poss=%X back=%X", i, ch,
              tostring(rb(ch + 0x89)), tostring(rd(ch + 0x48)),
              poss or 0, (poss and rq(poss + 0xD0)) or 0)
        end
      end
    end
    -- dump the first match's movement component
    local ch = matches[1]
    if ch then
      -- the movement component: try the offsets Trinity uses
      for _, off in ipairs({0x298, 0x2B8, 0x2C0}) do
        local comp = rq(ch + off)
        fmt("ch+%X = %X", off, comp or 0)
        if comp then
          fmt("   pos=(%.2f, %.2f, %.2f)", rf(comp+0x90) or 0, rf(comp+0x94) or 0, rf(comp+0x98) or 0)
        end
      end
    end
  end
end

return table.concat(out, "\n")
