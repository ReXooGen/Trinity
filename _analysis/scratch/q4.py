# Locate the Havok hkClass tables for the character proxy / collision filters
import json

TARGETS = {
    "hknpCharacterProxy": 0x145cbd858,
    "hknpCharacterProxyCinfo": 0x1452f8ab8,
    "hknpDisableCollisionFilter": 0x145308a90,
    "HK_CHARACTER_FLYING": 0x145303d60,
    "collisionFilterInfo": 0x145302aa0,
    "hknpGroupCollisionFilter": 0x14530a050,
}

for name, addr in TARGETS.items():
    out(f"\n=== xrefs to {name} @ {hex(addr)} ===")
    r = mcp.call("bn_data_xrefs_to", address=hex(addr))
    if isinstance(r, str):
        lines = [l for l in r.splitlines() if l.startswith("| 0x")]
        out("\n".join(lines[:12]) if lines else r[:700])
    else:
        out(json.dumps(r)[:1200])

# The hkClass member tables are arrays of {name*, type*, offset,...}. Dump some
# memory around the hknpCharacterProxy string in case the table sits inline.
out("\n=== memory around 'hknpCharacterProxy' string ===")
out(str(mcp.read(hex(0x145cbd858 - 0x60), 0xC0)))

out("\n=== memory around 'hknpDisableCollisionFilter' ===")
out(str(mcp.read(hex(0x145308a90 - 0x40), 0x100)))
