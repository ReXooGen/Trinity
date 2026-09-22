import json

print("=== integrator disasm (first 120) ===")
print(mcp.call("bn_function_disassembly", function="0x144282080", limit=120))
print("\n=== integrator callees ===")
print(mcp.call("bn_function_callees", function="0x144282080"))
print("\n=== integrator info ===")
print(mcp.call("bn_function_info", function="0x144282080"))
