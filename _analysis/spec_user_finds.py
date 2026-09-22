import json

def dis(a, n=80):
    r = mcp.call("bn_function_disassembly", function=hex(a), limit=n)
    return r if isinstance(r, str) else json.dumps(r)

def info(a):
    r = mcp.call("bn_function_info", function=hex(a))
    return r if isinstance(r, str) else json.dumps(r)

# 1) the wall-pass branch
print("=== info for function containing 0x140A00F6E ===")
print(info(0x140A00F6E))
print("\n=== disasm 0x140A00F40 (context of jne) ===")
print(dis(0x140A00F40, 60))

# 2) freeze instruction
print("\n\n=== info for 0x1436A9AE6 ===")
print(info(0x1436A9AE6))

# 3) NPC jae
print("\n\n=== info for 0x14087E5D5 ===")
print(info(0x14087E5D5))
