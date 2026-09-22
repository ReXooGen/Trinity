# BlackSpace-side movement controller names + any debug/free movement mode
import json

for q in ["MoveController", "CharacterPhysics", "MovementMode", "moveControl",
          "isGhost", "DebugFly", "FreeCamera", "flyMove", "FlyMove"]:
    out(f"\n=== strings ~ {q} ===")
    r = mcp.call("bn_string_list", query=q, limit=30)
    if isinstance(r, str):
        lines = [l for l in r.splitlines() if l.startswith("| 0x")]
        out("\n".join(lines[:25]) if lines else "(none)")
    else:
        out(json.dumps(r)[:900])

out("\n=== xrefs to CharacterPhysicsMoveController string ===")
# string was at 0x145cbd858 + 0x30 (seen in the previous dump window)
addr = 0x145cbd858 + 0x30
out(str(mcp.read(hex(addr - 8), 48)))
out(str(mcp.call("bn_data_xrefs_to", address=hex(addr))))
