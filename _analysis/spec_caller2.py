import json

def dis(a, n=60):
    r = mcp.call("bn_function_disassembly", function=hex(a), limit=n)
    return r if isinstance(r, str) else json.dumps(r)

# Second caller of the integrator: sub_1436a37f0, call site 0x1436a494e
print("=== sub_1436a37f0 disasm (around the call at 0x1436a494e) ===")
print(dis(0x1436a37f0, 200))
