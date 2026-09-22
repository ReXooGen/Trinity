import json, sys
sys.path.insert(0, ".")
from ce import CE

script = open("noclip_test.ct", encoding="utf-8").read()
ce = CE()
try:
    r1 = ce.call("auto_assemble_check", {"script": script, "enable": True})
    r2 = ce.call("auto_assemble_check", {"script": script, "enable": False})
    print("ENABLE:", json.dumps(r1))
    print("DISABLE:", json.dumps(r2))
finally:
    ce.close()
