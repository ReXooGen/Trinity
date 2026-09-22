import json

def dis(a, n=80):
    r = mcp.call("bn_function_disassembly", function=hex(a), limit=n)
    return r if isinstance(r, str) else json.dumps(r)

# 0x14027E0E0: the function whose return value becomes 'bl' (the branch flag)
print("=== 0x14027E0E0 (returns bl) ===")
print(dis(0x14027E0E0, 80))

print("\n\n=== 0x14027E4F0 (the 'skip' path call) ===")
print(dis(0x14027E4F0, 60))

print("\n\n=== 0x1409F9BC1 (collision path call) ===")
print(dis(0x1409F9BC1, 40))

print("\n\n=== 0x1409FA3FF (skip path call) ===")
print(dis(0x1409FA3FF, 40))
