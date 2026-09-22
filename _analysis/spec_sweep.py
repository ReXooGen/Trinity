import json, struct

def dis(a, n=160, off=0):
    return mcp.call("bn_function_disassembly", function=hex(a), limit=n, offset=off)

# 1) decode the proxy vtable slots
raw = mcp.call("bn_memory_read", address=hex(0x145BDA8D0), length=0x200)
hexs = raw["hex"]
b = bytes.fromhex(hexs)
print("=== proxy vtable slots ===")
for i in range(0, len(b), 8):
    v = struct.unpack("<Q", b[i:i+8])[0]
    print(f"  +{i:03X}: 0x{v:016X}")

# 2) sweep
print("\n\n=== SWEEP proxy.vtable+0x1F0 @ 0x1432551D0 ===")
print(dis(0x1432551D0, 200))

# 3) find QueryCollector vtable symbol
print("\n\n=== symbols matching QueryCollector ===")
r = mcp.call("bn_symbol_list", query="QueryCollector")
print(json.dumps(r)[:3000])
