# Hunt for noclip / collision / flight machinery: strings + the move integrator
import json


def show(title, r, n=60):
    out(f"\n=== {title} ===")
    if isinstance(r, str):
        out(r[:4000])
    else:
        out(json.dumps(r, indent=1)[:4000])


# 1) What does the string list tool accept?
show("bn_string_list raw (probe, limit 5)", mcp.call("bn_string_list", limit=5))

# 2) String hunts that would reveal an engine-side noclip / collision toggle
for q in ["noclip", "NoClip", "ghost", "Fly", "Collision", "CollisionFilter",
          "CharacterProxy", "characterproxy", "MoveSpeedInfo", "VIBE_"]:
    try:
        r = mcp.call("bn_string_list", query=q, limit=25)
        show(f"strings ~ {q}", r)
    except Exception as e:
        out(f"[{q}] ERR {e}")
