import json

def xrefs(addr, n=40):
    return mcp.call("bn_data_xrefs_to", address=hex(addr), limit=n)

def mem(addr, n):
    return mcp.call("bn_memory_read", address=hex(addr), length=n)

print("=== xrefs to 'hknpDisableCollisionFilter' string 0x145308A90 ===")
print(xrefs(0x145308A90))

print("\n=== xrefs to 'hknpCollisionFilter::Type' 0x145302830 ===")
print(xrefs(0x145302830))

print("\n=== data @ 0x14533a2d0 (first xref of disable filter string) ===")
print(mem(0x14533a2b0, 0x80))

print("\n=== data @ 0x1468af1c8 (second xref) ===")
print(mem(0x1468af180, 0xa0))

# all the collision filter type strings and their neighbors (registration table)
for name, va in [
    ("hknpGroupCollisionFilter", 0x14530A050),
    ("hknpPairCollisionFilter", 0x1453083D0),
    ("hknpConstraintCollisionFilter", 0x145308718),
    ("hknpDisableCollisionFilter", 0x145308A90),
]:
    print(f"\n=== xrefs to {name} {hex(va)} ===")
    print(xrefs(va, 12))
