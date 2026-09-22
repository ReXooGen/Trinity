# Decompile the Havok character-proxy move integrator: confirm the field contract
import re

d = mcp.call("bn_function_decompile", function="0x144282080")
if isinstance(d, dict):
    d = d.get("text") or str(d)
out("=== decompile length:", len(d), "===")
out(d[:14000])
