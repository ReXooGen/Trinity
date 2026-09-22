import json

def dis(a, n=220):
    r = mcp.call("bn_function_disassembly", function=hex(a), limit=n)
    return r if isinstance(r, str) else json.dumps(r)

print("=== real castShape @ 0x1442B0BF0 (first 220) ===")
print(dis(0x1442B0BF0, 220))

print("\n\n=== callees of castShape ===")
r = mcp.call("bn_function_callees", function=hex(0x1442B0BF0))
print(json.dumps(r)[:4000])
