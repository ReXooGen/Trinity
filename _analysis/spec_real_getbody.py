import json

def dis(a, n=180):
    r = mcp.call("bn_function_disassembly", function=hex(a), limit=n)
    return r if isinstance(r, str) else json.dumps(r)

print("=== real getBody @ 0x1439ED740 ===")
print(dis(0x1439ED740, 180))

print("\n\n=== real getBodyPropertyImpl @ 0x144236570 ===")
print(dis(0x144236570, 120))
