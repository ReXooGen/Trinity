import json

print("=== active view ===")
print(json.dumps(mcp.call("bn_binary_view_get_active")))
print("=== info ===")
print(json.dumps(mcp.call("bn_binary_view_info"))[:1200])

needles = [
    "hknpDisableCollisionFilter",
    "hknpCharacterProxy",
    "hknpCharacterProxyCinfo",
    "hknpSetBodyCollisionFilterInfoCommand",
    "collisionFilterInfo",
    "hknpCollisionFilter",
    "LtCharacterProxyIntegrate",
    "hknpCharacterSurfaceInfo",
    "hknpBodyCollisionFilterInfo",
    "hknpBodyCinfo",
    "hknpMotionProperties",
    "hknpCharacterState",
    "CharacterProxyMatchingTable",
    "hknpWorld",
    "hknpCharacterProxyManager",
]
for n in needles:
    r = mcp.call("bn_string_list", query=n, limit=40)
    if isinstance(r, dict):
        items = r.get("strings") or r.get("items") or r.get("results") or r
    else:
        items = r
    print(f"\n--- {n} ---")
    print(json.dumps(items)[:3000])
