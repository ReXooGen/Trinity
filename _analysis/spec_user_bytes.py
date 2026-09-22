import json

def mem(a, n):
    return mcp.call("bn_memory_read", address=hex(a), length=n)

# Read raw bytes around the three user-found addresses
for label, a, n in [
    ("wall-pass jne", 0x140A00F6E - 0x20, 0x60),
    ("freeze", 0x1436A9AE6 - 0x20, 0x60),
    ("npc jae", 0x14087E5D5 - 0x20, 0x60),
]:
    print(f"\n=== {label} @ {hex(a)} ===")
    print(mem(a, n))
