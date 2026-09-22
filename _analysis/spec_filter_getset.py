import json

def xrefs(addr, n=20):
    return mcp.call("bn_data_xrefs_to", address=hex(addr), limit=n)

print("=== xrefs to 'setBodyCollisionFilterInfo' 0x145bd9e58 ===")
print(xrefs(0x145bd9e58))
print("\n=== xrefs to 'getBodyCollisionFilterInfo' 0x145bda510 ===")
print(xrefs(0x145bda510))
print("\n=== xrefs to 'collisionFilterInfo' 0x145302aa0 ===")
print(xrefs(0x145302aa0))
