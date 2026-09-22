import json, sys
sys.path.insert(0, ".")
from ce import CE

script = open("noclip_test.ct", encoding="utf-8").read()
print("script repr head:", repr(script[:120]))
print("script len:", len(script))

ce = CE()
try:
    r = ce.call("auto_assemble_check", {"script": script})
    print(json.dumps(r, indent=2))
finally:
    ce.close()
