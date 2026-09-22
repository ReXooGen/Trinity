import json

r = mcp.call("bn_symbol_list", query="DisableCollisionFilter")
print("=== symbols matching DisableCollisionFilter ===")
print(json.dumps(r)[:4000])

r2 = mcp.call("bn_symbol_list", query="GroupCollisionFilter")
print("\n=== symbols matching GroupCollisionFilter ===")
print(json.dumps(r2)[:2500])
