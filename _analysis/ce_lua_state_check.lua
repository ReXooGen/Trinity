-- Quick state check: position and filter after noclip test.
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

local owner = _G.noclip_owner
if not owner or owner < 0x10000 then
  -- re-resolve
  local s = AOBScan("1A 00 00 01 00 00 00 80", "+W-C")
  if not s then s = AOBScan("1A 00 00 01 00 00 00 80") end
  if s and s.Count > 0 then
    for i = 0, s.Count - 1 do
      local hit = tonumber(s.getString(i), 16)
      if hit and rq(hit - 0x38 + 0x48) == 0x146A50FF0 then owner = hit - 0x38 break end
    end
    s.destroy()
  end
end

fmt("owner=%X", owner or 0)
if owner then
  fmt("pos=(%.2f, %.2f, %.2f)", rf(owner+0x90) or 0, rf(owner+0x94) or 0, rf(owner+0x98) or 0)
  fmt("filter=0x%08X", rd(owner+0x40) or 0)
end

return table.concat(out, "\n")
