import json

def dis(a, n=160):
    r = mcp.call("bn_function_disassembly", function=hex(a), limit=n)
    return r if isinstance(r, str) else json.dumps(r)

# property map lookup (called by getBodyPropertyImpl)
print("=== sub_1442b2780 (property map lookup) ===")
print(dis(0x1442b2780, 160))

# resolve j_sub_155dfbcc0 (consumer of the 0xF001 property value)
print("\n\n=== symbol at 0x155dfbcc0 ===")
r = mcp.call("bn_function_info", function=hex(0x155dfbcc0))
print(json.dumps(r)[:800])
print("\n=== j_sub_155dfbcc0 disasm ===")
print(dis(0x155dfbcc0, 120))
