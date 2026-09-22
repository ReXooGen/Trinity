# Recon: what collision / noclip / flight machinery exists in CrimsonDesert.exe
import json

out("=== binary info ===")
info = mcp.call("bn_binary_view_info")
out(json.dumps(info, indent=1)[:900])

out("\n=== function search: collision ===")
for q in ["Collision", "NoClip", "Noclip", "Ghost", "CharacterProxy", "FlyMode", "FreeCam"]:
    try:
        r = mcp.call("bn_function_search", query=q)
        n = len(r) if isinstance(r, list) else r
        out(f"[{q}] -> {n}")
        if isinstance(r, list):
            for item in r[:15]:
                out("   ", json.dumps(item)[:220])
    except Exception as e:
        out(f"[{q}] ERROR {e}")
