import json, sys
sys.path.insert(0, ".")
from ce import CE

script = open("noclip_test.ct", encoding="utf-8").read()
ce = CE()
try:
    r = ce.call("auto_assemble_check", {"script": script, "enable": True})
    print(json.dumps(r, indent=2))
finally:
    ce.close()
