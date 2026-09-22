import json

def dis(a, n=120, off=0):
    return mcp.call("bn_function_disassembly", function=hex(a), limit=n, offset=off)

print("### sub_144283700 (body property read path)")
print(dis(0x144283700, 120))
print("\n\n### sub_144283f10")
print(dis(0x144283f10, 100))
