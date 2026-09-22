import json

def show(title, r, n=4000):
    print(f"\n########## {title} ##########")
    s = r if isinstance(r, str) else json.dumps(r)
    print(s[:n])

# 1) xrefs to the RTTI type-name strings for the collision filters
for name, va in [
    ("hknpDisableCollisionFilter MSVC", 0x146a8f860),
    ("hknpDisableCollisionFilter plain", 0x145308a90),
    ("hknpCollisionFilter plain", 0x145d13858),
    ("hknpCollisionFilter MSVC", 0x146aa3910),
    ("QueryCollector@hknpCharacterProxyInternals", 0x146a926d0),
    ("hknpCharacterProxy MSVC", 0x146a90040),
    ("LtCharacterProxyIntegrate", 0x14530db68),
    ("hknpCharacterProxyManager plain", 0x14530c138),
]:
    show(f"xrefs_to {name} {hex(va)}", mcp.call("bn_data_xrefs_to", address=hex(va)))

# 2) search symbols for proxy/collision/filter related functions
for q in ["CollisionFilter", "CharacterProxy", "QueryCollector", "Contact", "Sweep"]:
    r = mcp.call("bn_function_search", query=q, limit=120)
    show(f"functions matching {q!r}", r, 6000)
