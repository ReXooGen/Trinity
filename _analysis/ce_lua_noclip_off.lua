-- No Clip test (OFF): stop holding the filter; restore group 27.
_G.noclip_enabled = false
if _G.noclip_owner and _G.noclip_owner > 0x10000 then
  writeBytes(_G.noclip_owner + 0x40, {0x1B, 0x40, 0x00, 0x00})
end
return "noclip OFF. collision filter restored to group 27 (0x401B)."
