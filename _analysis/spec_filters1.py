import json, struct

def dump(addr, n):
    r = mcp.call("bn_memory_read", address=hex(addr), length=n)
    if isinstance(r, str):
        return r
    return json.dumps(r)

for label, addr, n in [
    ("hknpDisableCollisionFilter str + 0x80", 0x145308a90, 0x100),
    ("RTTI .?AVhknpDisableCollisionFilter@@ -0x40", 0x146a8f820, 0x80),
    ("data_14533a2d0 (xref ptr)", 0x14533a2c0, 0x60),
    ("hknpCharacterProxyInternals::QueryCollector RTTI", 0x146a926a0, 0x80),
]:
    print(f"\n===== {label} @ {hex(addr)} =====")
    print(dump(addr, n))
