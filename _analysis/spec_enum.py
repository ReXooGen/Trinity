import json

def mem(a, n):
    return mcp.call("bn_memory_read", address=hex(a), length=n)

# reflected enum region for hknpCollisionFilter::Type (xref target 0x1468ad890)
print("=== enum region @ 0x1468ad840 ===")
print(mem(0x1468ad840, 0x120))

print("\n=== disable filter type descriptor region @ 0x1468af170 ===")
print(mem(0x1468af170, 0xc0))

print("\n=== group filter xref target 0x1468b04e8 region ===")
print(mem(0x1468b04a0, 0xa0))

# search BN types for the filter classes
print("\n=== bn type search 'DisableCollisionFilter' ===")
print(mcp.call("bn_type_search", query="DisableCollisionFilter"))
print("\n=== bn type search 'CollisionFilter' ===")
print(mcp.call("bn_type_search", query="CollisionFilter"))
