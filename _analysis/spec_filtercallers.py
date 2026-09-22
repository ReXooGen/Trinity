import json

print("=== callers of group isEnabled 0x144204080 ===")
r = mcp.call("bn_function_callers", function=hex(0x144204080), limit=20)
print(json.dumps(r)[:3000])

# also check the filter object: the isEnabled 'this' comes from a caller.
# Look at the group filter's own ctor/getType region - find the singleton.
print("\n=== xrefs to group vtable 0x145309eb0 ===")
print(mcp.call("bn_data_xrefs_to", address=hex(0x145309eb0), limit=20))
print("\n=== xrefs to group vtable 0x14530af20 ===")
print(mcp.call("bn_data_xrefs_to", address=hex(0x14530af20), limit=20))
