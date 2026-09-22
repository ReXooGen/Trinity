import json

def show(t, r, n=8000):
    print(f"\n##### {t}")
    print((r if isinstance(r, str) else json.dumps(r))[:n])

# The QueryCollector vtable printed in the integrator disassembly.
show("symbol + bytes at 0x14552b0e0", mcp.call("bn_memory_read", address=hex(0x14552b0d0), length=0x60))
show("symbols at 0x14552b0e0", mcp.call("bn_symbol_list_at", address=hex(0x14552b0e0)))
show("data xrefs to 0x14552b0e0", mcp.call("bn_data_xrefs_to", address=hex(0x14552b0e0), limit=20))
