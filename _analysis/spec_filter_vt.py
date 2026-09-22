import json, struct

def mem(a, n):
    return mcp.call("bn_memory_read", address=hex(a), length=n)

def slots(a):
    raw = mem(a, 104)
    b = bytes.fromhex(raw["hex"])
    return [struct.unpack("<Q", b[i:i+8])[0] for i in range(0, 104, 8)]

print("=== hknpDisableCollisionFilter vtable 0x1453089d8 ===")
for i, v in enumerate(slots(0x1453089d8)):
    print(f"  [{i:2d}] 0x{v:016X}")

print("\n=== hknpGroupCollisionFilter vtable 0x145309eb0 ===")
for i, v in enumerate(slots(0x145309eb0)):
    print(f"  [{i:2d}] 0x{v:016X}")

print("\n=== xrefs to disable vtable 0x1453089d8 ===")
print(mcp.call("bn_data_xrefs_to", address=hex(0x1453089d8), limit=20))
