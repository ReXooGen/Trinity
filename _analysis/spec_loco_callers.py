import json

print("=== callers of loco stepper 0x14369FF60 ===")
r = mcp.call("bn_function_callers", function=hex(0x14369FF60), limit=20)
print(json.dumps(r)[:2500])

print("\n=== callers of integrator 0x144282080 ===")
r2 = mcp.call("bn_function_callers", function=hex(0x144282080), limit=20)
print(json.dumps(r2)[:2500])
