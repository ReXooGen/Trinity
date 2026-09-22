import json, re

def dis(addr, limit=400, offset=0):
    return mcp.call("bn_function_disassembly", function=hex(addr), limit=limit, offset=offset)

text = dis(0x144282080, limit=400, offset=200)
# strip the YAML header noise
print(text)
