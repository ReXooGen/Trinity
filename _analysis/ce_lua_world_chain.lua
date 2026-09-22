-- Follow the Havok world singleton to resolve the REAL vtable method addresses
-- (getBody +0x80, getBodyPropertyImpl +0xF0, castShape +0x1F0). Pure reads.
local out = {}
local function fmt(s, ...) out[#out+1] = string.format(s, ...) end
local function rq(p)
  if not p or p < 0x10000 or p > 0x7FFFFFFFFFFF then return nil end
  local ok, v = pcall(readQword, p)
  if ok and v and v ~= 0 then return v end
  return nil
end

local WORLD = 0x146c8d0f8
fmt("world singleton @%X = %X", WORLD, rq(WORLD) or 0)

for _, off in ipairs({0x60, 0x78}) do
  local mgr = rq(rq(WORLD) + off)
  fmt("world+%X -> %X", off, mgr or 0)
  if mgr then
    local sub = mgr + 0x20
    local vt = rq(sub)
    fmt("   sub+0x20=%X  vtable=%X", sub, vt or 0)
    if vt then
      local getBody = rq(vt + 0x80)
      local getProp = rq(vt + 0xF0)
      local cast = rq(vt + 0x1F0)
      fmt("   real getBody(+0x80)     = %X", getBody or 0)
      fmt("   real getBodyProp(+0xF0) = %X", getProp or 0)
      fmt("   real castShape(+0x1F0)  = %X", cast or 0)
    end
  end
end

return table.concat(out, "\n")
