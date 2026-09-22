import json

def mem(a, n):
    return mcp.call("bn_memory_read", address=hex(a), length=n)

# larger window before the branch to find function prologue and where bl is set
print("=== 0x140A00E00 (0x1A0 bytes, before the branch) ===")
print(mem(0x140A00E00, 0x1A0))
