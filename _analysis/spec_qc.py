import json

VT = 0x14552b078

print("=== symbols at vtable ===")
print(mcp.call("bn_symbol_list_at", address=hex(VT)))
print("\n=== data xrefs to QueryCollector vtable ===")
print(mcp.call("bn_data_xrefs_to", address=hex(VT), limit=50))
print("\n=== bytes around vtable (raw) ===")
print(mcp.call("bn_memory_read", address=hex(VT - 0x20), length=0x80))
