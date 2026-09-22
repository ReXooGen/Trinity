import json

def dis(a, n=200):
    r = mcp.call("bn_function_disassembly", function=hex(a), limit=n)
    return r if isinstance(r, str) else json.dumps(r)

print("=== full group filter isEnabled 0x144204080 ===")
print(dis(0x144204080, 200))
print("\n\n=== full 0x1442045D0 ===")
print(dis(0x1442045D0, 100))
