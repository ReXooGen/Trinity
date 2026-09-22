import json

# Map the full vtable of the character-proxy component and disassemble the key
# slots: getBody (+0x80), and the +0xF0/+0xF8/+0x100 filter candidates.
VT = 0x145BDA8D0

def dis(a, n=140):
    r = mcp.call("bn_function_disassembly", function=hex(a), limit=n)
    return r if isinstance(r, str) else json.dumps(r)

# 1) dump the whole vtable 0x000..0x200
print("=== vtable 0x145BDA8D0 (0x200 bytes) ===")
print(mcp.call("bn_memory_read", address=hex(VT), length=0x200))

# 2) disassemble getBody
print("\n\n=== getBody @ 0x14324C200 ===")
print(dis(0x14324C200))

# 3) filter candidates
for off, a in [(0xF0, 0x14324EE20), (0xF8, 0x14324F160), (0x100, 0x14324F490),
               (0xD8, 0x14324E4C0), (0xE0, 0x14324E7D0), (0xE8, 0x14324EAE0)]:
    print(f"\n\n=== slot +{off:03X} @ {a:X} ===")
    print(dis(a))
