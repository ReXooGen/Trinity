-- Robust scan of Trinity.asi for RIP-relative memory instructions, to identify
-- the module's writable globals (candidate g_playerMoveOwner).
local out = {}
local function fmt(s, ...) out[#out+1] = string.format(s, ...) end

local MOD = getAddressSafe("Trinity.asi")
local MOD_SIZE = 2224128
fmt("base=%X", MOD or 0)
if not MOD then return "no module" end

local function rb(p, n)
  local ok, v = pcall(readBytes, p, n, true)
  if ok and v then return v end
  return nil
end
local function rq(p)
  local ok, v = pcall(readQword, p)
  if ok then return v end
  return nil
end

local targets = {}
local addr = MOD
local enda = MOD + MOD_SIZE
while addr < enda do
  local buf = rb(addr, 0x1000)
  if buf then
    for i = 1, #buf - 7 do
      local b1 = buf[i]
      -- REX.W (48-4F) + opcode (89 store / 8B load / 8D lea / 3B cmp ...)
      if b1 >= 0x48 and b1 <= 0x4F then
        local op = buf[i+1]
        if op == 0x89 or op == 0x8B or op == 0x8D or op == 0x3B or op == 0x39 then
          local modrm = buf[i+2]
          if modrm and math.floor(modrm / 64) == 0 and (modrm % 8) == 5 then
            local d = buf[i+3] + buf[i+4]*256 + buf[i+5]*65536 + buf[i+6]*16777216
            if d >= 0x80000000 then d = d - 0x100000000 end
            local instrAddr = addr + i - 1
            local tgt = instrAddr + 7 + d
            if tgt >= MOD + 0xFA000 and tgt < MOD + MOD_SIZE then
              local key = tgt
              local e = targets[key]
              if not e then
                e = { tgt = tgt, stores = 0, loads = 0, leas = 0, sites = {} }
                targets[key] = e
              end
              if op == 0x89 then e.stores = e.stores + 1 end
              if op == 0x8B then e.loads = e.loads + 1 end
              if op == 0x8D then e.leas = e.leas + 1 end
              if #e.sites < 3 then e.sites[#e.sites+1] = string.format("%X/%02X", instrAddr, op) end
            end
          end
        end
      end
    end
    addr = addr + 0x1000 - 8
  else
    addr = addr + 0x1000
  end
end

local list = {}
for _, e in pairs(targets) do list[#list+1] = e end
table.sort(list, function(a, b) return (a.stores + a.loads) > (b.stores + b.loads) end)
fmt("distinct data targets: %d", #list)
for i = 1, math.min(#list, 40) do
  local e = list[i]
  fmt("  tgt=%X  st=%d ld=%d lea=%d  val=%X  sites=%s",
      e.tgt, e.stores, e.loads, e.leas, rq(e.tgt) or 0, table.concat(e.sites, ","))
end

return table.concat(out, "\n")
