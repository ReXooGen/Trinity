import json

print("=== callers of 0x140A00EC0 ===")
r = mcp.call("bn_function_callers", function=hex(0x140A00EC0), limit=30)
print(json.dumps(r)[:3000])

print("\n=== function info 0x140A00EC0 ===")
r2 = mcp.call("bn_function_info", function=hex(0x140A00EC0))
print(json.dumps(r2)[:1500])
