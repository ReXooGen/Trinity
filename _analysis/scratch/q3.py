# Havok character controller states + movement component -> noclip levers
import json


def s(title, q, limit=40):
    out(f"\n=== strings ~ {q} ===")
    r = mcp.call("bn_string_list", query=q, limit=limit)
    if isinstance(r, str):
        for line in r.splitlines():
            if line.startswith("| 0x") or line.startswith("| 0x"):
                out("  ", line.strip())
    else:
        out(json.dumps(r)[:1500])


for q in ["HK_CHARACTER", "hknpCharacter", "CharacterProxy", "MoveSpeedInfo",
          "CollisionFilter", "characterController", "CharacterController"]:
    s("", q)

out("\n=== MoveUpdateIntegrator 0x144282080 ===")
out(json.dumps(mcp.call("bn_function_info", function="0x144282080"), indent=1)[:1200])
out("\n--- callees ---")
out(json.dumps(mcp.call("bn_function_callees", function="0x144282080"), indent=1)[:3000])

out("\n=== LocoStepper 0x14369FF60 info ===")
out(json.dumps(mcp.call("bn_function_info", function="0x14369FF60"), indent=1)[:1200])
