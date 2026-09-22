-- Read-only verification: what is the QueryCollector vtable's first slot, and
-- disassemble it. No writes, no calls.
local out = {}
local function fmt(s, ...) out[#out+1] = string.format(s, ...) end
local function rq(p)
  local ok, v = pcall(readQword, p)
  if ok then return v end
  return nil
end
local function rb(p, n)
  local ok, v = pcall(readBytes, p, n, true)
  if ok and v then return v end
  return nil
end

local base = 0x14500b0d0
-- search for the QueryCollector vtable near the address BN reported LIVE
fmt("scanning .rdata for the QueryCollector vtable (BN VA 0x14552b0e0)...")
for _, a in ipairs({0x14552b0e0, 0x14552b0e0 - 0x6C0000, 0x14552b0e0 + 0x6C0000}) do
  fmt("candidate @%X: %s", a, (function()
    local b = rb(a, 8)
    if not b then return "no-read" end
    local s = {}
    for i = 1, 8 do s[#s+1] = string.format("%02X", b[i]) end
    return table.concat(s, " ")
  end)())
end

return table.concat(out, "\n")
