-- Enumerate modules (looking for Trinity.asi) and probe the char-manager walk.
local out = {}
local function fmt(s, ...) out[#out+1] = string.format(s, ...) end

local mods = enumModules and enumModules() or nil
if mods then
  for i = 1, #mods do
    local m = mods[i]
    local nm = m.Name or m.name or "?"
    if nm:lower():find("trinit") or nm:lower():find("asi") then
      fmt("MOD %s base=%X size=%X", nm, m.Address or m.BaseAddress or 0, m.Size or 0)
    end
  end
  fmt("total modules=%d", #mods)
  -- also print first 3 non-system
  local shown = 0
  for i = 1, #mods do
    local m = mods[i]
    local nm = m.Name or m.name or "?"
    if not nm:lower():find("dll") and not nm:lower():find("exe") then
      fmt("  odd module: %s base=%X", nm, m.Address or 0)
      shown = shown + 1
      if shown > 10 then break end
    end
  end
end

return table.concat(out, "\n")
