-- No Clip test (ON): hold collision filter = group 22 ONLY on the player's
-- moveOwner, via a CE timer. Safe: never touches NPCs.
local function rq(p)
  if not p or p < 0x10000 or p > 0x7FFFFFFFFFFF then return nil end
  local ok, v = pcall(readQword, p)
  if ok and v ~= nil then return v end
  return nil
end

local PROXYMGR = 0x146A50FF0
local BODYID_PAT = "1A 00 00 01 00 00 00 80"   -- player body id 0x800000000100001A

local function resolve()
  local s = AOBScan(BODYID_PAT, "+W-C")
  if not s then s = AOBScan(BODYID_PAT) end
  if not s or s.Count == 0 then return nil end
  local owner = nil
  for i = 0, s.Count - 1 do
    local hit = tonumber(s.getString(i), 16)
    if hit then
      local c = hit - 0x38                    -- hit is at [moveOwner+0x38]
      if rq(c + 0x48) == PROXYMGR then owner = c break end
    end
  end
  if s.destroy then s.destroy() end
  return owner
end

local owner = resolve()
if not owner then return "noclip: player moveOwner not found" end

_G.noclip_owner = owner
_G.noclip_enabled = true
_G.noclip_orig_filter = 0x401B

local t = createTimer(nil, false)
t.Interval = 16
t.OnTimer = function()
  if not _G.noclip_enabled then
    local o = _G.noclip_owner
    if o and o > 0x10000 then
      writeBytes(o + 0x40, {0x1B, 0x40, 0x00, 0x00})   -- restore group 27
    end
    t.Enabled = false
    if t.destroy then t.destroy() end
    return
  end
  local o = _G.noclip_owner
  if o and o > 0x10000 then
    writeBytes(o + 0x40, {0x16, 0x00, 0x00, 0x00})     -- group 22 = no collision
  end
end
t.Enabled = true
_G.noclip_timer = t

return string.format("noclip ON. owner=%X. Test thick walls/textures now. To stop: run noclip_off script.", owner)
