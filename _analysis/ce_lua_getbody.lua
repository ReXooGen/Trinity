-- Resolve the live local-player physics body via the manager's own getBody
-- (vtable +0x80), then try the collision-filter getter slots (+0xD8/+0xF0/+0xF8)
-- and dump the body so the filter field is identifiable.
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

local MGR = 0x146A50FF0
local HANDLE = 0x800000000100001A

local function call1(fn, a1, a2, a3, a4)
  local ok, r = pcall(function()
    return executeCodeEx(1, fn, a1, a2, a3, a4)  -- cdecl-like; CE handles
  end)
  return ok, r
end

-- CE's executeCodeEx(callmethod, address, ...) where 0=stdcall? Try fastcall-ish
-- through CE's documented helper: executeCodeEx(0=stdcall,1=cdecl,2=thiscall,3=fastcall)
local function callFast(fn, a1, a2, a3, a4)
  local ok, r = pcall(executeCodeEx, 3, fn, a1, a2, a3, a4)
  if ok then return true, r end
  return false, nil
end
local function callStd(fn, a1, a2, a3, a4)
  local ok, r = pcall(executeCodeEx, 0, fn, a1, a2, a3, a4)
  if ok then return true, r end
  return false, nil
end
local function callCdecl(fn, a1, a2, a3, a4)
  local ok, r = pcall(executeCodeEx, 1, fn, a1, a2, a3, a4)
  if ok then return true, r end
  return false, nil
end

fmt("MGR=%X HANDLE=%X", MGR, HANDLE)

local getBody = 0x14324C200
fmt("calling getBody(%X, %X) as fastcall...", MGR, HANDLE)
local ok, r = callFast(getBody, MGR, HANDLE, 0, 0)
fmt("  fastcall ok=%s ret=%s", tostring(ok), tostring(r))
if not ok or not r or r == 0 then
  ok, r = callStd(getBody, MGR, HANDLE, 0, 0)
  fmt("  stdcall ok=%s ret=%s", tostring(ok), tostring(r))
end

local body = tonumber(tostring(r)) or 0
if body > 0x10000 then
  fmt("BODY = %X", body)
  local vt = rq(body)
  fmt("body vtable = %X", vt or 0)
  fmt("body dump:")
  for off = 0, 0x120, 8 do
    local q = rq(body + off) or 0
    local a, b = rf(body + off), rf(body + off + 4)
    local xs = ""
    if a and b then xs = string.format(" f=(%.3f,%.3f)", a, b) end
    fmt("  +%03X: %016X%s", off, q, xs)
  end
end

return table.concat(out, "\n")
