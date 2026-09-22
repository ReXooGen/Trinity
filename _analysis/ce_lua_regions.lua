-- Enumerate a compact map of committed memory regions, so we can tell game heap
-- apart from the image and find the physics/character objects.
local out = {}
local function fmt(s, ...) out[#out+1] = string.format(s, ...) end

local regions = enumMemoryRegions()
local rows = {}
for i = 1, #regions do
  local r = regions[i]
  local base = r.BaseAddress or 0
  local size = r.RegionSize or 0
  local prot = r.Protect or 0
  local state = r.State or 0
  if state == 0x1000 and size >= 0x100000 and base >= 0x10000 then
    rows[#rows+1] = { base = base, size = size, prot = prot }
  end
end
table.sort(rows, function(a, b) return a.size > b.size end)
fmt("regions with size >= 1MB: %d", #rows)
for i = 1, math.min(#rows, 40) do
  local r = rows[i]
  fmt("  %012X - %012X  size=%8X prot=%X", r.base, r.base + r.size, r.size, r.prot)
end

return table.concat(out, "\n")
