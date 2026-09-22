import json

def dis(a, n=40):
    r = mcp.call("bn_function_disassembly", function=hex(a), limit=n)
    return r if isinstance(r, str) else json.dumps(r)

for label, a in [
    ("disable [3] 0x1403600B0", 0x1403600B0),
    ("disable [4] 0x140359BA0", 0x140359BA0),
    ("group   [3] 0x144204080", 0x144204080),
    ("group   [4] 0x1442045D0", 0x1442045D0),
    ("shared  [2] 0x144123080", 0x144123080),
    ("shared  [8] 0x1441B240", 0x1441B240),
]:
    print(f"\n=== {label} ===")
    print(dis(a, 40))
