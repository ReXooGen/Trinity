-- Post-test sanity check: game still alive, character back on ground.
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
local function rf(p)
  local ok, v = pcall(readFloat, p)
  if ok then return v end
  return nil
end

-- owner is likely still 0x279C658EB00 (heap is stable); re-verify by scanning the
-- known Trinity global via the module (reuse a light scan).
local MOD = getAddressSafe("Trinity.asi")
if not MOD then return "no module" end

local function rb(p, n)
  local ok, v = pcall(readBytes, p, n, true)
  if ok and v then return v end
  return nil
end

local MOD_SIZE = 2224128
local owner = nil
local addr = MOD
while addr < MOD + MOD_SIZE and not owner do
  local b = rb(addr, 0x1000)
  if b then
    for i = 1, #b - 7 do
      if b[i] >= 0x48 and b[i] <= 0x4F and b[i+1] == 0x8B then
        local modrm = b[i+2]
        if modrm and math.floor(modrm/64) == 0 and (modrm % 8) == 5 then
          local d = b[i+3] + b[i+4]*256 + b[i+5]*65536 + b[i+6]*16777216
          if d >= 0x80000000 then d = d - 0x100000000 end
          local ia = addr + i - 1
          local tgt = ia + 7 + d
          if tgt >= MOD + 0xFA000 and tgt < MOD + MOD_SIZE then
            local v = rq(tgt)
            if v and v > 0x10000 and not (v >= 0x140000000 and v <= 0x157FCE020) then
              local vt = rq(v)
              if vt and vt >= 0x140000000 and vt <= 0x157FCE020 then
                local bidb = rb(v + 0x38, 8)
                local top = bidb and bidb[8] or 0
                if (top & 0x80) ~= 0 then
                  local x, y, z = rf(v+0x90), rf(v+0x94), rf(v+0x98)
                  if x and y and z and math.abs(x) > 5 and math.abs(y) > 5 and math.abs(z) > 5 then
                    owner = v
                  end
                end
              end
            end
          end
        end
      end
    end
  end
  addr = addr + 0x1000
end

if not owner then return "owner not found" end
fmt("owner=%X", owner)
fmt("position=(%.2f, %.2f, %.2f)", rf(owner+0x90), rf(owner+0x94), rf(owner+0x98))
fmt("proxy filter (owner+0x40) = 0x%08X", rd(owner+0x40) or 0)

return table.concat(out, "\n")
