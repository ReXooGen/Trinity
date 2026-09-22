import json

def dis(a, n=120):
    r = mcp.call("bn_function_disassembly", function=hex(a), limit=n)
    return r if isinstance(r, str) else json.dumps(r)

print(dis(0x140A00EC0, 120))
